# Драйвер жестов APDS9960 для CH32V003

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

🇬🇧 [English](README.md) | 🇷🇺 Русский

Компактный драйвер распознавания жестов для датчика приближения/освещённости/цвета APDS9960,
работающий с микроконтроллером CH32V003. Определяет четыре направления жестов:
**Влево**, **Вправо**, **Вверх**, **Вниз**.

## Возможности

- Минимальное использование RAM (~21 байт статики + ~150 байт стека на пике чтения FIFO)
- Минимальный размер Flash (~3.3 КБ только драйвер, ~11.4 КБ полный демо-проект)
- Без динамического выделения памяти (`malloc`/`free`)
- Без плавающей точки — только целочисленная арифметика
- Настраиваемые параметры для всей сборки через PlatformIO `build_flags`
- Автокалибровка порогов приближения
- Автовосстановление при переполнении FIFO
- Режим прерываний (APDS9960 INT → EXTI3 на PC3)
- Реальный дедлайн SysTick для чтения жестов (без искусственного cooldown)
- Проверка ID с поддержкой клонов (оригинал + китайские клоны)
- Диагностический API (коды ошибок, счётчик reinit, сырой статус шины I2C)
- Платформо-независимый C-код (без HAL, без Arduino)

## Аппаратная часть

### Поддерживаемые компоненты

| Компонент | Модель |
|-----------|--------|
| Микроконтроллер | CH32V003 (WCH) |
| Датчик | APDS9960 (включая клоны) |
| Интерфейс | I2C (100 кГц или 400 кГц Fast-mode) |

### Подключение пинов

| CH32V003 | APDS9960 | Функция |
|----------|----------|---------|
| PC1 | SDA | I2C данные |
| PC2 | SCL | I2C тактирование |
| PC3 | INT | Прерывание жеста (активный низкий, по спадающему фронту) |
| PD5 | — | UART TX (отладочный вывод) |

I2C-адрес датчика: `0x39`

## Структура файлов

```
src/
  apds9960.h         — Публичный API
  apds9960_config.h  — Единые параметры драйвера и проверки диапазонов
  apds9960.c         — Реализация драйвера
  apds9960_regs.h    — Карта регистров и битовые определения
  int_config.h / int_config.c — Настройка EXTI-прерывания (PC3)
examples/
  basic/
    main.c           — Пример использования (polling + interrupt режимы)
library.json         — Манифест библиотеки PlatformIO
platformio.ini       — Конфигурация сборки (build_src_filter для компиляции src/ + examples/basic/)
```

## Сборка

