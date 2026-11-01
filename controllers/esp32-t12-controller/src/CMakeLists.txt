idf_component_register(SRCS "main.c" "control_task.c" "console.c"
                       REQUIRES driver esp_hw_support esp_system console esp_wifi esp_netif esp_event nvs_flash esp_timer)
