idf_component_register(
	SRCS "homie.c" "ota.c"
	INCLUDE_DIRS "include"
	REQUIRES mqtt app_update esp_https_ota
)