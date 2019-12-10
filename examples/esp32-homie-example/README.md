# esp32-homie-example

An example project for https://github.com/craftmetrics/esp32-homie

## How to use

 1. Install and set up the [ESP-IDF toolchain](http://esp-idf.readthedocs.io)
 1. `make menuconfig` and fill out the "ESP32 Homie Example" and serial flasher section
 1. `make -j4 flash monitor`

## Example log

```
I (72) boot: Chip Revision: 0
I (32) boot: ESP-IDF v4.1-dev-1088-gb258fc376 2nd stage bootloader
I (32) boot: compile time 19:36:16
I (32) boot: Enabling RNG early entropy source...
I (38) boot: SPI Speed      : 40MHz
I (42) boot: SPI Mode       : DIO
I (46) boot: SPI Flash Size : 4MB

...

I (524) cpu_start: Pro cpu start user code
I (542) spi_flash: detected chip: gd
I (543) spi_flash: flash io: dio
I (543) cpu_start: Starting scheduler on PRO CPU.
I (0) cpu_start: Starting scheduler on APP CPU.
I (645) wifi: wifi driver task: 3ffc14c8, prio:23, stack:3584, core=0
I (645) system_api: Base MAC address is not set, read default base MAC address from BLK0 of EFUSE
I (645) system_api: Base MAC address is not set, read default base MAC address from BLK0 of EFUSE
I (675) wifi: wifi firmware version: a44d1c6
I (675) wifi: config NVS flash: enabled
I (675) wifi: config nano formating: disabled
I (675) wifi: Init dynamic tx buffer num: 32
I (675) wifi: Init data frame dynamic rx buffer num: 32
I (685) wifi: Init management frame dynamic rx buffer num: 32
I (685) wifi: Init management short buffer num: 32
I (695) wifi: Init static rx buffer size: 1600
I (695) wifi: Init static rx buffer num: 10
I (705) wifi: Init dynamic rx buffer num: 32
I (705) EXAMPLE: start the WIFI SSID:[MY_SSID] password:[******]
I (805) phy: phy_version: 4180, cb3948e, Sep 12 2019, 16:39:13, 0, 0
I (815) wifi: mode : sta (xx:xx:xx:xx:xx:xx)
I (815) EXAMPLE: Waiting for wifi
I (935) wifi: new:<1,0>, old:<1,0>, ap:<255,255>, sta:<1,0>, prof:1
I (935) wifi: state: init -> auth (b0)
I (975) wifi: state: auth -> assoc (0)
I (1015) wifi: state: assoc -> run (10)
I (1125) wifi: connected with Cheng Lay Guesthouse, aid = 4, channel 1, BW20, bssid = xx:xx:xx:xx:xx:xx, security type: 4, phy: bgn, rssi: -78
I (1135) wifi: pm start, type: 1

I (1205) wifi: AP's beacon interval = 102400 us, DTIM period = 1
I (5625) esp_netif_handlers: sta ip: 192.168.1.51, mask: 255.255.255.0, gw: 192.168.1.1
I (5625) HOMIE: Running esp_mqtt_client_start()
I (5625) HOMIE: Starting homie_task
I (5625) HOMIE: MQTT_EVENT_BEFORE_CONNECT
I (5635) EXAMPLE: MQTT_EVENT_BEFORE_CONNECT in my_mqtt_handler
MQTT URI: `mqtt://192.168.1.1:1883`
MAC address: `XXXXXXXXXXXX` / `XX:XX:XX:XX:XX:XX`
The topic of the device: `homie/foo/#` (use this topic path to see published attributes)
An example command:
	mosquitto_sub -v -h ip.add.re.ss -t 'homie/foo/#'
I (5665) EXAMPLE: Waiting for HOMIE_MQTT_CONNECTED_BIT to be set
I (6675) EXAMPLE: Waiting for HOMIE_MQTT_CONNECTED_BIT to be set
I (6745) MQTT_CLIENT: Sending MQTT CONNECT message, type: 1, id: 0000
I (6765) HOMIE: MQTT_EVENT_CONNECTED
I (6765) HOMIE: Starting the loop in homie_task()
I (6765) EXAMPLE: MQTT_EVENT_CONNECTED in my_mqtt_handler
I (6775) EXAMPLE: MQTT client has connected to the broker
I (6825) HOMIE: successfully subscribed to topic: `homie/foo/esp/reboot/set` msg_id=6184
I (6825) HOMIE: successfully subscribed to topic: `homie/foo/esp/ota/set` msg_id=20424
I (6835) HOMIE: device status has been updated
I (7325) HOMIE: MQTT_EVENT_SUBSCRIBED, msg_id=6184
I (7325) EXAMPLE: MQTT_EVENT_SUBSCRIBED in my_mqtt_handler
I (7335) HOMIE: MQTT_EVENT_SUBSCRIBED, msg_id=20424
I (7335) EXAMPLE: MQTT_EVENT_SUBSCRIBED in my_mqtt_handler
```

## Example output of MQTT topics of the device

```
homie/foo/$state init
homie/foo/$homie 4.0.1
homie/foo/$name My Device
homie/foo/$nodes esp
homie/foo/esp/$name ESP32
homie/foo/esp/$type rev: 0
homie/foo/esp/$properties uptime,rssi,signal,freeheap,mac,ip,sdk,firmware,firmware-version,ota,reboot
homie/foo/esp/uptime/$name Uptime since boot
homie/foo/esp/uptime/$datatype integer
homie/foo/esp/rssi/$name WiFi RSSI
homie/foo/esp/rssi/$datatype integer
homie/foo/esp/signal/$name WiFi RSSI in signal strength
homie/foo/esp/signal/$datatype integer
homie/foo/esp/freeheap/$name Free heap memory
homie/foo/esp/freeheap/$datatype integer
homie/foo/esp/mac/$name MAC address
homie/foo/esp/mac/$datatype string
homie/foo/esp/mac XX:XX:XX:XX:XX:XX
homie/foo/esp/ip/$name IP address
homie/foo/esp/ip/$datatype string
homie/foo/esp/ip 192.168.1.51
homie/foo/esp/sdk/$name SDK version
homie/foo/esp/sdk/$datatype string
homie/foo/esp/sdk v4.1-dev-1088-gb258fc376
homie/foo/esp/firmware/$name Firmware name
homie/foo/esp/firmware/$datatype string
homie/foo/esp/firmware Example
homie/foo/esp/firmware-version/$name Firmware version
homie/foo/esp/firmware-version/$datatype string
homie/foo/esp/firmware-version 0.0.1
homie/foo/esp/ota/$name OTA state
homie/foo/esp/ota/$datatype enum
homie/foo/esp/ota/$settable true
homie/foo/esp/ota/$retained false
homie/foo/esp/ota/$format idle,disabled,running,run
homie/foo/esp/ota idle
homie/foo/esp/reboot/$name Reboot state
homie/foo/esp/reboot/$datatype enum
homie/foo/esp/reboot/$settable true
homie/foo/esp/reboot/$retained false
homie/foo/esp/reboot/$format disabled,enabled,rebooting,reboot
homie/foo/esp/reboot enabled
homie/foo/esp/reboot/set (null)
homie/foo/esp/ota/set (null)
homie/foo/$state ready
homie/foo/esp/uptime 6
homie/foo/esp/rssi -83
homie/foo/esp/signal 34
homie/foo/esp/freeheap 200464
```
