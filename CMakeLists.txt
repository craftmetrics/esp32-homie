idf_component_register(SRCS "homie.c" "task_ota"
    REQUIRES "app_update" "mqtt" "esp_https_ota" "nvs_flash"
    INCLUDE_DIRS "include" "."
)
