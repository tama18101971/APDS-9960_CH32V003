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

/* ============================================================================
 * ОБЕРТКИ НАД I2C (адрес датчика 0x39)
 * ============================================================================ */

/* Чтение одного регистра датчика.
 * reg — адрес регистра (0x80-0xFF).
 * Возвращает: значение регистра (0-255). */
static uint8_t rd(uint8_t reg) {
    uint8_t v = 0;
    i2c_read_register(APDS9960_I2C_ADDR, reg, &v);
    return v;
}

/* Запись одного регистра датчика.
 * reg — адрес регистра, val — записываемое значение. */
static void wr(uint8_t reg, uint8_t val) {
    i2c_write_register(APDS9960_I2C_ADDR, reg, val);
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
 * SENSITIVITY_1 = 50 — Порог накопленной дельты для определения направления.
 * Когда |g_ud_delta| или |g_lr_delta| превышает это значение,
 * жест считается определенным.
 */
#define SENSITIVITY_1           10

/*
 * MAX_FIFO_READS = 32 — Максимальное количество чтений FIFO
 * за один вызов readGesture(). Каждое чтение ~1-2 мс.
 * 32 x ~2 мс = ~64 мс максимум блокировки.
 */
#define MAX_FIFO_READS          32

/*
 * RETRY_LIMIT = 3 — Количество повторных попыток при ошибках I2C.
 */
#define RETRY_LIMIT             3

/* ============================================================================
 * СОСТОЯНИЕ ЖЕСТА (статические переменные)
 * ============================================================================ */

/*
 * g_ud_acc / g_lr_acc — Накопленная сумма изменений Ratio.
 * С каждым новым действительным пакетом вычисляется дельта
 * от предыдущего пакета и прибавляется к аккумулятору.
 */
static int16_t  g_ud_acc;
static int16_t  g_lr_acc;

/*
 * g_prev_ud / g_prev_lr — Ratio предыдущего действительного пакета.
 * Используются для вычисления дельты между соседними пакетами.
 */
static int16_t  g_prev_ud;
static int16_t  g_prev_lr;

/*
 * g_has_prev — флаг: был ли обработан хотя бы один действительный пакет.
 */
static uint8_t  g_has_prev;

/*
 * g_motion — Результат: распознанный жест (gesture_t).
 */
static uint8_t  g_motion;

/* Сброс всех переменных состояния жеста */
static void gesture_reset(void) {
    g_ud_acc = 0;
    g_lr_acc = 0;
    g_prev_ud = 0;
    g_prev_lr = 0;
    g_has_prev = 0;
    g_motion = GESTURE_NONE;
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
    uint8_t fifo_level = rd(REG_GFLVL);
    if (fifo_level == 0) return;

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
        printf("DEC: no data\r\n");
        return false;
    }

    int16_t abs_ud = g_ud_acc < 0 ? -g_ud_acc : g_ud_acc;
    int16_t abs_lr = g_lr_acc < 0 ? -g_lr_acc : g_lr_acc;

    printf("DEC: ud_acc=%d lr_acc=%d abs_ud=%d abs_lr=%d thr=%d\r\n",
           g_ud_acc, g_lr_acc, abs_ud, abs_lr, SENSITIVITY_1);

    /* Нет значимого движения */
    if (abs_ud < SENSITIVITY_1 && abs_lr < SENSITIVITY_1) {
        printf("DEC: no motion (below threshold)\r\n");
        return false;
    }

    /* Определяем доминирующую ось */
    if (abs_ud > abs_lr) {
        g_motion = (g_ud_acc > 0) ? GESTURE_UP : GESTURE_DOWN;
    } else {
        g_motion = (g_lr_acc > 0) ? GESTURE_LEFT : GESTURE_RIGHT;
    }
    printf("DEC: motion=%d\r\n", g_motion);
    return true;
}

/* ============================================================================
 * ПЕРЕИНИЦИАЛИЗАЦИЯ ДАТЧИКА (при ошибках FIFO / I2C)
 *
 * Полный повторный набор всех регистров. Используется при:
 *   - переполнении FIFO (GFOV = 1)
 *   - зависании датчика
 *   - ошибке I2C шины
 * ============================================================================ */

