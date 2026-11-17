# Драйвер жестов APDS-9960 для CH32V003

Версия **1.4.0** · [English](README.md) · [История изменений](CHANGELOG.md) · MIT

Одноэкземплярный C-драйвер четырёх направлений LEFT, RIGHT, UP и DOWN для CH32V003 и NoneOS-SDK. Только целочисленная арифметика, без динамической памяти. Аппаратный I2C1 используется через [I2C-CH32V003 v7.2.1](https://github.com/tama18101971/I2C-CH32V003/tree/v7.2.1). Arduino и RTOS не используются. RGB/ALS и отдельного proximity-only режима нет; доступны сырые чтения proximity и STATUS.

## Сборка и подключение

Нужны PlatformIO, Community-PIO-CH32V и соответствующий NoneOS-SDK/toolchain. Плата `ch32v003f4p6_evt_r0`, HSI+PLL 48 МГц, 16 КиБ программной Flash и 2 КиБ SRAM.

```sh
pio pkg install --platform https://github.com/Community-PIO-CH32V/platform-ch32v.git
pio run
pio run -t upload
pio device monitor --port COM6 --baud 115200
```

COM6 — порт испытательного стенда, в своей системе выберите фактический UART-порт. Прошивка загружается через WCH-Link, а не через порт UART-монитора.

| MCU | Датчик / назначение |
|---|---|
| PC1 | SDA |
| PC2 | SCL |
| PC3 | INT, open-drain, активный низкий уровень |
| PD5 | UART TX, 115200 бод |

Адрес датчика — 7-битный `0x39`. Для SDA/SCL нужны подтяжки и согласованные логические уровни. Требования к питанию и развязке LED определяются конкретным модулем и [datasheet APDS-9960](https://cdn.sparkfun.com/assets/learn_tutorials/3/2/1/Avago-APDS-9960-datasheet.pdf). Ток всей платы не измерялся.

## Подключение библиотеки к другому проекту

Подробности: [INTEGRATION_RU.md](INTEGRATION_RU.md). В целевом `platformio.ini` закрепите версию:

```ini
lib_deps =
    https://github.com/tama18101971/APDS-9960_CH32V003.git#v1.4.0
```

Ссылка станет доступна после публикации тега. Манифест автоматически подключает I2C v7.2.1. В пакет входят исходники `src/`; примеры, тесты, журналы, настройки редактора и CI исключены.

До `apds_init()` вызовите `SystemCoreClockUpdate()`, `Delay_Init()` и успешный `i2c_init(400000)`. До использования примера логгера и любых диагностических режимов настройте UART через `USART_Printf_Init(115200)`. Все API датчика вызываются из одного main-контекста, не из ISR; выходные указатели должны быть действительными и non-NULL.

Актуальное приложение: [examples/basic/main.c](examples/basic/main.c). В режиме INT оно ограниченно ожидает флаг/физический уровень INT и периодически опрашивает датчик для восстановления. `WFI` намеренно не используется; экономия энергии CPU не заявлена. Завершённый жест уже осушает свой FIFO — не отбрасывайте следующий жест безусловным дополнительным циклом чтения.

```c
/* Фрагмент уже инициализированного main loop, сборка -DAPDS_INT_MODE=0. */
if (apds_available()) {
    gesture_t gesture = apds_readGesture();
    uint8_t error = apds_getLastError(); /* до следующего вызова API */
    if (error == APDS_ERR_NONE && gesture != GESTURE_NONE) {
        /* Передать жест приложению. */
    }
} else {
    uint8_t error = apds_getLastError();
    /* Обработать ошибку или продолжить ожидание данных. */
    (void)error;
}
```

## Поддерживаемые ID и эмпирические профили

Принимаются все четыре ID: **0xAB, 0x9C, 0xA8, 0x9E**. Принятый ID не доказывает производителя или полную аппаратную совместимость.

| ID | Gesture gain / базовый LED drive | Номинальный импульсный ток при boost 300% | Проверка |
|---|---|---:|---|
| 0xAB, 0x9C | 8× / 100 мА |300 мА| Приём ID проверен программно; физических экземпляров в этих испытаниях не было |
| 0xA8 | 2× / 25 мА |75 мА| На проверенной плате получено по 5 результатов каждого направления в заданном порядке; за 60 с покоя ложных событий не было |
| 0x9E | 1× / 12,5 мА |37,5 мА| Первый экземпляр распознавал четыре направления; другой с тем же ID не прошёл проверку связи |

Это настройки по измерениям отдельных плат, а не другая карта регистров и не гарантия для всех клонов. Часть близких пиков у 0xA8 всё ещё насыщалась и отбрасывалась. Фактический ток электрически не измерялся. Proximity LED настраивается отдельно через `APDS_LED_CURRENT`: по умолчанию база 100 мА и boost 300%, то есть номинально 300 мА у всех ID.

`APDS_CLONE_A8_PROFILE=0` и `APDS_CLONE_9E_PROFILE=0` отключают соответствующие переопределения gain/drive. После этого действуют `APDS_GGAIN` и `APDS_GLDRIVE`. Boost влияет на оба режима LED. Неисправность модуля или удерживаемая низкой SDA не должны маскироваться новой веткой по ID. [Аппаратные результаты и ограничения](HARDWARE_TEST_RU.md).

## Публичный API

| Функция | Контракт |
|---|---|
| `apds_init()` | Проверка разрешённого ID, конфигурация engines/FIFO, калибровка при включённой опции. Успех включает успешную запись fallback-порогов. |
| `apds_available()` | GSTATUS, восстановление границы потока после обрыва, recovery overflow. `false` означает отсутствие данных либо ошибку — немедленно читайте last error. |
| `apds_readGesture()` | Блокирующее чтение `gesture_t`; I2C fault, overflow и deadline не декодируют частичный поток. |
| `apds_readProximity(uint8_t*)` | Сырой PDATA; свежая конверсия не гарантируется. |
| `apds_readStatus(uint8_t*)` | Сырой STATUS. |
| `apds_sleep()` | ENABLE=PON; около 38 мкА номинально для состояния сенсора, не измеренный ток модуля. |
| `apds_shutdown()` | ENABLE=0, oscillator/engines выключены; около 1 мкА типично по datasheet, без гарантии для клона. |
| `apds_wakeup()` | Включение сохранённой конфигурации после sleep/shutdown; следующий вызов данных выполняет resync FIFO. После физической потери питания нужен полный init. |
| `apds_recalibrate()` | Повторный сбор proximity и commit порогов/fallback без полной конфигурации; UNSUPPORTED при выключенной калибровке. |
| `apds_enableInterrupt()` / `apds_disableInterrupt()` | GIEN и сохранение политики для recovery. Snapshot RMW не атомарен относительно автономного GMODE — не переключать посреди жеста. |
| `apds_clearInterrupt()` | Только совместимый read GSTATUS, не clear GINT. Нормальное завершение очищается чтением FIFO, recovery — GCONF4.GFIFO_CLR. |
| `apds_getLastError()` | Последний результат драйвера; последующий успешный API может его заменить. Успешные void interrupt-вызовы сохраняют код. |
| `apds_getLastI2CStatus()` | Sticky status последней неудачной транзакции; успех его не стирает. |
| `apds_getReinitCount()` | Счётчик recovery overflow; нормальный available/init/wakeup его сбрасывает, прямой успешный readGesture — нет. |

Значения `gesture_t`: NONE=0, LEFT=1, RIGHT=2, UP=3, DOWN=4. Направления соответствуют неповёрнутому корпусу из datasheet; монтаж платы может их повернуть.

| Ошибка драйвера | Значение |
|---|---:|
| `APDS_ERR_NONE` |0|
| `APDS_ERR_I2C` |1|
| `APDS_ERR_FIFO_OVERFLOW` |2|
| `APDS_ERR_SENSOR_HANG` |3|
| `APDS_ERR_INVALID_ID` |4|
| `APDS_ERR_UNSUPPORTED` |5|
| `APDS_ERR_TIMEOUT` |6|
| `APDS_ERR_INVALID_ARG` |7|

Сырые I2C-коды — отдельное пространство: OK0, NACK1, TIMEOUT2, CLK3, BERR4, ARLO5, RECOVERY6. После RECOVERY сначала устраните физическую причину и добейтесь успешного `i2c_init`; пример делает это перед APDS reinit. Сырой код после overflow может относиться уже к транзакции восстановления.

## Алгоритм, калибровка и время

Пакет FIFO — U,D,L,R. За транзакцию читается до 8 пакетов (32 байта), вычисляются `(U-D)*100/(U+D)` и `(L-R)*100/(L+R)`. Сравниваются средние последних двух и первых двух valid ratios. Эффективный минимум — `max(4,APDS_FIFO_MIN_PACKETS)`. Побеждает доминирующая абсолютная ось, при равенстве — горизонтальная. Положительная delta даёт DOWN/RIGHT. Усреднение снижает вклад выброса, но не эквивалентно старой сумме приращений и не обеспечивает невосприимчивость к шуму.

Пакет отбрасывается, если любой канал выше saturation limit либо все ниже signal minimum. Возвратные/составные движения могут взаимно компенсироваться в endpoint-метрике. NEAR/FAR не поддержаны.

Калибровка фильтрует до 32 PDATA samples, сортирует, использует медиану и целочисленную оценку sigma, выбирает/ограничивает пороги. При недостатке валидных/свежих данных либо sampling fault применяются defaults. Запись порогов строгая, cache обновляется после успеха обеих записей. Качество зависит от оптики, фона и семантики конверсий клона: одинаковые чтения регистра не доказывают независимые измерения.

При `APDS_OWN_SYSTICK=1` readGesture использует свободный счётчик SysTick HCLK/8 и останавливает его при возврате. Deadline проверяется между блокирующими I2C-вызовами, поэтому wall time может превысить заданное на незавершённую транзакцию/recovery. Timeout даёт NONE с TIMEOUT и помечает FIFO для resync. OWN=0 использует бюджет итераций, а не точный дедлайн в миллисекундах.

**Одного OWN=0 недостаточно для сохранения SysTick приложения:** штатный SDK `Delay_Ms` его меняет. Для общей временной базы также задайте `APDS_DELAY_MS` именем связанной функции `void delay(uint32_t ms)`, ожидающей не менее заданного без изменения SysTick. Определение должно действовать во всех translation units.

## Конфигурация

Макросы задаются для всего проекта через `build_flags`. Define только в main.c не меняет отдельно компилируемую библиотеку. Диапазоны проверяются в [apds9960_config.h](src/apds9960_config.h).

```ini
build_flags =
    -DAPDS_INT_MODE=0
    -DAPDS_CLONE_A8_PROFILE=0
    -DAPDS_GGAIN=1
    -DAPDS_GLDRIVE=2
```

| Макрос | По умолчанию |
|---|---|
| `APDS_CLONE_A8_PROFILE` | `1` |
| `APDS_CLONE_9E_PROFILE` | `1` |
| `APDS_GAIN` | `3` |
| `APDS_LED_CURRENT` | `0` |
| `APDS_LED_BOOST` | `3` |
| `APDS_FIFO_MIN_PACKETS` | `4` |
| `APDS_PROX_THRESHOLD` | `50` |
| `APDS_GESTURE_EXIT_TH` | `30` |
| `APDS_GESTURE_TIMEOUT_MS` | `300` |
| `APDS_DELAY_MS` | `Delay_Ms` |
| `APDS_OWN_SYSTICK` | `1` |
| `APDS_GGAIN` | `3` |
| `APDS_GLDRIVE` | `0` |
| `APDS_GWTIME` | `1` |
| `APDS_FIFO_SIGNAL_MIN` | `10` |
| `APDS_FIFO_SATURATION_MAX` | `250` |
| `APDS_GESTURE_SENSITIVITY` | `5` |
| `APDS_RETRY_LIMIT` | `6` |
| `APDS_ENABLE_CALIBRATION` | `1` |
| `APDS_CAL_SAMPLES` | `32` |
| `APDS_CAL_SIGMA_COEFF` | `3` |
| `APDS_CAL_PROX_MIN` | `10` |
| `APDS_CAL_PROX_MAX` | `200` |
| `APDS_CAL_FILTER_MAX` | `200` |
| `APDS_INT_MODE` | `1` |
| `APDS_PROVIDE_EXTI_ISR` | `1` |

Gain коды proximity/gesture 0..3: 1×,2×,4×,8×. Drive 0..3: 100,50,25,12,5 мА. Boost 0..3: 100,150,200,300%. GWTIME 0..7: 0;2,8;5,6;8,4;14;22,4;30,8;39,2 мс. Entry и exit относятся к разным engines; поддержанная конфигурация сейчас требует exit<=entry.

## Прерывания и интеграция

[INTEGRATION_RU.md](INTEGRATION_RU.md) описывает общий вектор и параметры переноса пина/clock. `apds_exti_init` настраивает AFIO/GPIO clock, input pull-up, falling edge, EXTI и NVIC. Все EXTI0..7 делят один вектор. По умолчанию ISR strong; при `APDS_PROVIDE_EXTI_ISR=0` приложение обязано предоставить свой strong handler. Пример репозитория содержит такой минимальный handler. NVIC enable/disable действуют на весь вектор. Callback должен очищать pending чужих линий и быть коротким; I2C из ISR запрещён контрактом зависимости.

## Вывод и диагностика

`apds9960_log.c/h` — маленький UART-логгер только для `%d`, `%u`, `%X`, `%02X`, `%%`, без stdio и heap. `APDS9960_DEBUG` включает algorithm/calibration logs, требующие заранее настроенный UART. Диагностические флаги по умолчанию выключены.

- `APDS_RAW_DEBUG`: каждый восьмой dataset; влияет на время, только для коротких записей.
- `APDS_I2C_TRACE`: регистр/status неуспешных wrapper-вызовов; не аудит I2C internals.
- `APDS_HW_TEST`: краткие проверки configuration/FIFO/sleep/wakeup примера.
- `APDS_HEALTH_TEST`: отдельная диагностика ID, WTIME read/write, active polling на 400/100 кГц, уровней линий после recovery failure, затем остановка. Требует реальное устройство и меняет его конфигурацию.
- `APDS_EXAMPLE_I2C_HZ`: частота I2C примера, default 400000; не runtime-параметр библиотеки.

## Размеры и проверка

Измерено 17.11.2026: GCC12.2.0, `-Os`, ch32v1.1.0+499971a, SDK2.30000.0+4fcf972, I2C7.2.1. Другие toolchains могут дать иные размеры.

| Область | Flash / code, байт | RAM, байт |
|---|---:|---:|
| Полный default demo |7952|380, включая reserve stack256|
| APDS+EXTI objects до linker GC |3736 text +1 initialized data|30BSS +1 initialized data|
| Отдельный объект UART logger |436 text|0 static|

Сумма объектов не равна точному вкладу в ELF: неиспользуемые функции удаляются. До исправлений APDS с той же I2C7.2.1 весь demo занимал 12544Flash/520RAM. Ядро выросло из-за recovery/профилей, а отказ от stdio уменьшил всю прошивку. Кадр readGesture по компилятору — 96 байт; полный stack watermark с I2C/ISR не измерялся. Оценка «peak148 байт» не заявляется.

```sh
pio run
python test/host/run.py
```

Host runner требует GCC и заголовок закреплённой I2C, установленный PlatformIO; пути переопределяются `CC`/`--i2c-include`. Windows wrapper: `test/host/run.ps1`. Четыре suites проверяют математику, границы FIFO, faults, profiles и lifecycle, но не I2C implementation и реальную оптику. CI собирает default/polling/no-calibration/debug/custom-ISR/external-SysTick/generic profiles с `-Wextra -Werror` на проектных исходниках и контролирует default RAM<=600/Flash<=13000.

Есть результаты двух работающих клонов и одного неработающего модуля 0x9E. Ни принятый ID, ни 60 секунд покоя не заменяют длительной квалификации. Оригинальный 0xAB/альтернативный 0x9C и широкий диапазон света/расстояний физически не проверены. См. [HARDWARE_TEST_RU.md](HARDWARE_TEST_RU.md).

## Переход с 1.3.x

- Перепроверьте ориентацию: исправлены знаки направлений и mapping GCONF2.
- Профили по ID переопределяют generic gesture gain/drive; для ручного tuning отключите нужный профиль.
- Обрабатывайте TIMEOUT и INVALID_ARG вместе с прежними неизменными кодами0..5.
- При ручном копировании добавьте `apds9960_log.c/h` к остальным исходникам.
- Замените старые фрагменты WFI и post-gesture drain актуальным примером.
- Для shared SysTick нужны одновременно OWN=0 и совместимый delay hook.

## Лицензия

[MIT](LICENSE).
