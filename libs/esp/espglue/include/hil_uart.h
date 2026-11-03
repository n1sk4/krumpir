#ifndef HIL_UART_H
#define HIL_UART_H

#include <stdbool.h>

/* The HIL link on the ESP32 (hil_port.h on a UART), for a firmware built with KRUMPIR_IO_HIL
 * (PlatformIO env esp32dev-hil): UART2, TX on KRUMPIR_HIL_TX_GPIO (17), RX on KRUMPIR_HIL_RX_GPIO
 * (16), KRUMPIR_HIL_BAUD (921600) 8N1, all overridable with -D. A reader task on core 0 feeds the
 * link; the control task on core 1 takes the newest plant values every tick and sends its
 * commands into the TX buffer. The console stays on UART0.
 *
 * Call after control_task_start() (which binds the slots). Returns false if the UART cannot be
 * set up. */
bool hil_uart_start(void);

#endif
