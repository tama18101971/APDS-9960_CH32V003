# Подключение драйвера APDS9960 как библиотеки в другие проекты

🇬🇧 [English](INTEGRATION.md) | 🇷🇺 Русский

Этот документ описывает, как использовать драйвер жестов APDS9960 (файлы
`apds9960.*`, `apds9960_config.h`, `apds9960_regs.h`, `int_config.*`) в **других** PlatformIO-проектах
на CH32V003, без копирования `main.c` (он — только демо-пример этого репозитория).

Драйвер I2C подключается как внешняя библиотека `I2C-CH32V003` через `lib_deps`.

## 1. Какие файлы входят в библиотеку

| Файл | Обязателен? | Назначение |
|------|-------------|------------|
| `apds9960_regs.h` | ✅ Всегда | Карта регистров APDS9960 |
| `apds9960_config.h` | ✅ Всегда | Общая compile-time конфигурация драйвера |
| `apds9960.h` / `apds9960.c` | ✅ Всегда | Сам драйвер жестов |
| `int_config.h` / `int_config.c` | ⚙️ Только если `APDS_INT_MODE=1` | EXTI3 (PC3) для interrupt-режима |

Драйвер I2C (`i2c.h` / `i2c.c`) — **внешняя зависимость**
[`I2C-CH32V003`](https://github.com/tama18101971/I2C-CH32V003.git).
Подключается через `lib_deps` в `platformio.ini`, копировать в проект не нужно.

Если новый проект работает только в polling-режиме (`APDS_INT_MODE=0`),
`int_config.h`/`int_config.c` можно не копировать вовсе.

## 2. Требования к целевому проекту

Драйвер написан под конкретную платформу и НЕ является MCU-агностичным:

- PlatformIO платформа `ch32v`, framework `noneos-sdk` (НЕ Arduino, НЕ FreeRTOS).
- Реальный чип CH32V003 (или совместимый по регистрам I2C1/EXTI/SysTick).
- `platformio.ini` целевого проекта должен содержать как минимум:

```ini
[env:my_board]
platform = ch32v
board = ch32v003f4p6_evt_r0   ; или другая ваша плата на CH32V003
framework = noneos-sdk
lib_deps =
    https://github.com/tama18101971/I2C-CH32V003.git
```

Для Способа C добавьте также саму библиотеку (см. раздел 5.2).

- Свободные пины: **PC1 (SDA)**, **PC2 (SCL)**, и (только для interrupt-режима)
  **PC3 (INT)**. Если эти пины заняты другой периферией в целевом проекте —
  нужно либо освободить их, либо переопределить параметры пина прерывания
  (`APDS_INT_PORT`, `APDS_INT_PIN`, `APDS_INT_LINE`, `APDS_INT_PORT_SOURCE`,
  `APDS_INT_PIN_SOURCE`) через `build_flags` (см. раздел 8).
- Свободна шина I2C1 (или уже используется только устройствами, совместимыми
  по адресу — APDS9960 сидит на `0x39`).

## 3. Способ A — быстрое копирование (для разовой интеграции)

Самый простой вариант, без создания отдельного git-репозитория.

1. Создайте в целевом проекте папку `lib/APDS9960/` (PlatformIO автоматически
   подключает всё из `lib/*` к сборке, не смешивая с `src/`):

   ```
   your_project/
     lib/
       APDS9960/
         apds9960.h
         apds9960.c
         apds9960_config.h
         apds9960_regs.h
         int_config.h      (опционально)
         int_config.c      (опционально)
     src/
       main.c              (ваш собственный main() — НЕ из этого репозитория)
   ```

2. Скопируйте туда файлы из раздела 1 (без `main.c`).
3. В `platformio.ini` добавьте зависимость I2C:

   ```ini
   lib_deps =
       https://github.com/tama18101971/I2C-CH32V003.git
   ```

4. В своём `src/main.c` подключайте как обычно:

   ```c
   #include "i2c.h"
   #include "apds9960.h"
   #if APDS_INT_MODE == 1
   #include "int_config.h"
   #endif
   ```

5. `pio run` — PlatformIO подхватит `lib/APDS9960/` и `I2C-CH32V003` автоматически.

**Минус способа A:** при исправлении багов в этом репозитории изменения нужно
руками копировать в каждый проект, где лежит копия.

## 4. Способ B — общая папка на диске (несколько локальных проектов, один разработчик)

Если у вас несколько проектов на одной машине и не хочется дублировать файлы —
храните библиотеку в одном месте и подключайте её через `lib_extra_dirs`.

1. Разместите файлы драйвера (без `main.c`) в отдельной папке, например:
   `C:\Projects\shared_libs\APDS9960_CH32V003\` (плоско, либо в подпапке `src/` —
   PlatformIO ищет заголовки/исходники рекурсивно).

2. В `platformio.ini` **каждого** проекта, где нужен драйвер:

   ```ini
   [env:my_board]
   platform = ch32v
   board = ch32v003f4p6_evt_r0
   framework = noneos-sdk
   lib_extra_dirs = C:\Projects\shared_libs
   ```

3. `pio run` подключит `APDS9960_CH32V003` как обычную библиотеку из
   `lib_extra_dirs`.

**Плюс:** один источник правды на диске, правите в одном месте — подхватывается
везде при следующей сборке. **Минус:** работает только на этой машине, не
подходит для CI/других разработчиков без синхронизации папки.

## 5. Способ C — отдельный git-репозиторий + `lib_deps` (рекомендуется для нескольких проектов)

Самый правильный вариант, если драйвер используется в нескольких проектах и/или
несколькими людьми/машинами: выделить драйвер в собственный git-репозиторий
(или git submodule) и подключать через `lib_deps` — PlatformIO сам скачает и
закеширует библиотеку при сборке.

### 5.1. Подготовка репозитория библиотеки (сделать один раз)

Текущий репозиторий уже имеет готовую структуру для публикации как библиотеки:

```
APDS9960_CH32V003/            (корень git-репозитория библиотеки)
  library.json
  src/
    apds9960.h
    apds9960.c
    apds9960_config.h
    apds9960_regs.h
    int_config.h
    int_config.c
  examples/
    basic/
      main.c                   (демо-пример, НЕ компилируется как часть библиотеки)
```

Содержимое `library.json` — см. в корне репозитория. Он уже включает зависимость
`I2C-CH32V003` и исключает `examples/` из компиляции библиотеки.

Если вы создаёте **собственный** репозиторий библиотеки — скопируйте файлы из `src/`
и создайте аналогичный `library.json`.

### 5.2. Подключение в целевом проекте

```ini
[env:my_board]
platform = ch32v
board = ch32v003f4p6_evt_r0
framework = noneos-sdk
lib_deps =
    https://github.com/tama18101971/APDS-9960_CH32V003.git
```

Или с фиксацией версии (тег/коммит/ветка):

```ini
lib_deps =
    https://github.com/tama18101971/APDS-9960_CH32V003.git#v1.0.0
```

Если вы создаёте **собственный** форк/репозиторий библиотеки — замените путь
на свой: `<ваш_аккаунт>/APDS9960_CH32V003.git`.

`I2C-CH32V003` подтягивается **автоматически** — он объявлен в `dependencies`
файла `library.json`, поэтому указывать его в `lib_deps` не нужно.

`pio run` сам клонирует репозиторий в `.pio/libdeps/<env>/` при первой сборке.

**Плюс:** единый источник правды, версионирование, работает на любой машине и
в CI. **Минус:** требует первоначальной настройки (git-репозиторий + `library.json`).

## 6. Минимальный пример использования (после подключения любым из способов)

```c
#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"
#if APDS_INT_MODE == 1
#include "int_config.h"
#endif

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    i2c_init(400000);              /* Fast-mode I2C, датчик и I2C-CH32V003 это поддерживают */

    if (!apds_init()) {
        printf("APDS9960 not responding!\r\n");
        while (1) {}
    }

#if APDS_INT_MODE == 1
    apds_exti_init();
    apds_enableInterrupt();
#endif

    while (1) {
#if APDS_INT_MODE == 1
        while (g_apds_int_flag == 0) { __WFI(); }
        g_apds_int_flag = 0;
        apds_clearInterrupt();
#endif
        if (apds_available()) {
            gesture_t g = apds_readGesture();
            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break;
            }
        }
    }
}
```

Полный рабочий пример (оба режима, с примером ISR) — см. `examples/basic/main.c` в этом
репозитории.

## 7. Пользовательский обработчик прерываний (несколько EXTI-линий)

На CH32V003 все EXTI-линии 0-7 делят один вектор (`EXTI7_0_IRQHandler`).
Библиотека предоставляет два варианта обработки дополнительных EXTI-линий:

### Вариант А: Полная замена обработчика

По умолчанию библиотека определяет **strong** `EXTI7_0_IRQHandler`, поскольку
weak fallback NoneOS-SDK зациклен. Если приложение должно владеть всеми EXTI
линиями, задайте `-DAPDS_PROVIDE_EXTI_ISR=0` в `build_flags`, затем определите
свой обработчик. Используйте `apds_handle_exti()` для проверки APDS9960 и
`apds_clear_exti()` для сброса pending bit:

```c
#include "int_config.h"

