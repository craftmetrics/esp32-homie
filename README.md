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

- ESP IDF 3.2.0
- https://github.com/tuanpmt/espmqtt

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

## Remote Logging

When remote logging is enabled, all calls to ESP_LOG\*() are published to `./log`.

To enable remote logging, send `true` to `./$implementation/logging`. To disable, send `false`.

Note that the default logger uses ANSI terminal colors in its log output, you may want to set `CONFIG_LOG_COLORS=n` in `sdkconfig` to disable this.

## License

All code is licensed under MIT license except:

* `task_ota.c`, `task_ota_3_2.c` and their header files (Public Domain, or
  [Creative Commons CC0](https://creativecommons.org/share-your-work/public-domain/cc0/))
