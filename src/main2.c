// #include <zephyr/kernel.h>
// #include <zephyr/device.h>
// #include <zephyr/drivers/uart.h>
// #include <zephyr/drivers/led.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/sys/atomic.h>
// #include <string.h>

// LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

// /* CO2 센서 */
// #define MSG_SIZE       9
// #define CO2_MULTIPLIER 256
// #define CO2_THRESHOLD  4000  /* ppm - 이 값보다 높으면 LED ON */

// static const struct device *const uart_serial =
//     DEVICE_DT_GET(DT_ALIAS(myserial));

// static char rx_buf[MSG_SIZE];
// static int  rx_buf_pos;
// static atomic_t co2_ppm_val = ATOMIC_INIT(0);

// enum uart_fsm_code {
//     UART_FSM_IDLE,
//     UART_FSM_HEADER,
//     UART_FSM_DATA,
//     UART_FSM_CHECKSUM,
//     UART_FSM_END,
// };
// static uint8_t uart_fsm = UART_FSM_IDLE;

// /* LED Matrix */
// #define LED_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(holtek_ht16k33)
// static const struct device *const led_dev = DEVICE_DT_GET(LED_NODE);

// /* ─────────────────────────────────── */

// uint8_t check_usart_fsm(uint8_t read_data)
// {
//     switch (uart_fsm) {
//     case UART_FSM_IDLE:
//         if (read_data == 0xFF) uart_fsm = UART_FSM_HEADER;
//         break;
//     case UART_FSM_HEADER:
//         if (read_data == 0x86) uart_fsm = UART_FSM_DATA;
//         else                   uart_fsm = UART_FSM_IDLE;
//         break;
//     case UART_FSM_DATA:
//         if (rx_buf_pos == MSG_SIZE - 2) uart_fsm = UART_FSM_CHECKSUM;
//         break;
//     case UART_FSM_CHECKSUM:
//         if (rx_buf_pos == MSG_SIZE - 1) uart_fsm = UART_FSM_END;
//         break;
//     case UART_FSM_END:
//         uart_fsm = UART_FSM_IDLE;
//         break;
//     default:
//         uart_fsm = UART_FSM_IDLE;
//         break;
//     }
//     return uart_fsm;
// }

// char getCheckSum(char *packet)
// {
//     char i, checksum = 0;
//     for (i = 1; i < 8; i++) checksum += packet[i];
//     checksum = 0xff - checksum;
//     checksum += 1;
//     return checksum;
// }

// void serial_callback(const struct device *dev, void *user_data)
// {
//     uint8_t c;
//     char checksum_ok, value_calc_flag;
//     int checksum;

//     if (!uart_irq_update(uart_serial)) return;
//     if (!uart_irq_rx_ready(uart_serial)) return;

//     while (uart_fifo_read(uart_serial, &c, 1) == 1) {
//         if (uart_fsm == UART_FSM_IDLE) rx_buf_pos = 0;
//         check_usart_fsm(c);
//         if (rx_buf_pos >= MSG_SIZE) rx_buf_pos = 0;
//         rx_buf[rx_buf_pos++] = c;
//     }

//     value_calc_flag = rx_buf_pos == MSG_SIZE;
//     if (value_calc_flag) {
//         checksum    = getCheckSum(rx_buf);
//         checksum_ok = checksum == rx_buf[8];

//         if (checksum_ok) {
//             uint8_t high = rx_buf[2];
//             uint8_t low  = rx_buf[3];
//             int co2_ppm  = (high * CO2_MULTIPLIER) + low;
//             atomic_set(&co2_ppm_val, co2_ppm);
//             printk("CO2: %d ppm\n", co2_ppm);

//             for (int i = 0; i < MSG_SIZE; i++) printk("%x ", rx_buf[i]);
//             printk("\n");
//         }
//     }
// }

// void serial_write(void)
// {
//     uint8_t tx_buf[MSG_SIZE] =
//         {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
//     for (int i = 0; i < MSG_SIZE; i++) {
//         uart_poll_out(uart_serial, tx_buf[i]);
//     }
// }

// int main(void)
// {
//     bool leds_on = false;

//     if (!device_is_ready(uart_serial)) {
//         printk("UART device not found!\n");
//         return 0;
//     }

//     if (!device_is_ready(led_dev)) {
//         printk("LED device not ready\n");
//         return 0;
//     }

//     /* LED 초기 OFF */
//     led_off(led_dev, 0);

//     int ret = uart_irq_callback_user_data_set(uart_serial,
//                                               serial_callback, NULL);
//     if (ret < 0) {
//         printk("Error setting UART callback: %d\n", ret);
//         return 0;
//     }
//     uart_irq_rx_enable(uart_serial);

//     while (1) {
//         k_sleep(K_MSEC(5000));
//         serial_write();