void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
    /* Проверяем APDS9960 — ставит g_apds_int_flag, pending bit ещё активен */
    apds_handle_exti();

    /* Проверяем свои EXTI-линии пока pending bits ещё на месте */
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        /* ... обработка EXTI5 ... */
        EXTI_ClearITPendingBit(EXTI_Line5);
    }

    /* Сбрасываем pending bit APDS9960 последним */
    apds_clear_exti();
}
```

### Вариант Б: Переопределение слабого callback

Если нужно просто добавить логику после дефолтной обработки, переопределите
`apds_exti_callback()`. Дефолтный ISR сначала обработает APDS9960, затем
вызовет ваш callback:

```c
#include "int_config.h"

void apds_exti_callback(void) {
    /* Вызывается после дефолтной обработки APDS9960 */
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        /* ... обработка EXTI5 ... */
        EXTI_ClearITPendingBit(EXTI_Line5);
    }
}
```

### API функции EXTI

| Функция | Назначение |
|---------|------------|
| `apds_handle_exti()` | Проверяет `APDS_INT_LINE`, ставит `g_apds_int_flag`. НЕ сбрасывает pending bit. |
| `apds_clear_exti()` | Сбрасывает pending bit `APDS_INT_LINE`. |
| `apds_exti_callback()` | Слабый callback, вызывается из дефолтного `EXTI7_0_IRQHandler`. |

## 7. Подключение пинов (не меняется независимо от способа интеграции)

| CH32V003 | APDS9960 | Назначение |
|----------|----------|------------|
| PC1 | SDA | I2C данные |
| PC2 | SCL | I2C тактирование |
| PC3 | INT | Прерывание жеста (активный низкий уровень), только для `APDS_INT_MODE=1` |
| PD5 | — | UART TX (диагностика, опционально) |

I2C-адрес датчика: `0x39`.

## 8. Настройка под конкретный проект

Параметры драйвера задавайте для **всей сборки** через `build_flags` в
`platformio.ini` (например, `-DAPDS_INT_MODE=0`). `#define` в `main.c` перед
`#include "apds9960.h"` влияет лишь на `main.c`, но не на отдельно
скомпилированный `apds9960.c`, поэтому не поддерживается. Дефолты и проверки
диапазонов находятся в `apds9960_config.h`.

