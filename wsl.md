This document describes how to setup ESP8266/ESP32 flashing on Windows + WSL2.

In short words, we need to forward the serial port to WSL. 

# Windows side
Commands should work in both Powershell and CMD.

1. Install Python (via any method). Visual Studio 2022 and above comes with its own Python3 installation, by adding its path (`C:\Program Files (x86)\Microsoft Visual Studio\Shared\Python39_64` in my case) to the "Path" environmental variable and __making it the highest priority (first entry to be the best)__, we can then run python directly in Powershell or CMD. You need to open a new terminal after setting the environmental variable.
2. Install git separately or use the one comes with Visual Studio 2022. In the latter case, add its path (`C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin` in my case) to the "Path" environmental variable, then restart the terminal.
2. Install required libraries: `python -m pip install serial pyserial`. If pip is not found, you need to enable python development in Visual Studio.
3. Get esptool: `git clone https://github.com/espressif/esptool.git`
4. Enter the repo directory: `cd esptool`
5. Run the RFC2217 server: `python esp_rfc2217_server.py -p2217 \\.\COM3`, replace "COM3" with your COM port, you can find that in "Device Manager".
6. Run `ipconfig` in another terminal and note down your IP address for latter use.

# Linux side (WSL2, assuming Ubuntu 22.04)
1. Before we start, we need to have python3 and pip available: `sudo apt install python3 python-is-python3 python3-pip python-serial`
2. Install build tools for the host side: `sudo apt install gcc git wget make libncurses-dev flex bison gperf build-essential`
3. Download the toolchain for Linux 64-bit and extract it to `/opt`:
```
wget https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz
sudo tar -zxf xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz --directory /opt
rm xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz
```
4. Download the ESP8266-RTOS-SDK and extract it to `/opt`:
```
cd /tmp
git clone https://github.com/espressif/ESP8266_RTOS_SDK.git
sudo mv -v ESP8266_RTOS_SDK /opt
```
5. Append these lines to the end of `~/.bashrc`, you can use `nano ~/.bashrc` to edit the file:
```
export PATH="$PATH:/opt/xtensa-lx106-elf/bin"
export IDF_PATH=/opt/ESP8266_RTOS_SDK
```
6. Apply the new bashrc (in all shells): `source ~/.bashrc`
6. ( [Recommended](https://github.com/espressif/ESP8266_RTOS_SDK/issues/1229) ), comment out (with #) "pyparsing" and "pyelftools" in `$IDF_PATH/requirements.txt`
7. Install dependencies required by ESP8266_RTOS_SDK: `python -m pip install --user -r $IDF_PATH/requirements.txt`
8. Clone this repo, build it accroding to the instructions in [readme.md](readme.md).
9. In `make menuconfig`, set "Serial flasher config -> Default serial port" to `rfc2217://<Host IP>:2217?ign_set_control`, where "<Host IP>" should be replaced with the IP of the RFC2217 host, i.e. the Windows machine. If you prefer ttynvt, set it to `/dev/ttyNVT0`.

## See also
1. https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/remote-serial-ports.html
2. https://github.com/espressif/esptool/issues/628
3. https://github.com/espressif/esptool/issues/383
4. https://pyserial.readthedocs.io/en/latest/url_handlers.html
5. https://docs.espressif.com/projects/esp8266-rtos-sdk/en/v3.4/get-started/linux-setup.html

# Utilities (Linux Only)

## ttynvt
The serial library of python supports RFC2217, but most of Linux tools dont. "ttynvt" allows us to connect to a RFC2217 server and expose a virtual serial port under `/dev`, so that we can use it in any Linux tools.

We need to compile this tool by ourselves.

1. Install required tools (Assume you have all the build tools in the "Linux side" section): `sudo apt install automake autoconf libfuse-dev pkg-config`
2. Run these commands:
```
autoreconf -vif
./configure
make
```
3. If succeed, the binary will be available at `src/ttynvt`.
4. Usage: `sudo ./ttynvt -n ttyNVT0 -S <Host IP>:2217`, where "<Host IP>" should be replaced with the IP of the RFC2217 host, i.e. the Windows machine.
5. Run a Linux serial tool, e.g. `screen /dev/ttyNVT0 115200`.