Требуется [PlatformIO](https://platformio.org/) с платформой `ch32v`.

```bash
pio run            # сборка
pio run -t upload  # прошивка через WCH-Link
```

## Использование драйвера в других проектах

Смотрите [`INTEGRATION_RU.md`](INTEGRATION_RU.md) для пошаговой инструкции по
повторному использованию этого драйвера (`apds9960.*`, `apds9960_regs.h`, `int_config.*`)
в других CH32V003/PlatformIO проектах — быстрое копирование, общая локальная папка
библиотеки или выделенная git-библиотека PlatformIO. Драйвер I2C
подключается как внешняя зависимость (`I2C-CH32V003`) через `lib_deps`.

## API

```c
#include "apds9960.h"

// Инициализация датчика (возвращает false, если не найден)
bool apds_init(void);

// Перевести датчик в спящий режим (~1 мкА, PON остаётся включённым)
bool apds_sleep(void);

// Полное отключение питания (<1 мкА, ИК-LED выключен)
bool apds_shutdown(void);

// Пробудить датчик из спящего режима или отключения
bool apds_wakeup(void);

// Проверить, готовы ли данные жеста
bool apds_available(void);

// Прочитать распознанный жест (блокирующий, ~10-60 мс)
gesture_t apds_readGesture(void);

// Прочитать значение приближения (0-255)
bool apds_readProximity(uint8_t *value);

// Прочитать регистр STATUS для диагностики
bool apds_readStatus(uint8_t *value);

// Получить последний код ошибки (APDS_ERR_*)
uint8_t apds_getLastError(void);

// Получить сырой статус последней НЕудачной I2C-транзакции (коды I2C-CH32V003 v7.0.x)
uint8_t apds_getLastI2CStatus(void);

// Получить счётчик reinit (0 = норма, >0 = проблемы)
uint8_t apds_getReinitCount(void);

// Перекалибровать пороги
bool apds_recalibrate(void);

// Включить прерывание жеста (GIEN=1, INT пин активный низкий)
void apds_enableInterrupt(void);

// Выключить прерывание жеста (GIEN=0)
void apds_disableInterrupt(void);

// Очистить прерывание: чтение GSTATUS → пин INT поднимается
void apds_clearInterrupt(void);
```

### Диагностика ошибок I2C

Когда `apds_getLastError()` возвращает `APDS_ERR_I2C`, функция
`apds_getLastI2CStatus()` сообщает сырой код уровня шины от драйвера
[I2C-CH32V003 v7.0.x](https://github.com/tama18101971/I2C-CH32V003):

| Значение | Символ | Описание |
|----------|--------|----------|
| 0 | `I2C_OK` | Отказов пока не зафиксировано |
| 1 | `I2C_NACK` | Нет ACK от датчика (монтаж / неверный адрес) |
| 2 | `I2C_ERR_TIMEOUT` | Таймаут шины (clock stretch / зависание) |
| 3 | `I2C_ERR_CLK` | Некорректная тактовая частота |
| 4 | `I2C_ERR_BERR` | Ошибка шины (сбой START/STOP, автоматически восстанавливается I2C-драйвером) |
| 5 | `I2C_ERR_ARLO` | Потеря арбитража |

Особенности поведения:

- Сырой статус записывается **только при сбоях** и не сбрасывается последующей
  успешной транзакцией.
- `apds_readGesture()` при сбое I2C в середине жеста немедленно возвращает
  `GESTURE_NONE` — частично накопленные данные не декодируются. Проверяйте
  `apds_getLastError()` после вызова.
- Interrupt-API (`apds_enableInterrupt()` / `apds_disableInterrupt()` /
  `apds_clearInterrupt()`) фиксирует `APDS_ERR_I2C` при сбое; при успехе
  предыдущий код ошибки не перезаписывается.
- Преходящие сбои I2C во время сбора выборок калибровки не приводят к отказу
  `apds_init()`: сбор прерывается досрочно, и при недостатке валидных замеров
  используются дефолтные пороги. Записи порогов остаются строгими.

### API EXTI-прерываний (`int_config.h`)

```c
#include "int_config.h"

// Инициализация EXTI3 + NVIC для прерывания APDS9960 на PC3
void apds_exti_init(void);

// Включение/выключение EXTI7_0 в NVIC
void apds_exti_enable(void);
void apds_exti_disable(void);

// Проверка APDS_INT_LINE и установка g_apds_int_flag (НЕ сбрасывает pending bit)
void apds_handle_exti(void);

// Сброс pending bit EXTI_Line3
void apds_clear_exti(void);

// Слабый callback — вызывается из дефолтного EXTI7_0_IRQHandler, переопределите для обработки своих EXTI
void apds_exti_callback(void);
```

### Пользовательский обработчик прерываний

На CH32V003 все EXTI-линии 0-7 делят один вектор прерывания (`EXTI7_0_IRQHandler`).
По умолчанию библиотека предоставляет **strong** обработчик, чтобы он вытеснил
зацикленный fallback обработчик NoneOS-SDK. Для добавления логики после обработки
APDS9960 безопаснее переопределить `apds_exti_callback()` ниже.

Если приложению необходим полный контроль над общим вектором EXTI0…7, задайте
`-DAPDS_PROVIDE_EXTI_ISR=0` в `build_flags` и определите обработчик сами:

```c
#include "int_config.h"

void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
    // Проверяем прерывание APDS9960 (ставит g_apds_int_flag, pending bit ещё НЕ сброшен)
    apds_handle_exti();

    // Проверяем свои EXTI-линии — pending bits ещё на месте
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        // ... обработка EXTI5 ...
        EXTI_ClearITPendingBit(EXTI_Line5);
    }

    // Сбрасываем pending bit APDS9960 последним
    apds_clear_exti();
}
```

Если нужно лишь добавить логику после дефолтной обработки, переопределите слабый callback:

```c
#include "int_config.h"

void apds_exti_callback(void) {
    // Вызывается после дефолтной обработки APDS9960 в EXTI7_0_IRQHandler
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        // ... обработка EXTI5 ...
        EXTI_ClearITPendingBit(EXTI_Line5);
    }
}
```

### Типы жестов

```c
typedef enum {
    GESTURE_NONE = 0,   // Жест не обнаружен
    GESTURE_LEFT,       // Свайп влево
    GESTURE_RIGHT,      // Свайп вправо
    GESTURE_UP,         // Свайп вверх
    GESTURE_DOWN        // Свайп вниз
} gesture_t;
```

### Коды ошибок

```c
#define APDS_ERR_NONE           0   // Нет ошибки
#define APDS_ERR_I2C            1   // Ошибка I2C — см. apds_getLastI2CStatus()
#define APDS_ERR_FIFO_OVERFLOW  2   // Переполнение FIFO
#define APDS_ERR_SENSOR_HANG    3   // Датчик не отвечает
#define APDS_ERR_INVALID_ID     4   // Устройство по адресу 0x39 не APDS9960
```

## Пример использования

### Режим прерываний (рекомендуется)

```c
#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"
#include "int_config.h"

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    uint8_t i2c_st = i2c_init(400000);
    if (i2c_st != I2C_OK) {
        printf("ERROR: i2c_init failed (%d)\r\n", i2c_st);
        while (1) {}
    }

    if (!apds_init()) {
        printf("Sensor not found! err=%d i2c=%d\r\n",
               apds_getLastError(), apds_getLastI2CStatus());
        while (1) {}
    }

    // INT → PC3 (EXTI3, по спадающему фронту)
    apds_exti_init();
    apds_enableInterrupt();

    while (1) {
        // CPU спит пока ISR не установит флаг
        while (g_apds_int_flag == 0) { __WFI(); }
        g_apds_int_flag = 0;

        apds_clearInterrupt();

        if (apds_available()) {
            gesture_t g = apds_readGesture();
            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break;
            }

            // Искусственный cooldown не нужен: apds_readGesture() уже блокируется
            // на реальном дедлайне SysTick до фактического сброса GVALID, поэтому
            // хвост одного физического свайпа никогда не декодируется как второй
            // жест. Эта очистка — страховка и обычно является no-op.
            while (apds_available()) apds_readGesture();
        }
    }
}
```

### Режим опроса (polling)

```c
#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    uint8_t i2c_st = i2c_init(400000);
    if (i2c_st != I2C_OK) {
        printf("ERROR: i2c_init failed (%d)\r\n", i2c_st);
        while (1) {}
    }

    if (!apds_init()) {
        printf("Sensor not found! err=%d i2c=%d\r\n",
               apds_getLastError(), apds_getLastI2CStatus());
        while (1) {}
    }

    while (1) {
        if (apds_available()) {
            gesture_t g = apds_readGesture();
            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break;
            }

            if (g != GESTURE_NONE) {
                while (apds_available()) apds_readGesture();
            }
        }
    }
}
```

## Алгоритм

### Обзор

APDS9960 содержит четыре фотодиода (Up, Down, Left, Right) и ИК-LED. Когда объект
движется над датчиком, показания фотодиодов изменяются. Драйвер считывает эти
значения из аппаратного буфера FIFO и вычисляет направление жеста.

### Поток данных

```
ИК-LED отражается от руки
        |
        v
Фотодиоды APDS9960 (U, D, L, R)
        |
        v
Аппаратный FIFO (4 байта на пакет: [U, D, L, R])
        |
        v
Драйвер считывает FIFO через I2C
        |
        v
Вычисление соотношений: (U-D)*100/(U+D), (L-R)*100/(L+R)
        |
        v
Накопление изменений между последовательными пакетами
        |
        v
Определение направления по накопленным дельтам
```

### Вычисление соотношений

Для каждого пакета FIFO вычисляются два соотношения с использованием целочисленной арифметики:

```
UD_ratio = (U - D) * 100 / (U + D)    диапазон: -100 до +100
LR_ratio = (L - R) * 100 / (L + R)    диапазон: -100 до +100
```

- **Положительный UD_ratio**: объект ближе к верхнему фотодиоду
- **Отрицательный UD_ratio**: объект ближе к нижнему фотодиоду
- **Положительный LR_ratio**: объект ближе к левому фотодиоду
- **Отрицательный LR_ratio**: объект ближе к правому фотодиоду

### Накопление

Между последовательными валидными пакетами накапливается изменение соотношения:

```
ud_acc += UD_ratio[текущий] - UD_ratio[предыдущий]
lr_acc += LR_ratio[текущий] - LR_ratio[предыдущий]
```

Этот подход накопления работает как для быстрых, так и для медленных жестов — даже небольшие
изменения на пакет суммируются со временем.

### Определение направления

После обработки всех данных FIFO (GVALID опускается):

1. Проверяется минимальное количество пакетов (>= 4) для фильтрации шума
2. Сравниваются абсолютные накопленные значения: `|ud_acc|` vs `|lr_acc|`
3. Доминирующая ось определяет жест:
   - `|ud_acc| > |lr_acc|` → вертикальный жест (UP или DOWN)
   - `|lr_acc| > |ud_acc|` → горизонтальный жест (LEFT или RIGHT)
4. Знак определяет направление внутри оси

### Фильтрация

- **Фильтр насыщения**: пакеты с любым каналом > 250 отбрасываются
- **Фильтр шума**: пакеты со всеми каналами < 10 отбрасываются
- **Минимум пакетов**: жест требует >= 4 валидных пакетов

## Калибровка

Драйвер выполняет автоматическую калибровку порогов приближения в `apds_init()`. Это обеспечивает
оптимальное распознавание жестов независимо от условий окружающей среды.

### Как это работает

1. Берётся 32 выборки PDATA в режиме жеста
2. Фильтруются насыщенные значения (>200)
3. Вычисляются медиана и стандартное отклонение
4. Устанавливаются пороги GPENTH (вход) и GEXTH (выход)
5. Пороги сохраняются для использования `sensor_reinit()`

Если сбой I2C прерывает фазу сбора, сбор прекращается досрочно, и когда остаётся
меньше половины валидных выборок — используются дефолтные пороги, а
`apds_init()` всё равно завершается успешно (датчик к этому моменту уже
сконфигурирован).

### Параметры калибровки

```c
#define APDS_ENABLE_CALIBRATION     1   // Включение/выключение калибровки
#define APDS_CAL_SAMPLES            32  // Количество выборок
#define APDS_CAL_SIGMA_COEFF        3   // Коэффициент сигма (2-5)
#define APDS_CAL_PROX_MIN           10  // Минимальный порог
#define APDS_CAL_PROX_MAX           200 // Максимальный порог
#define APDS_CAL_FILTER_MAX         200 // Порог фильтра выбросов
```

### Пороги

| Регистр | Назначение | Режим жеста | Режим приближения |
|---------|------------|-------------|-------------------|
| GPENTH | Порог входа | median/4 | median + 3*sigma |
| GEXTH | Порог выхода | GPENTH * 0.6 | GPENTH * 0.6 |
| PIHT | Порог прерывания | Как GPENTH | Как GPENTH |

## Конфигурация

Значения по умолчанию задаёт `src/apds9960_config.h`. Для настройки проекта
переопределите их через `build_flags`:

```ini
build_flags =
    -Isrc
    -DAPDS_GAIN=3
    -DAPDS_LED_CURRENT=0
    -DAPDS_GGAIN=3
    -DAPDS_GLDRIVE=0
    -DAPDS_PROX_THRESHOLD=50
    -DAPDS_GESTURE_EXIT_TH=30
    -DAPDS_GWTIME=1
    -DAPDS_GESTURE_TIMEOUT_MS=300
    -DAPDS_INT_MODE=1
```

> Важно: драйвер собирается отдельным модулем. Поэтому определение макросов
> непосредственно в `main.c` перед `#include "apds9960.h"` **не меняет**
> настройки в `apds9960.c`. Используйте `build_flags` в `platformio.ini`
> (либо изменяйте дефолты в `src/apds9960_config.h`):
>
> ```ini
> build_flags =
>     -Isrc
>     -DAPDS_INT_MODE=0
>     -DAPDS_GESTURE_SENSITIVITY=8
>     -DAPDS_FIFO_MIN_PACKETS=5
> ```
>
> `apds9960_config.h` проверяет диапазоны параметров на этапе компиляции.
> Дополнительно доступны `APDS_FIFO_SIGNAL_MIN`,
> `APDS_FIFO_SATURATION_MAX`, `APDS_GESTURE_SENSITIVITY` и
> `APDS_RETRY_LIMIT`.

### Советы по настройке

| Проблема | Решение |
|----------|---------|
| Жесты не распознаются | Увеличить `APDS_GAIN` и `APDS_GGAIN` до 3 (8x) |
| Ложные срабатывания | Увеличить порог приближения или чувствительность жестов |
| Медленные жесты не работают | Уменьшить `APDS_GESTURE_SENSITIVITY` через `build_flags` (по умолчанию: 5) |
| Несколько жестов на один свайп | Больше не должно происходить — `apds_readGesture()` блокируется на реальном дедлайне SysTick до сброса GVALID, а не по фиксированному числу итераций. Если всё ещё происходит — проверьте целостность сигнала I2C на 400 кГц. |
| Калибровка не удаётся | Проверьте ориентацию датчика, убедитесь что нет прямого солнечного света |

### Энергопотребление

| Функция | ENABLE | Потребление | Назначение |
|---------|--------|-------------|------------|
| `apds_init()` | PON+PEN+GEN+WEN | Полная активность | Нормальная работа |
| `apds_sleep()` | Только PON | ~1 мкА | Быстрое пробуждение, осциллятор работает |
| `apds_shutdown()` | 0x00 | <1 мкА | Максимальная экономия, ИК-LED выключен |

После `apds_shutdown()` вызовите `apds_wakeup()` для восстановления полной работы.
Корректно обрабатывает как пробуждение из спящего режима, так и из отключения.

Включение отладочного вывода — раскомментируйте в `apds9960.h`:

```c
#define APDS9960_DEBUG
```

Или определите при сборке:

```ini
build_flags = -DAPDS9960_DEBUG
```

Пример вывода:

```
APDS9960: ID=0x9E
CAL: median=191 valid=32 sigma=0 gpenth=47 gexth=28
Gesture: LEFT
```

## Потребление ресурсов

| Ресурс | Только драйвер | Полный демо-проект | Лимит |
|--------|----------------|--------------------|-------|
| RAM | ~21 байт статики | ~504 байт | 2048 байт |
| Flash | ~3.3 КБ | ~11.4 КБ | 16384 байт |

Показатели только драйвера — размеры `.text` объектов `apds9960.o` +
`int_config.o` (замерены утилитой `size` тулчейна). Они не включают
I2C-библиотеку (`I2C-CH32V003`), `main.c`, код запуска и библиотеки фреймворка.
Полный проект включает всё вышеперечисленное плюс отладочный вывод через
`printf`.

## Ограничения

- `apds_readGesture()` блокируется на время жеста (до `APDS_GESTURE_TIMEOUT_MS`, по умолчанию 300 мс)
- Нет API для RGB, ALS или режима только приближения (только жесты)
- Калибровка работает лучше всего в режиме жестов (режим приближения насыщается)

## Лицензия

[MIT](LICENSE)
