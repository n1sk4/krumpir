#include "xcp_glue.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "xcplib.h"

#include "wlan.h"    /* WIFI_SSID, WIFI_PASSWORD: copy wlan.h.example to wlan.h (gitignored) */
#include "xcp_epk.h" /* XCP_EPK: written for every build by xcp_epk.py, so a stale A2L file is detected */

#define XCP_PORT 5555
#define CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(s_wifi, CONNECTED_BIT);
    esp_wifi_connect(); /* ponytail: retries forever without backoff */
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *e = data;
    printf("xcp: wifi connected, ip " IPSTR ", XCP on UDP port %d\n", IP2STR(&e->ip_info.ip), XCP_PORT);
    xEventGroupSetBits(s_wifi, CONNECTED_BIT);
  }
}

static bool wifi_connect(void)
{
  esp_err_t err = nvs_flash_init(); /* the Wi-Fi driver keeps its calibration data in NVS */
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK)
    return false;

  s_wifi = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

  wifi_config_t cfg = {0};
  strncpy((char *)cfg.sta.ssid, WIFI_SSID, sizeof cfg.sta.ssid);
  strncpy((char *)cfg.sta.password, WIFI_PASSWORD, sizeof cfg.sta.password);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_wifi_set_ps(WIFI_PS_NONE); /* modem sleep would delay the measurement packets */

  printf("xcp: connecting to wifi '%s'\n", WIFI_SSID);
  EventBits_t bits =
      xEventGroupWaitBits(s_wifi, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
  if (!(bits & CONNECTED_BIT)) {
    printf("xcp: wifi not connected after 30 s, XCP stays off\n");
    return false;
  }
  return true;
}

/* XCP timestamps in microseconds from the 64-bit ESP timer (the default is the 1 ms RTOS tick). */
static uint64_t clock_us(void) { return (uint64_t)esp_timer_get_time(); }

bool xcp_glue_start(const char *name)
{
  bool wifi = wifi_connect();

  XcpSetLogLevel(3);
  XcpCreateEpk(XCP_EPK);
  /* Always initialize: without Wi-Fi XCP stays passive, so the event triggers in the loop are safe. */
  if (!XcpInit(name, XCP_EPK, wifi ? XCP_MODE_LOCAL : XCP_MODE_DEACTIVATE)) {
    printf("xcp: XcpInit failed\n");
    return false;
  }
  if (!wifi)
    return false;
  ApplXcpRegisterGetClockCallback(clock_us);
  const uint8_t any[4] = {0, 0, 0, 0};
  if (!XcpEthServerInit(any, XCP_PORT, false, 0)) {
    printf("xcp: XcpEthServerInit failed\n");
    return false;
  }
  return true;
}
