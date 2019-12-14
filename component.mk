# Use defaults
COMPONENT_PRIV_INCLUDEDIRS := "."
COMPONENT_OBJS := \
	homie.o \
	vendors/semver.c/semver.o \
	task_ota_3_2.o \
	task_log_mqtt.o

COMPONENT_SRCDIRS := . vendors/semver.c