//         int ppm = (int)atomic_get(&co2_ppm_val);

//         if (ppm > CO2_THRESHOLD && !leds_on) {
//             printk("CO2 detected! LED Matrix ON\n");
//             led_on(led_dev, 0);
//             leds_on = true;
//         } else if (ppm <= CO2_THRESHOLD && leds_on) {
//             printk("No CO2. LED Matrix OFF\n");
//             led_off(led_dev, 0);
//             leds_on = false;
//         }
//     }

//     return 0;
// }

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* LED Matrix */
#define LED_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(holtek_ht16k33)
static const struct device *const led_dev = DEVICE_DT_GET(LED_NODE);

/* Battery Display TM1651: CLK=D10=P1.12, DIO=D11=P1.13 */
#define TM1651_CLK 12
#define TM1651_DIO 13

static const struct device *gpio1;

/* ★ 여기만 바꾸면 됨 (0~100) */
#define MY_NUMBER 42

static void tm_delay(void) { k_busy_wait(50); }

static void tm_start(void)
{
    gpio_pin_set(gpio1, TM1651_CLK, 1);
    gpio_pin_set(gpio1, TM1651_DIO, 1);
    tm_delay();
    gpio_pin_set(gpio1, TM1651_DIO, 0);
    tm_delay();
    gpio_pin_set(gpio1, TM1651_CLK, 0);
    tm_delay();
}

static void tm_stop(void)
{
    gpio_pin_set(gpio1, TM1651_CLK, 0);
    tm_delay();
    gpio_pin_set(gpio1, TM1651_DIO, 0);
    tm_delay();
    gpio_pin_set(gpio1, TM1651_CLK, 1);
    tm_delay();
    gpio_pin_set(gpio1, TM1651_DIO, 1);
    tm_delay();
}

static void tm_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        gpio_pin_set(gpio1, TM1651_CLK, 0);
        tm_delay();
        gpio_pin_set(gpio1, TM1651_DIO, (byte >> i) & 1);
        tm_delay();
        gpio_pin_set(gpio1, TM1651_CLK, 1);
        tm_delay();
    }
    /* ACK */
    gpio_pin_set(gpio1, TM1651_CLK, 0);
    gpio_pin_configure(gpio1, TM1651_DIO, GPIO_INPUT);
    tm_delay();
    gpio_pin_set(gpio1, TM1651_CLK, 1);
    tm_delay();
    gpio_pin_set(gpio1, TM1651_CLK, 0);
    gpio_pin_configure(gpio1, TM1651_DIO, GPIO_OUTPUT_LOW);
    tm_delay();
}

/* level: 0~7 */
/* level: 0~7, 아래서부터 채워짐 */
void battery_set_level(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    /* 퍼센트 → 켜야 할 칸 수 (0~10), 10% 단위로 정확히 */
    int bars = percent / 10;  /* 반올림 없이 그냥 나누기 */
    if (bars > 10) bars = 10;

    /* 10비트 마스크, MSB가 맨 위 칸 */
    uint16_t mask = 0;
    for (int i = 0; i < bars; i++) {
        mask |= (1 << (9 - i));
    }

    uint8_t high = (mask >> 2) & 0xFF;
    uint8_t low  = (mask & 0x03) << 6;

    printk("percent=%d bars=%d mask=0x%04x\n", percent, bars, mask);

    tm_start();
    tm_write_byte(0x44);
    tm_stop();

    tm_start();
    tm_write_byte(0xC0);
    tm_write_byte(high);
    tm_stop();

    tm_start();
    tm_write_byte(0x44);
    tm_stop();

    tm_start();
    tm_write_byte(0xC1);
    tm_write_byte(low);
    tm_stop();

    tm_start();
    tm_write_byte(0x8A);
    tm_stop();
}

int main(void)
{
    gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    gpio_pin_configure(gpio1, TM1651_CLK, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpio1, TM1651_DIO, GPIO_OUTPUT_HIGH);

    if (!device_is_ready(led_dev)) {
        printk("LED device not ready\n");
        return 0;
    }

    /* 전체 끄기 */
    for (int i = 0; i < 128; i++) led_off(led_dev, i);
    battery_set_level(0);
    k_sleep(K_MSEC(500));

    /* MY_NUMBER 만큼 LED Matrix 켜기 */
    int num = (MY_NUMBER > 128) ? 128 : MY_NUMBER;
    printk("LED Matrix: %d LEDs ON\n", num);
    for (int i = 0; i < num; i++) {
        led_on(led_dev, i);
    }

    /* Battery: MY_NUMBER를 퍼센트로 그대로 전달 */
    printk("Battery: %d%%\n", MY_NUMBER);
    battery_set_level(MY_NUMBER);

    while (1) {
        k_sleep(K_MSEC(1000));
    }
    return 0;
}