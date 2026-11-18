#ifndef APDS9960_H
#define APDS9960_H

#define APDS9960_VERSION "1.4.0"

/*
 * apds9960.h — Драйвер распознавания жестов для APDS9960 (CH32V003)
 *
 * Поддерживает: влево, вправо, вверх, вниз.
 * Интерфейс: I2C (адрес 0x39).
 * Потребление RAM: см. измерения сборки и -fstack-usage.
 */

#include <stdint.h>
#include <stdbool.h>

#include "apds9960_config.h"

/* Перечисление типов жестов */
typedef enum {
    GESTURE_NONE = 0,   /* Жест не определен */
    GESTURE_LEFT,       /* Движение влево */
    GESTURE_RIGHT,      /* Движение вправо */
    GESTURE_UP,         /* Движение вверх */
    GESTURE_DOWN        /* Движение вниз */
} gesture_t;

/* ============================================================================
 * НАСТРАИВАЕМЫЕ ПАРАМЕТРЫ
 *
 * Все дефолты и проверки диапазонов находятся в apds9960_config.h.
 * Переопределяйте их для всего проекта через PlatformIO build_flags (-D...),
 * чтобы одинаковые значения видел и код приложения, и apds9960.c.
 * ============================================================================ */

/* Отладочный вывод: раскомментируйте следующую строку или определите
 * APDS9960_DEBUG при сборке (-DAPDS9960_DEBUG) для включения UART-логов */
/* #define APDS9960_DEBUG */


/* Коды ошибок драйвера */
#define APDS_ERR_TIMEOUT        6
#define APDS_ERR_INVALID_ARG    7

#define APDS_ERR_NONE           0   /* Ошибок нет */
#define APDS_ERR_I2C            1   /* Ошибка I2C (NACK, таймаут) */
#define APDS_ERR_FIFO_OVERFLOW  2   /* Переполнение FIFO */
#define APDS_ERR_SENSOR_HANG    3   /* Датчик не отвечает */
#define APDS_ERR_INVALID_ID     4   /* Устройство по адресу 0x39 не APDS9960 */
#define APDS_ERR_UNSUPPORTED    5   /* Функция выключена на этапе компиляции (APDS_ENABLE_CALIBRATION=0) */

/* ============================================================================
 * ПУБЛИЧНЫЙ API
 * ============================================================================ */

/* Инициализация датчика APDS9960 через I2C.
 * Проверяет ID (0xAB, 0xA8, 0x9C, 0x9E).
 * Настраивает все регистры для работы в жестовом режиме.
 * Включает proximity + gesture + wait.
 * Возвращает: true если датчик найден, false если не отвечает. */
bool apds_init(void);

/* Перевод датчика в режим сна.
 * Отключает proximity, gesture и wait, оставляя только PON.
 * PON-only: около 38 мкА номинально, без тока модуля/подтяжек.
 * Возвращает: true если запись ENABLE успешна. */
bool apds_sleep(void);

/* Пробуждение датчика из режима сна.
 * Включает proximity + gesture + wait.
 * Работает корректно после apds_sleep() и apds_shutdown().
 * Возвращает: true если датчик отвечает. */
bool apds_wakeup(void);

/* Полное отключение датчика (максимальная экономия энергии).
 * Отключает PON + все остальные функции, включая ИК-диод.
 * Sleep ток: около 1 мкА типично по datasheet; для клона не измерен.
 * Для пробуждения необходимо apds_wakeup().
 * Возвращает: true если запись ENABLE успешна. */
bool apds_shutdown(void);

/* Проверка наличия данных жеста.
 * Читает регистр GSTATUS и проверяет бит GVALID.
 * Если переполнение FIFO (GFOV) — выполняет переинициализацию.
 * Возвращает: true если данные готовы к чтению. */
bool apds_available(void);

