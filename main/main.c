#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"

#include "lua_repl.h"

void init_console(void)
{
  fflush(stdout);
  fsync(fileno(stdout));
  setvbuf(stdin, NULL, _IONBF, 0);

  esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
  esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

  const uart_config_t uart_config = {
      .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .source_clk = UART_SCLK_REF_TICK,
  };
  ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                      256, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));

  esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

  esp_console_config_t console_config = {
      .max_cmdline_args = 8,
      .max_cmdline_length = 256,
      .hint_color = atoi(LOG_COLOR_CYAN)};
  ESP_ERROR_CHECK(esp_console_init(&console_config));
}

void app_main(void)
{
  while (1)
  {
    lua_repl();
  }
}
