idf_component_register(SRCS "homie.c" "ota.c"
    REQUIRES "app_update" "mqtt" "esp_https_ota"
    INCLUDE_DIRS "include" "."
)
