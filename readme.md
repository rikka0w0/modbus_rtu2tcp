# modbus_rtu2tcp
ESP8266-based Modbus RTU-TCP transparent bridge, allows multiple TCP masters to access different RTU slaves concurrently. (Ethernet to RS485 Modbus)

## Hardware
### Wiring
1. UART, GPIO3 for RXD (connect to TXD of the other end), GPIO1 for TXD (connect to RXD of the other end). These two pins are used for both chip programming
and communication between the UART device (or the RS485 interface chip). Both must be high at power on, otherwise boot fails.
2. GPIO0, this pin controls the boot behavior of ESP8266. When pulled to ground during a restart, ESP8266 will enter flashing mode, and the new firmwire will be
downloaded via GPIO3 and GPIO1. For normal power-on, this pin has to be pull up, not to be left floating. Therefore, __It is highly recommended to connect a 10k resistor
between this pin and Vcc. A push button (for flash) should be connected between this pin and the ground.__ In addition, this pin outputs the direction control (DE)
signal required by the RS485 interface chip (e.g. MAX485). However, due to the limitation of ESP8266 where GPIO0 must be high on boot, the DE signal output on this pin is designed to be active low. A small signal Mosfet(e.g. 2N7002) must be used to invert the logic to correctly operate the RS485 interface chip.
3. GPIO2, internally connected to on-board LED on some modules (ESP-01), boot fails if it is pulled LOW. After boot, this pin is the log output. (Baud 115200, no parity, 8 data bits, 1 stop bit)
4. GPIO15, boot fails if pulled HIGH, can be used as GPIO after boot. Not available on ESP-01 module.
5. CH_PD/EN, connect to Vcc
6. Reset, pull up to Vcc via a 10k resistor. Pull this pin to ground will restart ESP8266.

### Operation Mode
1. UART (and RS232)

Directly connect GPIO3 and GPIO1 to the target microcontroller. DE (GPIO0) is not required and must be pulled up. The high level of Rx and Tx is 3.3V, level shifting is compulsory if the microcontroller runs on 5V. RS232 uses +/- 15V signaling, thus requiring an interface chip.

2. RS485 (Modbus RTU)

Connect GPIO3, GPIO1 and GPIO0 to the RS485 interface chip (e.g. cheap option MAX3485 or isolated option ADM2483), the DE signal from GPIO0 needs inversion in most case. __Be aware that 5V-powered MAX485 (or similar) can damage ESP8266.__


## Networking
By default, the device is in AP mode (Launch its own hotspot), the ssid is "Modbus RTU2TCP" plus the MAC address, the default password is "password" (case sensitive), IPv4 and IPv6 addresses are 10.1.10.1 and FE80::1, respectively. The device can be configured in the web page to operate in STA mode (Connect to you wireless LAN).

## Compile
Requires ESP8266_RTOS_SDK, please follow [the setup instructions](https://github.com/espressif/ESP8266_RTOS_SDK) before compile this project.

[ESP8266_RTOS_SDK version:](https://github.com/espressif/ESP8266_RTOS_SDK/tree/7f99618d9e27a726a512e22ebe81ccbd474cc530)
`master 7f99618d [origin/master] Merge branch 'bugfix/fix_rf_state_error_when_read_adc' into 'master'`

```
# Config settings
make menuconfig
# Compile the firmware
make all
# Erase everything on the flash
make erase_flash
# Flash the firmware to ESP8266
make flash
```

## TODOs
1. Need better HTML front-end, I'm really not good at this.
2. Support wifi scan
3. Add a HTML based Modbus host, for performing quick tests of attached RTU devices.
3. Add a Modbus slave device emulator, which controls the spare GPIOs on ESP8266.

Pull-requests are welcomed!