/* Полная переинициализация датчика APDS9960.
 * Сбрасывает все регистры к начальным значениям.
 * Аналогично apds_init(), но без проверки ID. */
static void sensor_reinit(void) {
    /* Отключаем все функции (регистр ENABLE = 0x00) */
    wr(REG_ENABLE, 0x00);

    /* Задержка ~1 мс (NOP-цикл на 48 МГц) */
    for (volatile uint16_t d = 0; d < 2000; d++) { __asm__ volatile("nop"); }

    /* --- Тайминги --- */
    /* ATIME = 0xDB (219): время интегрирования ALS = (256-219)*3.8мс = 140 мс */
    wr(REG_ATIME, 0xDB);
    /* WTIME = 0xFF (255): время ожидания = (256-255)*2.8мс = 2.8 мс */
    wr(REG_WTIME, 0xFF);

    /* --- Proximity --- */
    /* PPULSE = 0x87: длина импульса 16 мкс, количество 8 импульсов.
     * Биты [7:6] = 10 (16 мкс), биты [5:0] = 000111 (7+1=8 импульсов) */
    wr(REG_PPULSE, 0x87);
    /* Смещения proximity: 0 = без коррекции */
    wr(REG_POFFSET_UR, 0x00);
    wr(REG_POFFSET_DL, 0x00);
    /* Пороги proximity: PILT=0 (минимум), PIHT=50 (верхний порог) */
    wr(REG_PILT, 0x00);
    wr(REG_PIHT, (uint8_t)(APDS_PROX_THRESHOLD > 255 ? 255 : APDS_PROX_THRESHOLD));

    /* --- Регистр CONTROL (0x8F) --- */
    /* [7:6] LEDDRIVE=0 (100mA), [3:2] PGAIN=2 (4x), [1:0] AGAIN=0 (1x) */
    uint8_t ctrl = ((uint8_t)APDS_LED_CURRENT << CTRL_LED_SHIFT) |
                   ((uint8_t)APDS_GAIN << CTRL_PGAIN_SHIFT);
    wr(REG_CONTROL, ctrl);

    /* --- Конфигурация --- */
    /* CONFIG1 = 0x60: бит 5 = без 12x множителя WTIME */
    wr(REG_CONFIG1, 0x60);
    /* CONFIG2 = 0x31: LED_BOOST=300% (биты [5:4]=11), без прерываний насыщения */
    wr(REG_CONFIG2, 0x31);
    /* CONFIG3 = 0x00: все фотодиоды активны, без SAI (автономный сон) */
    wr(REG_CONFIG3, 0x00);
    /* PERS = 0x11: персистентность = 2 подряд для prox/ALS прерывания */
    wr(REG_PERS, 0x11);

    /* --- Жестовый режим --- */
    /* GPENTH = 50: порог входа в жестовый режим */
    wr(REG_GPENTH, APDS_PROX_THRESHOLD);
    /* GEXTH = 30: порог выхода из жестового режима */
    wr(REG_GEXTH, APDS_GESTURE_EXIT_TH);
    /* GCONF1 = 0x40: 4 события FIFO для прерывания, 1 для выхода */
    wr(REG_GCONF1, 0x40);

    /* --- GCONF2 (0xA3) --- */
    /* [6:5] GGLDRIVE=0 (100mA), [4:3] GGAIN=2 (4x), [2:0] GWTIME=1 (2.8мс) */
    uint8_t gconf2 = ((uint8_t)APDS_GLDRIVE << GCONF2_GLDRIVE_SHIFT) |
                     ((uint8_t)APDS_GGAIN << GCONF2_GGAIN_SHIFT) |
                     ((uint8_t)APDS_GWTIME);
    wr(REG_GCONF2, gconf2);

    /* Смещения жестовых фотодиодов: 0 = без коррекции */
    wr(REG_GOFFSET_U, 0x00);
    wr(REG_GOFFSET_D, 0x00);
    wr(REG_GOFFSET_L, 0x00);
    wr(REG_GOFFSET_R, 0x00);

    /* GPULSE = 0xC9: длина импульса 32 мкс, количество 10 импульсов.
     * Биты [7:6] = 11 (32 мкс), биты [5:0] = 001001 (9+1=10 импульсов) */
    wr(REG_GPULSE, 0xC9);

    /* GCONF3 = 0x00: все 4 фотодиода активны во время жеста */
    wr(REG_GCONF3, 0x00);

    /* GCONF4 = 0x01: GMODE=1 (жестовый конечный автомат включен),
     * GIEN=0 (прерывания выключены).
     * GMODE=1 обязателен — без него GVALID никогда не станет 1 */
    wr(REG_GCONF4, 0x01);

    /* --- Включение --- */
    /* ENABLE = 0x4D: PON(0x01) | PEN(0x04) | GEN(0x40) | WEN(0x08) = 0x4D */
    wr(REG_ENABLE, EN_PON | EN_PEN | EN_GEN | EN_WEN);
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
    /* --- Шаг 1: Проверка ID датчика --- */
    /* Известные ID: 0xAB (оригинал), 0xA8, 0x9C, 0x9E (клоны) */
    uint8_t id = rd(REG_ID);
    if (id != 0xAB && id != 0xA8 && id != 0x9C && id != 0x9E) {
        return false; /* Датчик не распознан или не отвечает по I2C */
    }

    /* --- Шаг 2: Отключение всех функций --- */
    wr(REG_ENABLE, 0x00);

    /* --- Шаг 3: Тайминги --- */
    /* ATIME = 0xDB (219): ALS интегрирование = 140 мс */
    wr(REG_ATIME, 0xDB);
    /* WTIME = 0xFF (255): ожидание = 2.8 мс */
    wr(REG_WTIME, 0xFF);

    /* --- Шаг 4: Proximity --- */
    /* PPULSE = 0x87: 16 мкс x 8 импульсов */
    wr(REG_PPULSE, 0x87);
    /* POFFSET_UR = 0x00: смещение UP/RIGHT = 0 */
    wr(REG_POFFSET_UR, 0x00);
    /* POFFSET_DL = 0x00: смещение DOWN/LEFT = 0 */
    wr(REG_POFFSET_DL, 0x00);
    /* PILT = 0x00: нижний порог proximity = 0 (всегда выше) */
    wr(REG_PILT, 0x00);
    /* PIHT = 50: верхний порог proximity (APDS_PROX_THRESHOLD) */
    wr(REG_PIHT, (uint8_t)(APDS_PROX_THRESHOLD > 255 ? 255 : APDS_PROX_THRESHOLD));

    /* --- Шаг 5: CONTROL (0x8F) --- */
    /* [7:6] LEDDRIVE = 0 (100mA), [3:2] PGAIN = 2 (4x), [1:0] AGAIN = 0 (1x) */
    uint8_t ctrl = ((uint8_t)APDS_LED_CURRENT << CTRL_LED_SHIFT) |
                   ((uint8_t)APDS_GAIN << CTRL_PGAIN_SHIFT);
    wr(REG_CONTROL, ctrl);

    /* --- Шаг 6: Конфигурация --- */
    /* CONFIG1 = 0x60: WTIME x 1 (без 12x множителя) */
    wr(REG_CONFIG1, 0x60);
    /* CONFIG2 = 0x31: LED_BOOST 300%, без прерываний */
    wr(REG_CONFIG2, 0x31);
    /* CONFIG3 = 0x00: все фотодиоды, без SAI */
    wr(REG_CONFIG3, 0x00);
    /* PERS = 0x11: 2 подряд prox/ALS для прерывания */
    wr(REG_PERS, 0x11);

    /* --- Шаг 7: Жестовый режим --- */
    /* GPENTH = 50: порог входа в gesture mode */
    wr(REG_GPENTH, APDS_PROX_THRESHOLD);
    /* GEXTH = 30: порог выхода из gesture mode */
    wr(REG_GEXTH, APDS_GESTURE_EXIT_TH);
    /* GCONF1 = 0x40: 4 FIFO для int, 1 для exit */
    wr(REG_GCONF1, 0x40);

    /* GCONF2: [6:5]=GGLDRIVE(0=100mA), [4:3]=GGAIN(2=4x), [2:0]=GWTIME(1=2.8мс) */
    uint8_t gconf2 = ((uint8_t)APDS_GLDRIVE << GCONF2_GLDRIVE_SHIFT) |
                     ((uint8_t)APDS_GGAIN << GCONF2_GGAIN_SHIFT) |
                     ((uint8_t)APDS_GWTIME);
    wr(REG_GCONF2, gconf2);

    /* Смещения: все 0 (без коррекции) */
    wr(REG_GOFFSET_U, 0x00);
    wr(REG_GOFFSET_D, 0x00);
    wr(REG_GOFFSET_L, 0x00);
    wr(REG_GOFFSET_R, 0x00);

    /* GPULSE = 0xC9: 32 мкс x 10 импульсов */
    wr(REG_GPULSE, 0xC9);

    /* GCONF3 = 0x00: все фотодиоды активны */
    wr(REG_GCONF3, 0x00);

    /* GCONF4 = 0x01: GMODE=1 (жестовый конечный автомат включен),
     * GIEN=0 (прерывания выключены) */
    wr(REG_GCONF4, 0x01);

    /* --- Шаг 8: Включение --- */
    /* ENABLE = 0x4D: PON | PEN | GEN | WEN */
    wr(REG_ENABLE, EN_PON | EN_PEN | EN_GEN | EN_WEN);

    /* --- Шаг 9: Проверка PON --- */
    if (!(rd(REG_ENABLE) & EN_PON)) return false;

    gesture_reset();
    return true;
}