Параметры `int_config.h` (пин прерывания) также переопределяются через
`build_flags`:

```ini
build_flags =
    -DAPDS_INT_PORT=GPIOC
    -DAPDS_INT_PIN=GPIO_Pin_3
    -DAPDS_INT_LINE=EXTI_Line3
    -DAPDS_INT_PORT_SOURCE=GPIO_PortSourceGPIOC
    -DAPDS_INT_PIN_SOURCE=GPIO_PinSource3
```

```ini
build_flags =
    -DAPDS_GAIN=3
    -DAPDS_LED_CURRENT=0
    -DAPDS_GGAIN=3
    -DAPDS_GLDRIVE=0
    -DAPDS_PROX_THRESHOLD=50
    -DAPDS_GESTURE_EXIT_TH=30
    -DAPDS_GESTURE_TIMEOUT_MS=300
    -DAPDS_GWTIME=1
    -DAPDS_ENABLE_CALIBRATION=1
    -DAPDS_INT_MODE=1
    -DAPDS_GESTURE_SENSITIVITY=5
    ; -DAPDS9960_DEBUG
```

Полный список и допустимые диапазоны — в `apds9960_config.h`.

## 9. Бюджет ресурсов (учитывайте в новом проекте)

На чипе CH32V003 (RAM 2 КБ, Flash 16 КБ) сам драйвер занимает примерно:

