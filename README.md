# `esp32-homie`

An esp-idf component for the [Homie convention](https://github.com/homieiot/convention).

[![Build Status](https://travis-ci.com/trombik/esp32-homie.svg?branch=homie4)](https://travis-ci.com/trombik/esp32-homie)

## Goals

This is alpha-level software. Pull requests are welcome! Here is where we're
at:

- [ ] Conforms to
  [4.x](https://homieiot.github.io/specification/spec-core-v4_0_0/) of the
  Homie specification
- [x] Publishes stats for wifi signal, freeheap, and uptime
- [x] OTA firmware updates
- [ ] Support for extendible nodes/stats

## Philosophy

I believe a minimalist library is a better fit for the ESP-IDF ecosystem
rather than a framework. I don't plan to include a captive portal, nor an
inversion of program control. The scope of this library will be to manage the
MQTT connection using the Homie convention, handle OTA, and little else.

## Dependencies

- ESP IDF 3.2.0
- https://github.com/tuanpmt/espmqtt

## How to use

Clone this component to [ESP-IDF](https://github.com/espressif/esp-idf) project (as submodule):

```
git submodule add https://github.com/craftmetrics/esp32-homie.git components/esp32-homie
```

## Example

Examples are under [examples](examples) directory.

## OTA Updates

The OTA routine is based on
[native_ota_example](https://github.com/espressif/esp-idf/tree/master/examples/system/ota)
without logic change.

* Download a firmware at the location specified in `http_config`
* If the version of the firmware is different from the version of the running
  firmware, start the OTA

Depending on the version of `esp-idf`, additional checks will be performed (in
4.x, it checks the last invalid firmware, and do not upgrade if the version of
new firmware is same or older than the one of the running firmware).

For more details, see `examples/system/ota` in your branch of `esp-idf` at
[https://github.com/espressif/esp-idf/](https://github.com/espressif/esp-idf/).

Note that `sdkconfig` should have necessary variables set, such as
`CONFIG_PARTITION_TABLE_TWO_OTA` in `esp-idf` 4.x, etc). For more details, see
[Over The Air Updates (OTA)](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/ota.html).

## Remote Logging

Not implemented yet

## License

All code is licensed under MIT license except:

* `task_ota.c`, `task_ota_3_2.c` and their header files (Public Domain, or
  [Creative Commons CC0](https://creativecommons.org/share-your-work/public-domain/cc0/))
