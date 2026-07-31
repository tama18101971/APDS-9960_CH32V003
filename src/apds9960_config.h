/*
 * apds9960_config.h — единая конфигурация драйвера APDS9960.
 *
 * Этот файл включается и приложением, и apds9960.c. Переопределяйте значения
 * через build_flags PlatformIO, например:
 *   build_flags = -Isrc -DAPDS_GGAIN=2 -DAPDS_GESTURE_SENSITIVITY=8
 *
 * #define в main.c перед #include "apds9960.h" не меняет уже отдельно
 * скомпилированный apds9960.c и потому не является поддерживаемым способом
 * настройки драйвера.
 */

#ifndef APDS9960_CONFIG_H
#define APDS9960_CONFIG_H

/* Усиление proximity-канала CONTROL.PGAIN: 0=1x, 1=2x, 2=4x, 3=8x. */
#ifndef APDS_GAIN
#define APDS_GAIN                       3
#endif

/* Ток LED proximity/ALS CONTROL.LEDDRIVE: 0=100mA, 1=50mA, 2=25mA, 3=12.5mA. */
#ifndef APDS_LED_CURRENT
#define APDS_LED_CURRENT                0
#endif

/* Минимум валидных FIFO-пакетов для распознавания. */
#ifndef APDS_FIFO_MIN_PACKETS
#define APDS_FIFO_MIN_PACKETS           4
#endif

/* Пороги входа/выхода gesture engine. */
#ifndef APDS_PROX_THRESHOLD
#define APDS_PROX_THRESHOLD             50
#endif
#ifndef APDS_GESTURE_EXIT_TH
#define APDS_GESTURE_EXIT_TH            30
#endif

/* Максимальное время блокирующего чтения одного жеста. */
#ifndef APDS_GESTURE_TIMEOUT_MS
#define APDS_GESTURE_TIMEOUT_MS         300
#endif

/* Усиление и ток LED gesture engine: 0=максимум, 3=минимум для drive. */
#ifndef APDS_GGAIN
#define APDS_GGAIN                      3
#endif
#ifndef APDS_GLDRIVE
#define APDS_GLDRIVE                    0
#endif

/* Пауза gesture engine между измерениями: 0=0ms ... 7=39.2ms. */
#ifndef APDS_GWTIME
#define APDS_GWTIME                     1
#endif

/* Параметры программной фильтрации FIFO. */
#ifndef APDS_FIFO_SIGNAL_MIN
#define APDS_FIFO_SIGNAL_MIN            10
#endif
#ifndef APDS_FIFO_SATURATION_MAX
#define APDS_FIFO_SATURATION_MAX        250
#endif
#ifndef APDS_GESTURE_SENSITIVITY
#define APDS_GESTURE_SENSITIVITY        5
#endif

/* Повторная инициализация после ошибок/переполнения FIFO. */
#ifndef APDS_RETRY_LIMIT
#define APDS_RETRY_LIMIT                6
#endif

/* Автоматическая калибровка proximity-порогов. */
#ifndef APDS_ENABLE_CALIBRATION
#define APDS_ENABLE_CALIBRATION         1
#endif
#ifndef APDS_CAL_SAMPLES
#define APDS_CAL_SAMPLES                32
#endif
#ifndef APDS_CAL_SIGMA_COEFF
#define APDS_CAL_SIGMA_COEFF            3
#endif
#ifndef APDS_CAL_PROX_MIN
#define APDS_CAL_PROX_MIN               10
#endif
#ifndef APDS_CAL_PROX_MAX
#define APDS_CAL_PROX_MAX               200
#endif
#ifndef APDS_CAL_FILTER_MAX
#define APDS_CAL_FILTER_MAX             200
#endif

/* 0 = polling, 1 = INT/GVALID interrupt. */
#ifndef APDS_INT_MODE
#define APDS_INT_MODE                   1
#endif

/* 1 = int_config.c exports the default strong EXTI7_0_IRQHandler. */
#ifndef APDS_PROVIDE_EXTI_ISR
#define APDS_PROVIDE_EXTI_ISR           1
#endif