- **RAM:** ~20 байт статики + до ~150 байт стека на пике (пакетное чтение FIFO)
- **Flash:** ~2.0 КБ (только `apds9960.c` + `int_config.c`, без I2C-библиотеки)

Если в целевом проекте уже используется значительная часть Flash/RAM другим
кодом — проверьте фактический бюджет через `pio run` (вывод "RAM:"/"Flash:").

## 10. Чек-лист совместимости перед переносом

- [ ] Целевой проект — `platform = ch32v`, `framework = noneos-sdk`
- [ ] PC1/PC2 (и PC3, если interrupt-режим) свободны
- [ ] I2C1 не занят другим устройством на адресе `0x39`
- [ ] Хватает Flash/RAM (см. раздел 9)
- [ ] В `platformio.ini` добавлена `lib_deps` с `I2C-CH32V003`

## 11. Типичные проблемы при переносе

| Симптом | Причина | Решение |
|---------|---------|---------|
| `multiple definition of 'EXTI7_0_IRQHandler'` | Ваш проект и `int_config.c` оба определяют strong ISR | Добавьте `-DAPDS_PROVIDE_EXTI_ISR=0` и в своём ISR вызовите `apds_handle_exti()` / `apds_clear_exti()` (см. раздел 7). |
| `apds_init()` возвращает `false` | Неверная разводка I2C, либо PC1/PC2 заняты другой периферией | Проверить пины, `APDS9960_DEBUG` для вывода ID |
| Жесты не распознаются | Слишком слабый сигнал / неверная калибровка | Увеличить `APDS_GAIN`/`APDS_GGAIN`, проверить освещение (не под прямым солнцем) |
| Жесты "двоятся" | Старая версия `apds9960.c` с искусственным обрывом `apds_readGesture()` по числу итераций | Убедиться, что скопирована актуальная версия драйвера из этого репозитория |
| Сборка не находит `ch32v00x.h`/`debug.h` | Не тот `framework`/`platform` в `platformio.ini` целевого проекта | См. раздел 2 |
| Сборка не находит `i2c.h` | Не добавлена `lib_deps` с `I2C-CH32V003` в `platformio.ini` | Добавить `https://github.com/tama18101971/I2C-CH32V003.git` в `lib_deps` |
| Конфликт `main()` при подключении через `lib_deps` на весь репозиторий | В библиотеку случайно попал `main.c` | Не копировать/не включать `main.c` в библиотечный репозиторий (см. раздел 1 и 5.1) |

## 12. Обратная синхронизация исправлений

Если после переноса в другой проект вы найдёте баг или улучшите алгоритм —
переносите исправление обратно в этот репозиторий (источник истины), а затем
обновляйте копии/зависимости в других проектах тем же способом, каким они были
подключены (Способ A — вручную скопировать заново, Способ B — правка одной
папки на диске подхватится сама, Способ C — обновить тег/коммит в `lib_deps`).
