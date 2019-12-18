# Use defaults
COMPONENT_PRIV_INCLUDEDIRS := "."
COMPONENT_OBJS := \
	homie.o \
	vendors/semver.c/semver.o
ifndef CONFIG_IDF_TARGET_ESP8266
COMPONENT_OBJS += \
	task_ota_3_2.o \
	task_log_mqtt.o
endif

COMPONENT_SRCDIRS := . vendors/semver.c
