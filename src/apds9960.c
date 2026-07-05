/*
 * apds9960.c — Драйвер распознавания жестов для APDS9960
 *
 * Алгоритм основан на соотношениях U/D и L/R из FIFO-буфера датчика.
 * Не использует float/double, только целочисленную арифметику.
 * Потребление RAM: ~20 байт статика.
 */

#include "apds9960.h"
#include "apds9960_regs.h"
#include "i2c.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <debug.h>

/* ============================================================================
 * ОБЕРТКИ НАД I2C (адрес датчика 0x39)
 * ============================================================================ */

/* Чтение одного регистра датчика.
 * reg — адрес регистра (0x80-0xFF), out — указатель для значения.
 * Возвращает: true если I2C успешен. */
static bool rd(uint8_t reg, uint8_t *out) {
    return i2c_read_register(APDS9960_I2C_ADDR, reg, out) == I2C_OK;
}

/* Запись одного регистра датчика.
 * reg — адрес регистра, val — записываемое значение.
 * Возвращает: true если I2C успешен. */
static bool wr(uint8_t reg, uint8_t val) {
    return i2c_write_register(APDS9960_I2C_ADDR, reg, val) == I2C_OK;
}

/* Блочное чтение из регистра (автоинкремент адреса).
 * reg — начальный адрес регистра, buf — буфер, len — количество байт.
 * Возвращает: true если успешно. */
static bool rdBlock(uint8_t reg, uint8_t *buf, uint8_t len) {
    return i2c_read_buffer(APDS9960_I2C_ADDR, reg, buf, len) == I2C_OK;
}

/* ============================================================================
 * КОНСТАНТЫ ФИЛЬТРАЦИИ И АЛГОРИТМА
 * ============================================================================ */

/*
 * THRESH_OUT = 10 — Минимальное значение по каждому каналу (U/D/L/R)
 * для учета пакета. Если все 4 значения меньше этого порога,
 * пакет считается шумом и отбрасывается.
 */
#define THRESH_OUT              10

/*
 * SENSITIVITY_1 = 5 — Порог накопленной дельты для определения направления.
 * Когда |g_ud_acc| или |g_lr_acc| превышает это значение,
 * жест считается определенным.
 */
#define SENSITIVITY_1           5

/*
 * MAX_FIFO_READS = 32 — Максимальное количество чтений FIFO
 * за один вызов readGesture(). Каждое чтение ~1-2 мс.
 * 32 x ~2 мс = ~64 мс максимум блокировки.
 */
#define MAX_FIFO_READS          32

/*
 * RETRY_LIMIT = 3 — Количество повторных попыток при ошибках I2C.
 */
#define RETRY_LIMIT             6

/* Сохранённые откалиброванные пороги (F7) — объявлены до calibrate_proximity() */
static uint8_t  g_cal_piht;
static uint8_t  g_cal_gpenth;
static uint8_t  g_cal_gexth;
static uint8_t  g_cal_valid;

#if APDS_ENABLE_CALIBRATION
/* Целочисленный квадратный корень (алгоритм Ньютона).
 * Используется при калибровке для вычисления σ.
 * Возвращает: floor(√n). */
static uint8_t isqrt(uint16_t n) {
    if (n == 0) return 0;
    uint16_t x = n;
    uint16_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (uint16_t)((x + n / x) / 2);
    }
    return (uint8_t)x;
}

