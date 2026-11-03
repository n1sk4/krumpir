/* The HIL link on a UART of the ESP32: see hil_uart.h. The link logic is libs/hil (hil_port.c). */
#ifdef KRUMPIR_IO_HIL

#include "hil_uart.h"

#include <stdio.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hil_port.h"

#ifndef KRUMPIR_HIL_UART
#define KRUMPIR_HIL_UART UART_NUM_2
#endif
#ifndef KRUMPIR_HIL_TX_GPIO
#define KRUMPIR_HIL_TX_GPIO 17
#endif
#ifndef KRUMPIR_HIL_RX_GPIO
#define KRUMPIR_HIL_RX_GPIO 16
#endif
#ifndef KRUMPIR_HIL_BAUD
#define KRUMPIR_HIL_BAUD 921600
#endif

#define BUF_BYTES 1024 /* each way: some 40 frames */

/* From the tick (commands) and the reader (HELLO answers): into the driver's TX ring buffer,
 * which does not wait while there is room. */
void hil_port_write(const uint8_t *buf, size_t n)
{
  uart_write_bytes(KRUMPIR_HIL_UART, buf, n);
}

static void reader(void *arg)
{
  (void)arg;
  uint8_t buf[128];
  for (;;) {
    /* wait for one byte, then take whatever else has arrived, so a frame is never held back */
    int n = uart_read_bytes(KRUMPIR_HIL_UART, buf, 1, portMAX_DELAY);
    size_t more = 0;
    uart_get_buffered_data_len(KRUMPIR_HIL_UART, &more);
    if (n == 1 && more > 0) {
      const int m = uart_read_bytes(KRUMPIR_HIL_UART, buf + 1,
                                    more < sizeof buf - 1 ? more : sizeof buf - 1, 0);
      n += m > 0 ? m : 0;
    }
    for (int i = 0; i < n; i++) {
      const int type = hil_port_feed(buf[i]);
      if (type == HIL_HELLO) {
        printf("hil: runner connected\n");
      } else if (type == -HIL_HELLO) {
        printf("hil: the runner's HELLO is for other values; it will refuse the link\n");
      } else if (type == HIL_BYE) {
        printf("hil: BYE from the runner: stop\n");
      }
    }
  }
}

bool hil_uart_start(void)
{
  const uart_config_t cfg = {
      .baud_rate = KRUMPIR_HIL_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  if (uart_driver_install(KRUMPIR_HIL_UART, BUF_BYTES, BUF_BYTES, 0, NULL, 0) != ESP_OK ||
      uart_param_config(KRUMPIR_HIL_UART, &cfg) != ESP_OK ||
      uart_set_pin(KRUMPIR_HIL_UART, KRUMPIR_HIL_TX_GPIO, KRUMPIR_HIL_RX_GPIO, UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK) {
    printf("hil: UART%d setup failed\n", (int)KRUMPIR_HIL_UART);
    return false;
  }
  /* core 0, with the network and the console; the control task has core 1 */
  xTaskCreatePinnedToCore(reader, "hil", 3072, NULL, configMAX_PRIORITIES - 3, NULL, 0);
  printf("hil: waiting for the runner on UART%d (TX GPIO%d, RX GPIO%d, %d baud)\n",
         (int)KRUMPIR_HIL_UART, KRUMPIR_HIL_TX_GPIO, KRUMPIR_HIL_RX_GPIO, KRUMPIR_HIL_BAUD);
  return true;
}

#endif
