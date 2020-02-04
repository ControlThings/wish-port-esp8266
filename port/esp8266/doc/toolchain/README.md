# ESP8266

## How to flash ESP8266

### Dowload esptool

https://github.com/themadinventor/esptool

```bash
git clone https://github.com/themadinventor/esptool.git
```

### Download the latest firmware ("AT-program")

https://docs.google.com/file/d/0B3dUKfqzZnlwdUJUc2hkZDUyVjA/edit?pli=1

### Connect ESP8266 to TTL-USB

On the ttl-usb connect 3.3v with a jumber.

Connections from ttl-usb to esp8266 are:

GND -> GND
RXD -> TXD
TXD -> RXD
VDD -> VCC

look at flash-esp.png.

### Flash the thing

1. Connect ttl-usb to yor usb port.
2. Hold down boot and press reset button.
3. 
```bash
esptool $ sudo python esptool.py --port /dev/ttyUSB0  write_flash 0x00000 Path_To_AT Firmware.bin 
``` 
Not if problem to conect try to boot the esp with external power and then 

## How to use ESP8266(ta firmware) and minicom

Disconnect everything if you just flashed the card !!

### Connect ESP8266 to TTL-USB
 
1. On the ttl-usb connect 3.3v with a jumber.

2. Connections from ttl-usb to esp8266 are:

GND -> GND
RXD -> TXD
TXD -> RXD

3. Connect external battery to esp8266.

look at minicom-ta.png.

### Try if it works

i. Connect ttl-usb to yor usb port.
ii. Start minicom.
```bash
minicom -b 9600 -D /dev/ttyUSB0
```
iii. Press reset button on esp.
iv. to check firmvare version:
```bash
TA-GMR (ctrl)m (ctrl)j
``` 
#### Use (ctrl)m (ctrl)j not return 

## Problems

```
Traceback (most recent call last):
  File "esptool.py", line 471, in <module>
    esp.connect()
  File "esptool.py", line 133, in connect
    self._port.setRTS(True)
  File "/usr/lib/python2.7/dist-packages/serial/serialposix.py", line 525, in setRTS
    fcntl.ioctl(self.fd, TIOCMBIS, TIOCM_RTS_str)
IOError: [Errno 5] Input/output error
```
Connect external power and boot the esp and start minicom, repeat from step one.

```
Writing at 0x00000000... (3 %)
Traceback (most recent call last):
  File "esptool.py", line 536, in <module>
    esp.flash_block(block, seq)
  File "esptool.py", line 195, in flash_block
    struct.pack('<IIII', len(data), seq, 0, 0)+data, ESPROM.checksum(data))[1] != "\0\0":
  File "esptool.py", line 106, in command
    raise Exception('Invalid head of packet')
Exception: Invalid head of packet

```
Do not use external power when flashing.


