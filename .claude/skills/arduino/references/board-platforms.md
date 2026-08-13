# Arduino Board Platforms Reference

Common Arduino-compatible boards with their FQBNs, platform cores, and configuration requirements.

## Official Arduino Boards

### AVR Family (Most Common)
| Board | FQBN | Core ID |
|-------|------|---------|
| Arduino Uno | `arduino:avr:uno` | `arduino:avr` |
| Arduino Nano | `arduino:avr:nano` | `arduino:avr` |
| Arduino Nano (old bootloader) | `arduino:avr:nano:cpu=atmega328old` | `arduino:avr` |
| Arduino Mega 2560 | `arduino:avr:mega:cpu=atmega2560` | `arduino:avr` |
| Arduino Leonardo | `arduino:avr:leonardo` | `arduino:avr` |
| Arduino Micro | `arduino:avr:micro` | `arduino:avr` |
| Arduino Pro Mini 3.3V | `arduino:avr:pro:cpu=8MHzatmega328` | `arduino:avr` |
| Arduino Pro Mini 5V | `arduino:avr:pro:cpu=16MHzatmega328` | `arduino:avr` |

No additional URLs needed. Install core: `arduino-cli core install arduino:avr`

### SAMD Family (32-bit ARM)
| Board | FQBN | Core ID |
|-------|------|---------|
| Arduino Zero | `arduino:samd:arduino_zero_edbg` | `arduino:samd` |
| Arduino MKR1000 | `arduino:samd:mkr1000` | `arduino:samd` |
| Arduino MKR WiFi 1010 | `arduino:samd:mkrwifi1010` | `arduino:samd` |
| Arduino MKR Zero | `arduino:samd:mkrzero` | `arduino:samd` |
| Arduino Nano 33 IoT | `arduino:samd:nano_33_iot` | `arduino:samd` |

No additional URLs needed. Install core: `arduino-cli core install arduino:samd`

### Renesas Family (UNO R4)
| Board | FQBN | Core ID |
|-------|------|---------|
| Arduino UNO R4 Minima | `arduino:renesas_uno:minima` | `arduino:renesas_uno` |
| Arduino UNO R4 WiFi | `arduino:renesas_uno:unor4wifi` | `arduino:renesas_uno` |

No additional URLs needed. Install core: `arduino-cli core install arduino:renesas_uno`

### Mbed Family
| Board | FQBN | Core ID |
|-------|------|---------|
| Arduino Nano 33 BLE | `arduino:mbed_nano:nano33ble` | `arduino:mbed_nano` |
| Arduino Nano 33 BLE Sense | `arduino:mbed_nano:nano33ble` | `arduino:mbed_nano` |
| Arduino Nano RP2040 Connect | `arduino:mbed_nano:nanorp2040connect` | `arduino:mbed_nano` |
| Arduino Portenta H7 | `arduino:mbed_portenta:envie_m7` | `arduino:mbed_portenta` |

No additional URLs needed. Install with: `arduino-cli core install arduino:mbed_nano`

## 3rd Party Boards (Require Additional URLs)

### ESP32
**Board Manager URL**: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`

```
arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

| Board | FQBN |
|-------|------|
| ESP32 Dev Module | `esp32:esp32:esp32` |
| ESP32-S2 Dev Module | `esp32:esp32:esp32s2` |
| ESP32-S3 Dev Module | `esp32:esp32:esp32s3` |
| ESP32-C3 Dev Module | `esp32:esp32:esp32c3` |
| NodeMCU-32S | `esp32:esp32:nodemcu-32s` |
| LOLIN D32 | `esp32:esp32:d32` |

**Note**: ESP32 boards may require holding the BOOT button during upload.

### ESP8266
**Board Manager URL**: `https://arduino.esp8266.com/stable/package_esp8266com_index.json`

```
arduino-cli config set board_manager.additional_urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
```

| Board | FQBN |
|-------|------|
| NodeMCU 1.0 (ESP-12E) | `esp8266:esp8266:nodemcuv2` |
| Wemos D1 Mini | `esp8266:esp8266:d1_mini` |
| Generic ESP8266 | `esp8266:esp8266:generic` |

### Adafruit Boards
**Board Manager URL**: `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`

```
arduino-cli config set board_manager.additional_urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
arduino-cli core update-index
```

| Board | FQBN | Core ID |
|-------|------|---------|
| Adafruit Feather M0 | `adafruit:samd:adafruit_feather_m0` | `adafruit:samd` |
| Adafruit Feather M4 Express | `adafruit:samd:adafruit_feather_m4` | `adafruit:samd` |
| Adafruit Circuit Playground Express | `adafruit:samd:adafruit_circuitplayground_m0` | `adafruit:samd` |
| Adafruit ItsyBitsy M4 | `adafruit:samd:adafruit_itsybitsy_m4` | `adafruit:samd` |
| Adafruit QT Py (SAMD21) | `adafruit:samd:adafruit_qtpy_m0` | `adafruit:samd` |
| Adafruit Feather RP2040 | `adafruit:rp2040:adafruit_feather` | `adafruit:rp2040` |

### Raspberry Pi Pico (RP2040)
**Board Manager URL**: `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`

```
arduino-cli config set board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
```

| Board | FQBN |
|-------|------|
| Raspberry Pi Pico | `rp2040:rp2040:rpipico` |
| Raspberry Pi Pico W | `rp2040:rp2040:rpipicow` |

### STM32 (STMicroelectronics)
**Board Manager URL**: `https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json`

```
arduino-cli config set board_manager.additional_urls https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json
arduino-cli core update-index
arduino-cli core install STMicroelectronics:stm32
```

## Multiple Board Manager URLs

To configure multiple URLs at once:

```
arduino-cli config set board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json,\
  https://arduino.esp8266.com/stable/package_esp8266com_index.json,\
  https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
```

## Finding a Board's FQBN

If a board is connected and recognized:
```
arduino-cli board list
```

If a board is not recognized, search by name:
```
arduino-cli board listall <search-term>
```

Search across installed cores only. To find boards from 3rd party cores, install the core first using the URLs above.
