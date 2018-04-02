# esp32-homie

An esp-idf component for the [Homie convention](https://github.com/homieiot/convention).

## Goals

This is alpha-level software. Pull requests are welcome! Here is where we're at:

- [x] Conforms to 2.0.1 of the Homie specification
- [x] Publishes stats for wifi signal, freeheap, and uptime
- [x] OTA firmware updates
- [ ] Support for extendible nodes/stats

## Philosophy

I believe a minimalist library is a better fit for the ESP-IDF ecosystem rather than a framework. I don't plan to include a captive portal, nor an inversion of program control. The scope of this library will be to manage the MQTT connection using the Homie convention, handle OTA, and little else.

## Dependencies

 * https://github.com/tuanpmt/espmqtt
 * https://github.com/tuanpmt/esp-request

## How to use

Clone this component to [ESP-IDF](https://github.com/espressif/esp-idf) project (as submodule):
```
git submodule add https://github.com/craftmetrics/esp32-homie.git components/esp32-homie
```

## Example

https://github.com/craftmetrics/esp32-homie-example

## OTA Updates

OTA works according to the following scheme:

1. OTA must be enabled in the config passed to `homie_init` (it's off by default)
1. The initiating entity publishes a message to `./$implementation/ota/url` containing a URL to the new firmware
1. If an error is encountered, the device publishes a message to `./$implementation/ota/status`
1. If it is successful, the device reboots into the new firmware
