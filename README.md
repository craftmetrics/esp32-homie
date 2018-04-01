# esp32-homie

An esp-idf component for the [Homie convention](https://github.com/homieiot/convention).

## Goals

This is alpha-level software. Pull requests are welcome! Here is where we're at:

* Conforms to 2.0.1 of the Homie specification
* Publishes stats for wifi signal, freeheap, and uptime
* Support for extendible nodes/stats (planned)
* OTA updates similar to [homie-esp8266](http://marvinroger.github.io/homie-esp8266/docs/2.0.0/others/ota-configuration-updates/) (planned)
* Compatibility with [jpmens/homie-ota](https://github.com/jpmens/homie-ota) (planned)

## Philosophy

I believe a minimalist library is a better fit for the ESP-IDF ecosystem rather than a framework. I don't plan to include a captive portal, nor an inversion of program control. The scope of this library will be to manage the MQTT connection using the Homie convention, handle OTA, and little else.

## Dependencies

 * https://github.com/tuanpmt/espmqtt

## How to use

Clone this component to [ESP-IDF](https://github.com/espressif/esp-idf) project (as submodule):
```
git submodule add https://github.com/craftmetrics/esp32-homie.git components/esp32-homie
```

## Example

https://github.com/craftmetrics/esp32-homie-example