/* Простейшая сортировка вставками для uint8_t (32 элемента). */
static void sort_u8(uint8_t *arr, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {
        uint8_t key = arr[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

/* Автоматическая калибровка порогов proximity.
 * Работает в gesture mode (PON+PEN+GEN+WEN) — без переключения режимов.
 * Собирает APDS_CAL_SAMPLES замеров PDATA с отсевом насыщения (>FILTER_MAX),
 * вычисляет медиану и σ, устанавливает PIHT / GPENTH / GEXTH,
 * сохраняет их для sensor_reinit().
 * Gesture FIFO дрainится перед каждым чтением PDATA для предотвращения GFOV.
 * Если калибровка невозможна — используются дефолты.
 * Возвращает: true (датчик остаётся в gesture mode). */
static bool calibrate_proximity(void) {
    uint8_t buf[APDS_CAL_SAMPLES];
    uint8_t valid_cnt = 0;

    /* Датчик уже в gesture mode (PON+PEN+GEN+WEN) после configure_registers().
     * Переключение в proximity-only НЕ требуется — PDATA в gesture mode
     * отражает реальные условия работы (GLDRIVE + GGAIN, без LED_BOOST). */
    Delay_Ms(100);  /* Стабилизация gesture mode (больше чем proximity-only) */

    /* Сбор замеров PDATA с фильтрацией насыщения и drain FIFO */
    {
        uint16_t pv_timeout = 100;
        uint8_t total = 0;
        while (valid_cnt < APDS_CAL_SAMPLES && total < APDS_CAL_SAMPLES * 2) {
            /* Drain gesture FIFO — предотвращаем GFOV во время калибровки */
            uint8_t fifo_level;
            if (!rd(REG_GFLVL, &fifo_level)) return false;
            while (fifo_level > 0) {
                uint8_t dummy[4];
                if (!rdBlock(REG_GFIFO_U, dummy, 4)) return false;
                fifo_level--;
            }

            uint8_t status;
            if (!rd(REG_STATUS, &status)) return false;

            if (!(status & ST_PVALID)) {
                if (--pv_timeout == 0) break;
                Delay_Ms(1);
                continue;
            }

            uint8_t data;
            if (!rd(REG_PDATA, &data)) return false;
            total++;
            if (data > APDS_CAL_FILTER_MAX) continue;  /* насыщение — пропускаем */

            buf[valid_cnt++] = data;
            pv_timeout = 100;
            Delay_Ms(10);
        }
    }

    /* Если валидных замеров меньше половины — среда загрязнена, используем дефолты */
    if (valid_cnt < APDS_CAL_SAMPLES / 2) {
        g_cal_valid = 0;
#ifdef APDS9960_DEBUG
        printf("CAL: FAIL (saturated, valid=%d) using defaults gpenth=%d gexth=%d\r\n",
               valid_cnt, APDS_PROX_THRESHOLD, APDS_GESTURE_EXIT_TH);
#endif
        return true;
    }

    /* Сортируем и берём медиану */
    sort_u8(buf, valid_cnt);
    uint8_t median;
    if (valid_cnt & 1) {
        median = buf[valid_cnt / 2];
    } else {
        median = (uint8_t)(((uint16_t)buf[valid_cnt / 2 - 1] + (uint16_t)buf[valid_cnt / 2]) / 2);
    }

    /* Дисперсия от медианы (со сдвигом /4 для избежания переполнения) */
    uint16_t sum_sq = 0;
    for (uint8_t i = 0; i < valid_cnt; i++) {
        int16_t d = (int16_t)buf[i] - (int16_t)median;
        int16_t d4 = d / 4;
        sum_sq += (uint16_t)(d4 * d4);
    }
    uint16_t var = sum_sq / valid_cnt;
    uint8_t sigma = isqrt(var) * 4;

    /* Вычисление порогов */
    uint16_t entry;
    uint16_t exit_th;

    if (median > 100) {
        /* Gesture mode: порог НИЖЕ фона (gesture engine всегда активен).
         * GPENTH = median / 4, GEXTH = GPENTH * 60% */
        entry = (uint16_t)median / 4;
        exit_th = entry * 6 / 10;
    } else {
        /* Proximity mode: порог ВЫШЕ фона (median + N*sigma) */
        entry = (uint16_t)median + (uint16_t)APDS_CAL_SIGMA_COEFF * sigma;
        exit_th = entry * 6 / 10;
    }

    if (entry > APDS_CAL_PROX_MAX) entry = APDS_CAL_PROX_MAX;
    if (entry < APDS_CAL_PROX_MIN) entry = APDS_CAL_PROX_MIN;
    if (exit_th < 1) exit_th = 1;

    uint8_t prox_th = (uint8_t)entry;
    uint8_t exit_val = (uint8_t)exit_th;

    /* Sanity check + сохранение откалиброванных порогов */
    if (median > 100) {
        /* Gesture mode: порог должен быть ниже фона, но не > 100 */
        if (prox_th > 100) {
            prox_th = APDS_PROX_THRESHOLD;
            exit_val = APDS_GESTURE_EXIT_TH;
            g_cal_valid = 0;
        } else {
            g_cal_piht   = prox_th;
            g_cal_gpenth = prox_th;
            g_cal_gexth  = exit_val;
            g_cal_valid  = 1;
        }
    } else {
        /* Proximity mode: порог выше фона, не > 100 и не < дефолта */
        if (prox_th > 100 || prox_th < APDS_PROX_THRESHOLD) {
            prox_th = APDS_PROX_THRESHOLD;
            exit_val = APDS_GESTURE_EXIT_TH;
            g_cal_valid = 0;
        } else {
            g_cal_piht   = prox_th;
            g_cal_gpenth = prox_th;
            g_cal_gexth  = exit_val;
            g_cal_valid  = 1;
        }
    }

    /* Запись порогов (датчик уже в gesture mode — не переключаем) */
    if (!wr(REG_PIHT, prox_th)) return false;
    if (!wr(REG_GPENTH, prox_th)) return false;
    if (!wr(REG_GEXTH, exit_val)) return false;

#ifdef APDS9960_DEBUG
    printf("CAL: median=%d valid=%d sigma=%d gpenth=%d gexth=%d\r\n",
           median, valid_cnt, sigma, prox_th, exit_val);
#endif

    return true;
}
#endif /* APDS_ENABLE_CALIBRATION */

/* ============================================================================
 * СОСТОЯНИЕ ЖЕСТА (статические переменные)
 * ============================================================================ */

/*
 * g_ud_acc / g_lr_acc — Накопленная сумма изменений Ratio.
 * С каждым новым действительным пакетом вычисляется дельта
 * от предыдущего пакета и прибавляется к аккумулятору.
 */
static volatile int16_t  g_ud_acc;
static volatile int16_t  g_lr_acc;

/*
 * g_prev_ud / g_prev_lr — Ratio предыдущего действительного пакета.
 * Используются для вычисления дельты между соседними пакетами.
 */
static volatile int16_t  g_prev_ud;
static volatile int16_t  g_prev_lr;

/*
 * g_has_prev — флаг: был ли обработан хотя бы один действительный пакет.
 */
static volatile uint8_t  g_has_prev;

/*
 * g_packet_count — Количество обработанных действительных пакетов.
 * Используется для фильтрации шума: жест не определяется
 * при малом количестве пакетов.
 */
static volatile uint8_t  g_packet_count;

static volatile uint8_t  g_motion;

/* g_reinit_count — Счётчик последовательных переинициализаций.
 * Сбрасывается при успешном чтении GSTATUS.
 * Если превышает RETRY_LIMIT — датчик считается неисправным. */
static volatile uint8_t  g_reinit_count;

/* g_last_error — Код последней ошибки для диагностики.
 * Устанавливается функциями драйвера при сбоях. */
static volatile uint8_t  g_last_error;

/* Сброс всех переменных состояния жеста */
static void gesture_reset(void) {
    g_ud_acc = 0;
    g_lr_acc = 0;
    g_prev_ud = 0;
    g_prev_lr = 0;
    g_has_prev = 0;
    g_packet_count = 0;
    g_motion = GESTURE_NONE;
    g_reinit_count = 0;
}

/* ============================================================================
 * ОБРАБОТКА ПАКЕТОВ FIFO
 *
 * Формат одного пакета FIFO: 4 байта [U, D, L, R]
 *   U (Up)    — значение верхнего фотодиода
 *   D (Down)  — значение нижнего фотодиода
 *   L (Left)  — значение левого фотодиода
 *   R (Right) — значение правого фотодиода
 *
 * Значения: 0-255 (8 байт), больше = рука ближе.
 *
 * Алгоритм:
 *   1. Читаем FIFO_LEVEL — сколько пакетов готово
 *   2. Для каждого пакета читаем 4 байта из регистра GFIFO_U (0xFC)
 *      Адрес автоинкрементируется: 0xFC -> U, 0xFD -> D, 0xFE -> L, 0xFF -> R
 *   3. Фильтруем: пропускаем пакеты где любой канал > 250 (насыщение)
 *      или все каналы < 10 (шум)
 *   4. Запоминаем первый и последний действительные пакеты
 *   5. Вычисляем Ratio для каждого: (U-D)*100/(U+D)
 *   6. Delta = Ratio_последний - Ratio_первый
 *   7. Накапливаем: g_ud_delta += ud_delta
 * ============================================================================ */

/* Обработка одного батча пакетов FIFO.
 * Каждый новый пакет: вычисляем дельту от предыдущего и прибавляем к аккумулятору.
 * Ratio = (U-D)*100/(U+D), диапазон -100..+100 */
static void process_fifo_batch(void) {
    uint8_t fifo_level = 0;
    if (!rd(REG_GFLVL, &fifo_level) || fifo_level == 0) return;

    uint8_t buf[4]; /* [U, D, L, R] */

    for (uint8_t i = 0; i < fifo_level; i++) {
        if (!rdBlock(REG_GFIFO_U, buf, 4)) return;

        uint8_t u = buf[0], d = buf[1], l = buf[2], r = buf[3];

        /* Фильтр: насыщение (> 250) или шум (все < 10) */
        if (u > 250 || d > 250 || l > 250 || r > 250) continue;
        if (u < THRESH_OUT && d < THRESH_OUT && l < THRESH_OUT && r < THRESH_OUT) continue;

        /* Вычисляем Ratio для этого пакета */
        int16_t ud_sum = (int16_t)u + d;
        int16_t lr_sum = (int16_t)l + r;
        int16_t ud_ratio = 0, lr_ratio = 0;
        if (ud_sum > 0) ud_ratio = ((int16_t)u - d) * 100 / ud_sum;
        if (lr_sum > 0) lr_ratio = ((int16_t)l - r) * 100 / lr_sum;

        /* Первый пакет — просто запоминаем, накапливать нечего */
        if (!g_has_prev) {
            g_prev_ud = ud_ratio;
            g_prev_lr = lr_ratio;
            g_has_prev = 1;
            continue;
        }

        /* Накапливаем дельту: текущий - предыдущий */
        g_ud_acc += ud_ratio - g_prev_ud;
        g_lr_acc += lr_ratio - g_prev_lr;

        /* Запоминаем как предыдущий для следующего пакета */
        g_prev_ud = ud_ratio;
        g_prev_lr = lr_ratio;
        g_packet_count++;
    }
}

/* ============================================================================
 * ДЕКОДИРОВАНИЕ НАПРАВЛЕНИЯ
 *
 * Матрица определения направления по g_ud_count и g_lr_count:
 *
 *   g_lr_count:  -1 (лево)    0 (нет)      +1 (право)
 *   g_ud_count:
 *   -1 (вверх)   UP или LEFT  UP           UP или RIGHT
 *    0 (нет)     LEFT         --           RIGHT
 *   +1 (вниз)   DOWN или LEFT DOWN         DOWN или RIGHT
 *
 * Для диагональных движений: сравниваем |g_ud_delta| и |g_lr_delta|.
 * Если |UD| > |LR| -> вертикальное, иначе -> горизонтальное.
 * ============================================================================ */

/* Декодирование: определяет направление по накопленным изменениям.
 * Возвращает: true если жест определен. */
static bool decode_gesture(void) {
    if (!g_has_prev) {
#ifdef APDS9960_DEBUG
        printf("DEC: no data\r\n");
#endif
        return false;
    }

    if (g_packet_count < APDS_FIFO_MIN_PACKETS) {
#ifdef APDS9960_DEBUG
        printf("DEC: too few packets (%d)\r\n", g_packet_count);
#endif
        return false;
    }

    int16_t abs_ud = g_ud_acc < 0 ? -g_ud_acc : g_ud_acc;
    int16_t abs_lr = g_lr_acc < 0 ? -g_lr_acc : g_lr_acc;

#ifdef APDS9960_DEBUG
    printf("DEC: ud_acc=%d lr_acc=%d pkts=%d thr=%d\r\n",
           g_ud_acc, g_lr_acc, g_packet_count, SENSITIVITY_1);
#endif

    if (abs_ud < SENSITIVITY_1 && abs_lr < SENSITIVITY_1) {
#ifdef APDS9960_DEBUG
        printf("DEC: no motion (below threshold)\r\n");
#endif
        return false;
    }

    if (abs_ud > abs_lr) {
        g_motion = (g_ud_acc > 0) ? GESTURE_UP : GESTURE_DOWN;
    } else {
        g_motion = (g_lr_acc > 0) ? GESTURE_LEFT : GESTURE_RIGHT;
    }
#ifdef APDS9960_DEBUG
    printf("DEC: motion=%d\r\n", g_motion);
#endif
    return true;
}

/* ============================================================================
 * КАЛИБРОВКА ПОРОГОВ PROXIMITY
 * ============================================================================ */

/* Полная конфигурация всех регистров датчика.
 * Отключает все функции, настраивает тайминги, proximity, CONTROL, 
 * жестовый режим и включает PON+PEN+GEN+WEN. */
static bool configure_registers(void) {
    if (!wr(REG_ENABLE, 0x00)) return false;

    Delay_Ms(1);

    /* --- Тайминги --- */
    if (!wr(REG_ATIME, 0xDB)) return false;    /* ALS интегрирование 140 мс */
    if (!wr(REG_WTIME, 0xFF)) return false;    /* ожидание 2.8 мс */

    /* --- Proximity --- */
    if (!wr(REG_PPULSE, 0x87)) return false;   /* 16 мкс x 8 импульсов */
    if (!wr(REG_POFFSET_UR, 0x00)) return false;
    if (!wr(REG_POFFSET_DL, 0x00)) return false;
    if (!wr(REG_PILT, 0x00)) return false;
    if (!wr(REG_PIHT, (uint8_t)(APDS_PROX_THRESHOLD > 255 ? 255 : APDS_PROX_THRESHOLD))) return false;

    /* --- CONTROL (0x8F) --- */
    uint8_t ctrl = ((uint8_t)APDS_LED_CURRENT << CTRL_LED_SHIFT) |
                   ((uint8_t)APDS_GAIN << CTRL_PGAIN_SHIFT);
    if (!wr(REG_CONTROL, ctrl)) return false;

    /* --- Конфигурация --- */
    if (!wr(REG_CONFIG1, 0x60)) return false;
    if (!wr(REG_CONFIG2, 0x31)) return false;
    if (!wr(REG_CONFIG3, 0x00)) return false;
    if (!wr(REG_PERS, 0x11)) return false;

    /* --- Жестовый режим --- */
    if (!wr(REG_GPENTH, APDS_PROX_THRESHOLD)) return false;
    if (!wr(REG_GEXTH, APDS_GESTURE_EXIT_TH)) return false;
    if (!wr(REG_GCONF1, 0x40)) return false;

    /* --- GCONF2 (0xA3) --- */
    uint8_t gconf2 = ((uint8_t)APDS_GLDRIVE << GCONF2_GLDRIVE_SHIFT) |
                     ((uint8_t)APDS_GGAIN << GCONF2_GGAIN_SHIFT) |
                     ((uint8_t)APDS_GWTIME);
    if (!wr(REG_GCONF2, gconf2)) return false;

    if (!wr(REG_GOFFSET_U, 0x00)) return false;
    if (!wr(REG_GOFFSET_D, 0x00)) return false;
    if (!wr(REG_GOFFSET_L, 0x00)) return false;
    if (!wr(REG_GOFFSET_R, 0x00)) return false;

    /* GPULSE = 0xC9: 32 мкс x 10 импульсов */
    if (!wr(REG_GPULSE, 0xC9)) return false;

    if (!wr(REG_GCONF3, 0x00)) return false;
    if (!wr(REG_GCONF4, 0x01)) return false;

    /* --- Включение --- */
    if (!wr(REG_ENABLE, EN_PON | EN_PEN | EN_GEN | EN_WEN)) return false;

    return true;
}

/* Полная переинициализация датчика.
 * Используется при переполнении FIFO или ошибках I2C.
 * После аппаратной настройки восстанавливает откалиброванные пороги. */
static void sensor_reinit(void) {
    configure_registers();
    if (g_cal_valid) {
        wr(REG_PIHT,   g_cal_piht);
        wr(REG_GPENTH, g_cal_gpenth);
        wr(REG_GEXTH,  g_cal_gexth);
    }
}

/* ============================================================================
 * ПУБЛИЧНЫЙ API
 * ============================================================================ */

/* Инициализация датчика APDS9960.
 *
 * Последовательность:
 *   1. Проверка ID (0xAB, 0xA8, 0x9C, 0x9E)
 *   2. Отключение всех функций (ENABLE = 0x00)
 *   3. Настройка таймингов (ATIME, WTIME)
 *   4. Настройка proximity (пороги, импульсы)
 *   5. Настройка CONTROL (ток LED, усиление)
 *   6. Настройка CONFIG1/2/3 (множители, буст, фильтры)
 *   7. Настройка жестового режима (пороги, усиление, смещения)
 *   8. Включение: PON + PEN + GEN + WEN
 *   9. Проверка что PON установлен
 *
 * Возвращает: true если датчик отвечает и настроен. */
bool apds_init(void) {
    uint8_t id;
    if (!rd(REG_ID, &id)) return false;
    if (id != 0xAB && id != 0xA8 && id != 0x9C && id != 0x9E) {
        return false;
    }

    for (uint8_t attempt = 0; attempt < RETRY_LIMIT; attempt++) {
        if (configure_registers()) {
            uint8_t en;
            if (rd(REG_ENABLE, &en) && (en & EN_PON)) {
                gesture_reset();

#if APDS_ENABLE_CALIBRATION
                if (!calibrate_proximity()) return false;
#endif
                return true;
            }
        }
        Delay_Ms(50);
    }

    g_last_error = APDS_ERR_SENSOR_HANG;
    return false;
}

/* Перевод датчика в режим сна.
 * Отключает PEN, GEN, WEN — остаётся только PON.
 * Потребление: ~1 мкА (питание только на логику).
 * Возвращает: true всегда. */
bool apds_sleep(void) {
    wr(REG_ENABLE, EN_PON);
    return true;
}

bool apds_wakeup(void) {
    if (!wr(REG_ENABLE, EN_PON | EN_PEN | EN_GEN | EN_WEN)) return false;
    uint8_t en;
    return rd(REG_ENABLE, &en) && (en & EN_PON) != 0;
}

bool apds_available(void) {
    uint8_t gs;
    if (!rd(REG_GSTATUS, &gs)) {
        g_last_error = APDS_ERR_I2C;
        return false;
    }

    if (gs & GST_GFOV) {
        gesture_reset();
        g_last_error = APDS_ERR_FIFO_OVERFLOW;
        if (g_reinit_count < RETRY_LIMIT) {
            g_reinit_count++;
            sensor_reinit();
        }
        return false;
    }

    g_reinit_count = 0;
    return (gs & GST_GVALID) != 0;
}

/* Чтение распознанного жеста (блокирующая функция).
 *
 * Алгоритм:
 *   1. Сбрасывает состояние
 *   2. Цикл пока GVALID=1 (макс. MAX_FIFO_READS=32 итераций):
 *      a. Читает GSTATUS
 *      b. Если GVALID=0 -> жест завершен, декодируем
 *      c. Если GFOV=1 -> переполнение, выходим
 *      d. Обрабатываем батч FIFO (process_fifo_batch)
 *      e. Задержка ~500 NOP (ожидание новых данных)
 *   3. Декодируем финальное направление
 *
 * Возвращает: gesture_t — тип жеста или GESTURE_NONE. */
gesture_t apds_readGesture(void) {
    gesture_reset();

    uint16_t loops = 0;
    uint32_t elapsed_ms = 0;

    while (loops < MAX_FIFO_READS && elapsed_ms < APDS_GESTURE_TIMEOUT_MS) {
        uint8_t gs;
        if (!rd(REG_GSTATUS, &gs)) {
            g_last_error = APDS_ERR_I2C;
            break;
        }

        if (!(gs & GST_GVALID)) {
            decode_gesture();
            return g_motion;
        }

        if (gs & GST_GFOV) {
            gesture_reset();
            return GESTURE_NONE;
        }

        process_fifo_batch();
        loops++;

        Delay_Ms(1);
        elapsed_ms += 1;
    }

    decode_gesture();
    return g_motion;
}

bool apds_readProximity(uint8_t *value) {
    return rd(REG_PDATA, value);
}

bool apds_readStatus(uint8_t *value) {
    return rd(REG_STATUS, value);
}

uint8_t apds_getLastError(void) {
    return g_last_error;
}

uint8_t apds_getReinitCount(void) {
    return g_reinit_count;
}

bool apds_recalibrate(void) {
#if APDS_ENABLE_CALIBRATION
    return calibrate_proximity();
#else
    (void)0;
    return true;
#endif
}
