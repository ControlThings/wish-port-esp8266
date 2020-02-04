To upload to the module, configure the following pins:

|  Pin   | Level | Description 
| ------ | ----- | -----------
| CH_PD  | High  | Enables the chip
| GPIO0  | Low   | Selects UART download [[boot mode|Boot-Process#esp-boot-modes]]
| GPIO2  | High  | Selects UART download [[boot mode|Boot-Process#esp-boot-modes]]
| GPIO15 | Low   | If availble. Selects UART download [[boot mode|Boot-Process#esp-boot-modes]]

Pinout is in [[Hardware versions|Hardware_versions]]

## Uploading the Blinky Example
```
cd /opt/Espressif/source-code-examples/blinky
make ESPPORT=/dev/ttyUSB0 flash
```

Your device should start toggling GPIO2 a second or so after `make` completes.

## Uploading the IoT Demo
```
cd /opt/Espressif/ESP8266_SDK/IoT_Demo
cd .output/eagle/debug/image
/opt/Espressif/esptool-py/esptool.py --port /dev/ttyUSB0 write_flash 0x00000 eagle.app.v6.flash.bin 0x40000 eagle.app.v6.irom0text.bin
```

The procedure is similar for the AT Demo, but the directory is `/opt/Espressif/ESP8266_SDK/at_v0.19_on_SDKv0.9.2` instead.

## Caveats
- Note to Windows users: For the port argument, use standard port names, e.g. `COM3`.
- Sometimes a reset is required between uploading the first and second file.
- There are some cases where the upload fails with "Failed to leave Flash mode". Try updating to a newer version of `esptool-py` or [this fixed version](https://github.com/tommie/esptool.git).