---
name: arduino-development
description: This skill should be used when the user asks to "create an Arduino project", "program an Arduino", "write Arduino code", "upload to Arduino", "compile Arduino sketch", "use arduino-cli", "blink an LED", "read a sensor", mentions "microcontroller programming", "embedded development with Arduino", "Arduino Uno", "Arduino Nano", "Arduino Mega", "ESP32 with Arduino", "ESP8266", "Adafruit board", or discusses IoT prototyping, sensor projects, servo control, or Arduino library usage.
version: 1.0.0
metadata:
  mcpmarket-version: 1.0.0
---
# Arduino Development

Guidance for Arduino development using the `arduino-cli` command-line tool (v1.4.1).

## Core Workflow

Every Arduino project follows this sequence:

1. **Detect the board**: Run `arduino-cli board list` to identify connected hardware. The output includes the port address and FQBN (Fully Qualified Board Name) needed for all subsequent steps.

2. **Install the platform core**: Each board family requires a platform core. Extract the core ID from the FQBN (first two colon-separated segments, e.g., `arduino:avr` from `arduino:avr:uno`). Install with `arduino-cli core install <core-id>`.

3. **Create the sketch**: Run `arduino-cli sketch new <Name>`. This creates a directory with a `.ino` file containing empty `setup()` and `loop()` functions. The directory name must match the `.ino` filename.

4. **Write the code**: Edit the `.ino` file. Every sketch requires:
   - `void setup()` -- runs once at power-on/reset
   - `void loop()` -- runs repeatedly after setup

5. **Compile**: Run `arduino-cli compile --fqbn <FQBN> <sketch-dir>`. The output reports flash and RAM usage.

6. **Upload**: Run `arduino-cli upload -p <port> --fqbn <FQBN> <sketch-dir>`. Some boards require pressing a reset button before upload.

7. **Monitor** (optional): Run `arduino-cli monitor -p <port>` to view Serial output. Default baud rate is 9600.

## Available Slash Commands

Use these commands for streamlined workflows:
- `/arduino-new <name>` -- Create a new sketch
- `/arduino-build [path] [--fqbn <FQBN>]` -- Compile a sketch
- `/arduino-upload [path]` -- Upload to a connected board
- `/arduino-boards [search]` -- List boards or search for board support
- `/arduino-libs <search | install name>` -- Find or install libraries

## Arduino Sketch Structure

A minimal sketch:

```cpp
void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
}
```

Key conventions:
- Pin constants: `LED_BUILTIN`, `A0`-`A5`, `D0`-`D13`
- Serial communication: `Serial.begin(baud)`, `Serial.println(data)`
- Digital I/O: `pinMode()`, `digitalWrite()`, `digitalRead()`
- Analog I/O: `analogRead()`, `analogWrite()` (PWM)
- Timing: `delay(ms)`, `millis()`, `micros()`
- Prefer `millis()` over `delay()` for non-blocking timing

## Multi-File Projects

For larger projects, place additional files alongside the `.ino`:

```
MyProject/
├── MyProject.ino       # Main sketch (must match dir name)
├── sensors.h           # Header for sensor module
├── sensors.cpp         # Sensor implementation
└── config.h            # Configuration constants
```

All `.ino`, `.c`, `.cpp`, `.h` files in the sketch directory are automatically compiled together. Use `#include "file.h"` for local headers.

## Library Management

- Search: `arduino-cli lib search <keyword>`
- Install: `arduino-cli lib install "<Library Name>"`
- List installed: `arduino-cli lib list`
- Include with: `#include <LibraryName.h>`

## 3rd Party Board Support

Boards like ESP32, ESP8266, and Adafruit require additional board manager URLs. Configure with:
```
arduino-cli config set board_manager.additional_urls <URL1>,<URL2>
```
Then run `arduino-cli core update-index` to refresh.

## Additional Resources

For detailed board-to-FQBN-to-core mappings, consult:
- **`references/board-platforms.md`** -- Common boards, FQBNs, cores, and board manager URLs

For common compilation and upload errors, consult:
- **`references/troubleshooting.md`** -- Error diagnosis and fixes

For advanced project structure patterns, consult:
- **`references/project-patterns.md`** -- State machines, non-blocking timing, multi-file organization

## Important Notes

- Use `--json` flag when parsing `arduino-cli` output programmatically
- The `board list` command shows Bluetooth and debug serial ports too; only entries with `matching_boards` in the JSON represent actual Arduino devices
- Some boards (ESP32, STM32) require additional board manager URLs
- On macOS, serial ports appear as `/dev/cu.usbmodem*` or `/dev/cu.usbserial*`
