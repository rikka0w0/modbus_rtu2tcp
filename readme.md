# modbus_rtu2tcp
ESP8266-based Modbus RTU-TCP transparent bridge, allows multiple TCP masters to access RTU slaves. (Ethernet to RS485 Modbus)

## Hardware
### Wiring
1. UART, GPIO3 for RXD (connect to TXD of the other end), GPIO1 for TXD (connect to RXD of the other end). These two pins are used for both chip programming
and communication between the RS232 device (or the RS485 interface chip). Both must be high at boot.
2. GPIO0, this pin controls the boot behavior of ESP8266. When pulled to ground during a restart, ESP8266 will enter flashing mode, and the new firmwire will be
downloaded via GPIO3 and GPIO1. For normal power-on, this pin has to be pull up, not to be left floating. Therefore, __It is highly recommended to connect a 10k resistor
between this pin and Vcc. A push button (for flash) should be connected between this pin and the ground. __ In addition, this pin is used as the direction control (DE)
of the RS485 interface chip. However, due to the limitation that this pin must be high on boot, the DE logic on this pin is designed as active low. A small signal
Mosfet(e.g. 2N7002) must be used to invert the logic to correctly operate the RS485 interface chip.
3. GPIO2, connected to on-board LED, boot fails if pulled LOW.
4. GPIO15, boot fails if pulled HIGH, can be used as GPIO after boot.
5. CH_PD/EN, connect to Vcc
6. Reset, pull up to Vcc via a 10k resistor. Pull this pin to ground will restart ESP8266.

### Operation Mode
1. RS232

Directly connect GPIO3 and GPIO1 to the target micro-controller, DE (GPIO0) is not required.

2. RS485 (Modbus RTU)

Connect GPIO3, GPIO1 and GPIO0 to the RS485 interface chip (e.g. cheap option MAX485 or isolated option ADM2483). __Be careful that MAX485 (or similar) should not be
powered by 5V, a 5V output can damage ESP8266.


## Network
By default, the device is in AP mode, the ssid is "Modbus RTU2TCP" plus the MAC address, the default password is "password", IPv4 and IPv6 addresses are 10.1.10.1 and FE80::1,
respectively. The device can be configured in the web page to operate in STA mode (Connect to you wireless LAN).

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