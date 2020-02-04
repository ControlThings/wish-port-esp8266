2016-12-28 note: ESP8266_NONOS_SDK_V2.0.0_16_08_10.zip

These are the instructions we have used to set up the SDK that we have
been using at ControlThings for ESP8266 development since november 2015.


2016-02-09 note: SDK 1.5.1_16_01_08


2015-11-07 note: These instructions are still valid, but you should use
the newest esp_iot_sdk (1.4.1_pre5_15-10-27 as of this writing) instead of the
old distribution referenced above; the SDK in available here:
Annoncement: http://bbs.espressif.com/viewtopic.php?f=46&t=1268
Download: http://bbs.espressif.com/download/file.php?id=917

There is a full list of ESP8266 chip documentation here:
http://bbs.espressif.com/viewtopic.php?f=20&t=225

#Building the toolchain

Please consider using https://github.com/pfalcon/esp-open-sdk to build the complete toolchain. It does most of steps listed below (and some not listed, e.g. to bring you SDK with less binary blobs).
If you cannot, or prefer doing each step yourself read on.



## Install needed dependencies (as root)
### 32-Bit Debian (Linux)
```
apt-get install git autoconf build-essential gperf bison flex texinfo libtool libncurses5-dev wget gawk libc6-dev-i386 python-serial libexpat-dev
mkdir /opt/Espressif
chown $username /opt/Espressif/
```

### 64-bit Debian (Linux)
```
apt-get install git autoconf build-essential gperf bison flex texinfo libtool libncurses5-dev wget gawk libc6-dev-amd64 python-serial libexpat-dev
mkdir /opt/Espressif
chown $username /opt/Espressif/
```
(replace $username with the name of the local user)

### Windows
*By zoutepopcorn.*

- Use Python 2.7, **not** Python 3: https://www.python.org/downloads/
- Install pyserial-2.7: http://pyserial.sourceforge.net/pyserial.html

##Install the Xtensa crosstool-NG (as local user)
*A big thanks to [jcmvbkbc](https://github.com/jcmvbkbc) for making GCC and crosstool-NG work with the Xtensa call0 ABI!*

```
cd /opt/Espressif
git clone -b lx106 git://github.com/jcmvbkbc/crosstool-NG.git 
cd crosstool-NG
./bootstrap && ./configure --prefix=`pwd` && make && make install
./ct-ng xtensa-lx106-elf
./ct-ng build
PATH=$PWD/builds/xtensa-lx106-elf/bin:$PATH
```

If you install crosstool-NG outside of the build directory (by changing `--prefix` above), you need to copy `local-patches/` and `overlays/` to `<prefix>/lib/ct-ng.1.20.0/`

Note the default `xtensa-lx106-elf` configuration installs the compiler in `${CT_TOP_DIR}/builds/${CT_TARGET}`, not under `x-tools/`.

#Setting up the Espressif SDK
- Download the ZIP file from our SDK mirror at https://github.com/esp8266/esp8266-wiki/tree/master/sdk or search http://bbs.espressif.com/
- Extract the contents to `/opt/Espressif/ESP8266_SDK/`.
- Move the (ZIP-bombing) `License` file into the right directory.

```
cd /opt/Espressif
wget -O esp_iot_sdk_v0.9.3_14_11_21.zip https://github.com/esp8266/esp8266-wiki/raw/master/sdk/esp_iot_sdk_v0.9.3_14_11_21.zip
wget -O esp_iot_sdk_v0.9.3_14_11_21_patch1.zip https://github.com/esp8266/esp8266-wiki/raw/master/sdk/esp_iot_sdk_v0.9.3_14_11_21_patch1.zip
unzip esp_iot_sdk_v0.9.3_14_11_21.zip
unzip esp_iot_sdk_v0.9.3_14_11_21_patch1.zip
mv esp_iot_sdk_v0.9.3 ESP8266_SDK
mv License ESP8266_SDK/
```

##Patching
- We use another toolchain.
- The IoT_Demo Makefile assumes it's one directory up (for libraries and linker script).

```
cd /opt/Espressif/ESP8266_SDK
sed -i -e 's/xt-ar/xtensa-lx106-elf-ar/' -e 's/xt-xcc/xtensa-lx106-elf-gcc/' -e 's/xt-objcopy/xtensa-lx106-elf-objcopy/' Makefile
mv examples/IoT_Demo .
```

#Installing Xtensa libraries and headers
```
cd /opt/Espressif/ESP8266_SDK
wget -O lib/libc.a https://github.com/esp8266/esp8266-wiki/raw/master/libs/libc.a
wget -O lib/libhal.a https://github.com/esp8266/esp8266-wiki/raw/master/libs/libhal.a
wget -O include.tgz https://github.com/esp8266/esp8266-wiki/raw/master/include.tgz
tar -xvzf include.tgz
```
These are binary libraries from the Xtensa SDK. You can also build them from source, but this has not yet been tested fully:
- [lx106-hal](https://github.com/tommie/lx106-hal) and
- [newlib-xtensa](https://github.com/jcmvbkbc/newlib-xtensa)

#Installing the ESP image tool
Download the [deb](https://github.com/esp8266/esp8266-wiki/raw/master/deb/esptool_0.0.2-1_i386.deb) for esptool from the Git repo or get the [source](https://github.com/esp8266/esp8266-wiki/raw/master/deb/src/esptool_0.0.2.orig.tar.gz) and build yourself. Also available as a [Git repository](https://github.com/tommie/esptool-ck) for online browsing.

```
cd /opt/Espressif
wget -O esptool_0.0.2-1_i386.deb https://github.com/esp8266/esp8266-wiki/raw/master/deb/esptool_0.0.2-1_i386.deb
dpkg -i esptool_0.0.2-1_i386.deb
```

#Installing the ESP upload tool
```
cd /opt/Espressif
git clone https://github.com/themadinventor/esptool esptool-py
ln -s $PWD/esptool-py/esptool.py crosstool-NG/builds/xtensa-lx106-elf/bin/
```

Next step: [[Building]]
