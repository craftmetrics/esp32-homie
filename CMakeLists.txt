idf_component_register(
    SRCS "homie.c"
        "task_ota.c"
        "vendors/semver.c/semver.c"
    REQUIRES "app_update" "mqtt" "esp_https_ota" "nvs_flash"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "." "vendors/semver.c"
)
