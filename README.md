# `esp32-homie`

An `ESP-IDF` component for the [Homie convention](https://github.com/homieiot/convention).

[![Build Status](https://travis-ci.com/trombik/esp32-homie.svg?branch=homie4)](https://travis-ci.com/trombik/esp32-homie)

## Goals

This is alpha-level software. Pull requests are welcome! Here is where we're
at:

- [ ] Conforms to
  [4.x](https://homieiot.github.io/specification/spec-core-v4_0_0/) of the
  Homie specification
- [x] Publishes stats for wifi signal, freeheap, and uptime
- [x] OTA firmware updates
- [x] Reboot by MQTT command topic
- [x] Logging over MQTT
- [x] Support for extendable nodes

## Philosophy

I believe a minimalist library is a better fit for the `ESP-IDF` ecosystem
rather than a framework. I don't plan to include a captive portal, nor an
inversion of program control. The scope of this library will be to manage the
MQTT connection using the Homie convention, handle OTA, and little else.

## Supported SDKs and versions


| SDK name           | Version      |
|--------------------|--------------|
| `ESP-IDF`          | 3.x or newer |
| `ESP8266 RTOS SDK` | `master`     |

## How to use

Clone this component to [ESP-IDF](https://github.com/espressif/esp-idf) project (as submodule):

```
git submodule add https://github.com/craftmetrics/esp32-homie.git components/esp32-homie
```

## Example

Examples are under [examples](examples) directory.

## Required variables in `sdkconfig`

Some non-default variables must be set in `sdkconfig`.

For `esp-idf` version 4.x, or master, see
[`sdkconfig.defaults`](examples/esp32-homie-example/sdkconfig.defaults) and
[`sdkconfig.defaults.esp32`](examples/esp32-homie-example/sdkconfig.defaults.esp32).

For `esp-idf` version 3.x, see
[`sdkconfig.defaults.esp32_v3`](examples/esp32-homie-example/sdkconfig.defaults.esp32_v3).

For ESP8266 RTOS SDK, see
[`sdkconfig.defaults.esp8266`](examples/esp32-homie-example/sdkconfig.defaults.esp8266).

## OTA Updates

The OTA routine is based on
[native_ota_example](https://github.com/espressif/esp-idf/tree/master/examples/system/ota)
without logic change.

Note that `sdkconfig` should have necessary variables set, such as
`CONFIG_PARTITION_TABLE_TWO_OTA` in `esp-idf` 4.x, etc). For more details, see
[Over The Air Updates (OTA)](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/ota.html).

For more details, see `examples/system/ota` in your branch of `esp-idf` at
[https://github.com/espressif/esp-idf/](https://github.com/espressif/esp-idf/).

### `esp-idf` version 3.x

* Download a firmware at the location specified in `http_config`
* If the version of the firmware is different from the version of the running
  firmware, start the OTA

### `esp-idf` version 4.x

* Download a firmware at the location specified in `http_config`
* If the running firmware version is older than the new firmware, start the
  OTA.

Firmware version can be set in `version.txt` at the root directory of _your_
project. See ["App version"](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/system.html#app-version)
section in ["Miscellaneous System APIs"](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/system.html).

Accepted version string must be in the form of [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## KNOWN BUGS

Command topics, such as `ota` and `reboot`, are not properly rendered in
`PAPER UI` of `OpenHAB2` version `2.5.0~M6-1`.

Although `ESP-IDF` 3.x is supported, the example is not tested in the testing
environment.

When the SDK is ESP8266 RTOS SDK, OTA is not supported.

When the SDK is ESP8266 RTOS SDK, logging over MQTT is not supported.

## License

All code is licensed under MIT license except:

* `task_ota.c`, `task_ota_3_2.c` and their header files (Public Domain, or
  [Creative Commons CC0](https://creativecommons.org/share-your-work/public-domain/cc0/))

The library includes the following third-party libraries:

* [semver.c](https://github.com/h2non/semver.c) (MIT license, Copyright (c) Tomás Aparicio)
