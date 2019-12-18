COMPONENT_OBJS := main.o

ifdef CONFIG_IDF_TARGET_ESP8266
COMPONENT_OBJS += connect_esp8266.o
else
COMPONENT_OBJS += connect_esp32.o
endif