/* Чтение распознанного жеста (блокирующая функция).
 * Читает пакеты из FIFO пока GVALID=1,
 * накапливает соотношения U/D и L/R, определяет направление.
 *
 * Алгоритм:
 *   1. Фильтрует и считает действительные FIFO-пакеты
 *   2. Вычисляет Ratio: (U-D)*100/(U+D) и (L-R)*100/(L+R)
 *   3. Сравнивает средние Ratio первых двух и последних двух пакетов
 *   4. Определяет направление по доминирующей оси
 *
 * @note При сбое I2C частично накопленные данные НЕ декодируются:
 * возвращается GESTURE_NONE, а apds_getLastError() == APDS_ERR_I2C.
 *
 * При дедлайне возвращает NONE с APDS_ERR_TIMEOUT; частичные данные
 * отбрасываются, следующий вызов сначала восстанавливает FIFO.
 * Возвращает: gesture_t — тип жеста или GESTURE_NONE */
gesture_t apds_readGesture(void);

/* Set the clockwise rotation of the mounted sensor as seen from its optical
 * face. 0 is the APDS-9960 datasheet Figure 11 orientation (IR LED on the left,
 * pin 1 on the upper right). Valid values are 0, 90, 180 and 270 degrees.
 * The next apds_readGesture() returns directions in the observer's frame.
 * The setting defaults to 0 after MCU reset and survives sensor reinitialization.
 * An invalid value returns false with APDS_ERR_INVALID_ARG.
 */
bool apds_setRotation(uint16_t degrees);
uint16_t apds_getRotation(void);

/* Чтение текущего значения proximity-канала (0-255).
 * value должен быть non-NULL; NULL даёт APDS_ERR_INVALID_ARG.
 * Возвращает: true если чтение успешно; свежесть конверсии не гарантируется. */
bool apds_readProximity(uint8_t *value);

/* Чтение регистра STATUS (0x93) для диагностики.
 * value должен быть non-NULL; NULL даёт APDS_ERR_INVALID_ARG.
 * Возвращает: true если чтение успешно; свежесть конверсии не гарантируется. */
bool apds_readStatus(uint8_t *value);

/* Получение кода последней ошибки драйвера.
 * Возвращает: APDS_ERR_* (0 = нет ошибок). */
uint8_t apds_getLastError(void);

/* Получение сырого статуса последней НЕудачной I2C-транзакции
 * (коды драйвера I2C-CH32V003 v7.2.1 из i2c.h):
 *   0 = I2C_OK, 1 = I2C_NACK (нет ACK от датчика),
 *   2 = I2C_ERR_TIMEOUT (таймаут шины), 3 = I2C_ERR_CLK,
 *   4 = I2C_ERR_BERR (ошибка шины), 5 = I2C_ERR_ARLO (потеря арбитража),
 *   6 = I2C_ERR_RECOVERY (повторите i2c_init после устранения сбоя).
 * Обновляется только при сбоях; успех не сбрасывает предыдущий статус.
 * Интерпретируйте совместно с apds_getLastError() == APDS_ERR_I2C.
 * Возвращает: код i2c.h (см. выше). */
uint8_t apds_getLastI2CStatus(void);

/* Получение количества последовательных переинициализаций.
 * 0 = нормальная работа, >0 = возможны проблемы с датчиком. */
uint8_t apds_getReinitCount(void);

/* Повторная калибровка порогов proximity без полной переинициализации.
 * Полезно при изменении условий освещения или положения датчика.
 * Доступна только если APDS_ENABLE_CALIBRATION = 1; при 0 возвращает
 * false с apds_getLastError() == APDS_ERR_UNSUPPORTED.
 * true означает успешную запись выбранных порогов, включая fallback. */
bool apds_recalibrate(void);

/* Включение прерывания жеста (GIEN=1).
 * INT pin становится низким когда GVALID=1.
 * При сбое I2C фиксирует apds_getLastError() = APDS_ERR_I2C. */
void apds_enableInterrupt(void);

/* Отключение прерывания жеста (GIEN=0).
 * При сбое I2C фиксирует apds_getLastError() = APDS_ERR_I2C. */
void apds_disableInterrupt(void);

/* Compatibility status read: does not clear GINT or FIFO.
 * Normal completion drains FIFO; recovery uses GCONF4.GFIFO_CLR.
 * Non-reentrant: call all APDS APIs from one main context, never from ISR.
 * On read failure records APDS_ERR_I2C. */
void apds_clearInterrupt(void);

#endif /* APDS9960_H */
