# Подключение APDS-9960 1.4.0

[English](INTEGRATION.md) · [API и конфигурация](README_RU.md)

## Зависимость PlatformIO

Используйте CH32V003 и NoneOS-SDK. При необходимости установите Community-PIO-CH32V; Arduino и STM32 HAL не являются совместимой заменой. Минимальная конфигурация:

```ini
[env:ch32v003f4p6_evt_r0]
platform = ch32v
board = ch32v003f4p6_evt_r0
framework = noneos-sdk
lib_deps =
    https://github.com/tama18101971/APDS-9960_CH32V003.git#v1.4.0
build_flags = -DAPDS_INT_MODE=0
```

Тег v1.4.0 должен быть опубликован до установки по этой ссылке. I2C-CH32V003 v7.2.1 автоматически подключается из library.json. Оставьте одну копию APDS и одну версию I2C; main примера не должен компилироваться вместе с main приложения. Startup и системная конфигурация остаются частью целевого проекта.

## Ручное копирование / локальная библиотека

Скопируйте весь каталог `src/` в `lib/APDS9960/src/` либо подключите его как локальную библиотеку PlatformIO. Нужны:

- `apds9960.c/h`, `apds9960_config.h`, `apds9960_regs.h`;
- `apds9960_log.c/h` для штатного логгера и debug;
- `int_config.c/h`, если используются EXTI helpers (для чистого polling необязательны).

При копировании без library.json явно укажите `https://github.com/tama18101971/I2C-CH32V003.git#v7.2.1` в lib_deps. Логгер использует UART1; до вывода нужна настройка UART. При выключенной диагностике и отсутствии вызовов логгера linker GC удаляет его неиспользуемый код. Замена на обычный stdio без контроля буферизации может вернуть heap и существенно увеличить Flash.

## Инициализация и обслуживание

Подключите `<ch32v00x.h>`, `<debug.h>`, `"i2c.h"`, `"apds9960.h"`; для EXTI добавьте `"int_config.h"`. Порядок инициализации:

```c
bool app_sensor_init(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200); /* Required by example/debug logging. */
    if (i2c_init(400000) != I2C_OK) return false;
    return apds_init();
}
```

Вызывающий должен обработать false и не продолжать работу с датчиком после неуспешной инициализации. Полное приложение с fault handlers и retry: [examples/basic/main.c](examples/basic/main.c).

Все API вызываются из одного main-контекста. ISR устанавливает byte flag и не обращается к I2C. Выходные указатели non-NULL. Сохраняйте last error до следующего API. available=false с NONE означает отсутствие данных; readGesture=NONE может также означать timeout/overflow/I2C. Восстановление границы потока выполняет драйвер — не декодируйте и не отбрасывайте хвост дополнительными чтениями приложения.

Пример проверяет флаг и физический активный низкий INT, используя ограниченный polling. CPU sleep не реализован: незащищённая последовательность проверки флага и WFI может потерять пробуждение. Для энергосберегающего приложения нужен отдельно проверенный системный handshake.

## Общий вектор EXTI

Все EXTI0..7 используют EXTI7_0_IRQHandler. При APDS_PROVIDE_EXTI_ISR=1 библиотека предоставляет strong handler, заменяющий зацикленный weak fallback SDK. Если вектор принадлежит приложению, задайте `-DAPDS_PROVIDE_EXTI_ISR=0` для всей сборки и реализуйте:

```c
#include "int_config.h"
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
    apds_handle_exti();
    /* Handle and clear any other EXTI0..7 lines here. No I2C calls. */
    apds_clear_exti();
}
```

Другой вариант — сохранить default handler и переопределить weak apds_exti_callback. Callback вызывается после снятия APDS pending, должен очищать pending остальных линий и быть коротким в рамках контракта I2C. apds_exti_enable/disable влияет на весь NVIC-вектор, не только на APDS.

Пример репозитория содержит минимальный собственный handler при provide-ISR=0; это полноценный link fixture для CI. Пользовательское приложение при отключении ISR библиотеки всё равно обязано предоставить свой обработчик.

## Перенос INT

Укажите все шесть согласованных параметров; пример ниже сохраняет PC3:

```ini
build_flags =
    -DAPDS_INT_PORT=GPIOC
    -DAPDS_INT_PIN=GPIO_Pin_3
    -DAPDS_INT_LINE=EXTI_Line3
    -DAPDS_INT_PORT_SOURCE=GPIO_PortSourceGPIOC
    -DAPDS_INT_PIN_SOURCE=GPIO_PinSource3
    -DAPDS_INT_GPIO_CLOCK=RCC_APB2Periph_GPIOC
```

AFIO и clock выбранного порта включает apds_exti_init. Проверяйте уже низкий уровень INT до ожидания нового falling edge. PC1/PC2 остаются стандартными I2C-пинами по контракту зависимости: параметры APDS INT не ремапят I2C.

## Общий SysTick и собственные задержки

OWN=1 нормализует SysTick, монопольно использует его внутри readGesture и останавливает при возврате. OWN=0 включает iteration budget, но default Delay_Ms по-прежнему меняет SysTick. Для сохранения таймера приложения:

```ini
build_flags =
    -DAPDS_OWN_SYSTICK=0
    -DAPDS_DELAY_MS=app_apds_delay_ms
```

Реализуйте связанную функцию `void app_apds_delay_ms(uint32_t ms)`, ожидающую не менее заданного без изменения SysTick. Драйвер объявляет выбранную внешнюю функцию. Это управляет задержками APDS; ресурсы времени остальных компонентов проверяйте независимо. После изменения clocks повторно инициализируйте соответствующие компоненты.

## Профили, сбои и переход с прежней версии

Четыре ID сохранены. Профили0xA8/0x9E меняют только gesture gain/drive, не карту регистров и не proximity gain/drive. Для ручного tuning отключите нужный профиль и задайте GGAIN/GLDRIVE/LED_BOOST. Профиль не гарантирует исправность каждого модуля с этим ID. Номинальные токи: [README_RU.md](README_RU.md); измерения и ограничения: [HARDWARE_TEST_RU.md](HARDWARE_TEST_RU.md).

I2C_ERR_RECOVERY означает выключенный PE: устраните физическую причину, добейтесь успешного i2c_init и затем повторите apds_init. Постоянные SDA-low/ARLO не следует маскировать вечными retries или выдачей partial gesture. Wakeup восстанавливает сохранённую конфигурацию; после снятия питания нужен полный init.

После 1.3.x перепроверьте чувствительность и ориентацию: исправлены GCONF2 и знаки направлений. Прежние ошибки0..5 сохранены, добавлены TIMEOUT6/INVALID_ARG7. Удалите post-gesture discard loops. Успешная calibration может означать запись fallback; независимые свежие samples во всех фоновых условиях не обещаются.

## Проверка и границы пакета

Соберите целевой проект, измерьте linked Flash/RAM и проверьте первый жест после sleep/timeout/overflow/reinit. Размер demo не равен полному приложению. Полный stack с вашим callback/ISR нужно проверять отдельно.

Пакет библиотеки исключает examples/tests/logs/editor/CI; в GitHub-репозитории они остаются для воспроизводимости. Host-тесты используют установленный закреплённый i2c.h и не проверяют I2C implementation. Физические оригинальные0xAB/альтернативные0x9C и широкий диапазон освещения/дистанций в этом выпуске не квалифицированы. История версий: [CHANGELOG.md](CHANGELOG.md).