/* Перевод датчика в режим сна.
 * Отключает PEN, GEN, WEN — остаётся только PON.
 * Потребление: ~1 мкА (питание только на логику).
 * Возвращает: true всегда. */
bool apds_sleep(void) {
    /* ENABLE = 0x01: только PON (питание включено, функции выключены) */
    wr(REG_ENABLE, EN_PON);
    return true;
}

/* Пробуждение датчика из режима сна.
 * Включает PEN + GEN + WEN обратно.
 * Возвращает: true если PON установлен после записи. */
bool apds_wakeup(void) {
    /* ENABLE = 0x4D: PON | PEN | GEN | WEN */
    wr(REG_ENABLE, EN_PON | EN_PEN | EN_GEN | EN_WEN);
    return (rd(REG_ENABLE) & EN_PON) != 0;
}

/* Проверка наличия данных жеста.
 * Читает регистр GSTATUS (0xAF):
 *   Бит 0 (GVALID): 1 = данные в FIFO готовы к чтению
 *   Бит 1 (GFOV):   1 = переполнение FIFO (нужна переинициализация)
 * При переполнении FIFO вызывает sensor_reinit().
 * Возвращает: true если данные готовы. */
bool apds_available(void) {
    uint8_t gs = rd(REG_GSTATUS);

    /* Переполнение FIFO -> сброс и переинициализация */
    if (gs & GST_GFOV) {
        gesture_reset();
        sensor_reinit();
        return false;
    }

    /* GVALID = 1 -> данные готовы */
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
    while (loops < MAX_FIFO_READS) {
        uint8_t gs = rd(REG_GSTATUS);

        /* GVALID=0 -> поток данных завершен, декодируем накопленное */
        if (!(gs & GST_GVALID)) {
            decode_gesture();
            return g_motion;
        }

        /* GFOV=1 -> переполнение FIFO, прерываем */
        if (gs & GST_GFOV) {
            gesture_reset();
            return GESTURE_NONE;
        }

        /* Обрабатываем батч пакетов из FIFO */
        process_fifo_batch();
        loops++;

        /* Задержка ~0.1 мс (NOP-цикл) для накопления данных в FIFO */
        for (volatile uint16_t d = 0; d < 500; d++) { __asm__ volatile("nop"); }
    }

    /* Достигнут лимит итераций — декодируем что есть */
    decode_gesture();
    return g_motion;
}