/* Проверки диапазонов: ошибочная конфигурация останавливает сборку. */
#if (APDS_GAIN < 0) || (APDS_GAIN > 3)
#error "APDS_GAIN must be in range 0..3"
#endif
#if (APDS_LED_CURRENT < 0) || (APDS_LED_CURRENT > 3)
#error "APDS_LED_CURRENT must be in range 0..3"
#endif
#if (APDS_GGAIN < 0) || (APDS_GGAIN > 3)
#error "APDS_GGAIN must be in range 0..3"
#endif
#if (APDS_GLDRIVE < 0) || (APDS_GLDRIVE > 3)
#error "APDS_GLDRIVE must be in range 0..3"
#endif
#if (APDS_GWTIME < 0) || (APDS_GWTIME > 7)
#error "APDS_GWTIME must be in range 0..7"
#endif
#if (APDS_FIFO_MIN_PACKETS < 2) || (APDS_FIFO_MIN_PACKETS > 32)
#error "APDS_FIFO_MIN_PACKETS must be in range 2..32"
#endif
#if (APDS_PROX_THRESHOLD < 0) || (APDS_PROX_THRESHOLD > 255)
#error "APDS_PROX_THRESHOLD must be in range 0..255"
#endif
#if (APDS_GESTURE_EXIT_TH < 0) || (APDS_GESTURE_EXIT_TH > APDS_PROX_THRESHOLD)
#error "APDS_GESTURE_EXIT_TH must be in range 0..APDS_PROX_THRESHOLD"
#endif
#if (APDS_GESTURE_TIMEOUT_MS < 1)
#error "APDS_GESTURE_TIMEOUT_MS must be greater than zero"
#endif
#if (APDS_FIFO_SIGNAL_MIN < 0) || (APDS_FIFO_SIGNAL_MIN > 255)
#error "APDS_FIFO_SIGNAL_MIN must be in range 0..255"
#endif
#if (APDS_FIFO_SATURATION_MAX < 1) || (APDS_FIFO_SATURATION_MAX > 255)
#error "APDS_FIFO_SATURATION_MAX must be in range 1..255"
#endif
#if (APDS_GESTURE_SENSITIVITY < 1) || (APDS_GESTURE_SENSITIVITY > 200)
#error "APDS_GESTURE_SENSITIVITY must be in range 1..200"
#endif
#if (APDS_RETRY_LIMIT < 1) || (APDS_RETRY_LIMIT > 255)
#error "APDS_RETRY_LIMIT must be in range 1..255"
#endif
#if (APDS_ENABLE_CALIBRATION != 0) && (APDS_ENABLE_CALIBRATION != 1)
#error "APDS_ENABLE_CALIBRATION must be 0 or 1"
#endif
#if (APDS_CAL_SAMPLES < 4) || (APDS_CAL_SAMPLES > 32)
#error "APDS_CAL_SAMPLES must be in range 4..32"
#endif
#if (APDS_CAL_SIGMA_COEFF < 1) || (APDS_CAL_SIGMA_COEFF > 10)
#error "APDS_CAL_SIGMA_COEFF must be in range 1..10"
#endif
#if (APDS_CAL_PROX_MIN < 0) || (APDS_CAL_PROX_MIN > APDS_CAL_PROX_MAX) || (APDS_CAL_PROX_MAX > 255)
#error "APDS_CAL_PROX_MIN/MAX must satisfy 0 <= min <= max <= 255"
#endif
#if (APDS_CAL_FILTER_MAX < 0) || (APDS_CAL_FILTER_MAX > 255)
#error "APDS_CAL_FILTER_MAX must be in range 0..255"
#endif
#if (APDS_INT_MODE != 0) && (APDS_INT_MODE != 1)
#error "APDS_INT_MODE must be 0 or 1"
#endif
#if (APDS_PROVIDE_EXTI_ISR != 0) && (APDS_PROVIDE_EXTI_ISR != 1)
#error "APDS_PROVIDE_EXTI_ISR must be 0 or 1"
#endif

#endif /* APDS9960_CONFIG_H */