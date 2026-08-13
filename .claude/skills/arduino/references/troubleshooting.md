# Arduino Troubleshooting Reference

Common errors and their solutions when using `arduino-cli` for Arduino development.

## Compilation Errors

### "No such file or directory" for includes
**Cause**: Missing library.
**Fix**: Install the library with `arduino-cli lib install "<LibraryName>"`. Search first with `arduino-cli lib search <keyword>`.

### "was not declared in this scope"
**Cause**: Using a function or variable before it's declared, or from a library that isn't included.
**Fix**: Add the appropriate `#include` directive. For Arduino functions like `analogWrite`, ensure the correct board is selected (not all boards support all functions).

### "Sketch too big" / "text section exceeds available space"
**Cause**: Compiled sketch exceeds the board's flash memory.
**Fix**:
- Remove unused libraries and code
- Use `F()` macro for string literals: `Serial.println(F("text"))` stores strings in flash instead of RAM
- Use smaller data types (`uint8_t` instead of `int` where possible)
- Consider a board with more flash memory

### "Low memory available" warning
**Cause**: Global variables use too much RAM (typically warns above 75%).
**Fix**:
- Use `PROGMEM` for constant data
- Reduce buffer sizes
- Use `F()` macro for Serial prints
- Avoid `String` class, use `char[]` arrays instead

### "Multiple definitions" / "already defined"
**Cause**: Variable or function defined in a header file that's included multiple times.
**Fix**: Add include guards to header files:
```cpp
#ifndef MY_HEADER_H
#define MY_HEADER_H
// header content
#endif
```

### "expected unqualified-id before" syntax errors
**Cause**: Usually a missing semicolon, brace, or parenthesis on a previous line.
**Fix**: Check the line indicated AND the lines above it for missing punctuation.

## Upload Errors

### "No device found on port"
**Cause**: Board not connected, wrong port, or port in use by another application.
**Fix**:
1. Verify cable connection (try a different USB cable -- some cables are charge-only)
2. Run `arduino-cli board list` to find the correct port
3. Close any serial monitors or other applications using the port
4. Try a different USB port on the computer

### "Permission denied" on serial port
**Cause**: User lacks permissions to access the serial port.
**Fix**:
- **macOS**: Grant Terminal/IDE serial port access in System Settings > Privacy & Security
- **Linux**: Add user to the `dialout` group: `sudo usermod -a -G dialout $USER` then log out and back in
- **Quick fix**: `sudo chmod 666 /dev/ttyACM0` (temporary, resets on reboot)

### "avrdude: stk500_getsync() not in sync"
**Cause**: Board not responding to programmer. Common with Arduino Nano clones.
**Fix**:
- For Arduino Nano clones, use the old bootloader variant: `arduino:avr:nano:cpu=atmega328old`
- Press the reset button right before upload starts
- Check that the correct FQBN is selected

### "No upload port provided"
**Cause**: The `-p` flag was not specified or board not detected.
**Fix**: Specify the port explicitly: `arduino-cli upload -p /dev/cu.usbmodem* --fqbn ...`

### "dfu-util: No DFU capable USB device available"
**Cause**: Board needs to be in bootloader/DFU mode for upload.
**Fix**: Double-tap the reset button quickly to enter bootloader mode (applies to SAMD-based boards like Arduino Zero, MKR family).

### ESP32: "Failed to connect to ESP32: Timed out"
**Cause**: ESP32 not in download mode.
**Fix**: Hold the BOOT button on the ESP32 while upload starts, release after "Connecting..." appears. Some boards have an auto-reset circuit that handles this automatically.

## Serial Monitor Issues

### "Port is busy" or "Resource busy"
**Cause**: Another process is using the serial port.
**Fix**:
- Close other serial monitors (Arduino IDE, screen, minicom, PlatformIO)
- On macOS: `lsof /dev/cu.usbmodem*` to find the process, then close it
- Kill the process: `kill <PID>`

### "Garbled output" in serial monitor
**Cause**: Baud rate mismatch between `Serial.begin()` in code and monitor setting.
**Fix**: Ensure the baud rate matches. Common rates: 9600, 115200. Use `arduino-cli monitor -p <port> -c baudrate=115200` to set a specific baud rate.

### No output from Serial.println()
**Cause**:
- Missing `Serial.begin()` in `setup()`
- Wrong baud rate
- Code crashes before reaching the print statement
**Fix**: Always call `Serial.begin(9600)` (or desired baud rate) in `setup()`. Add a small delay after `Serial.begin()` for boards that need USB enumeration time: `delay(1000)`.

## Core Installation Issues

### "No platform found matching" or "platform not found"
**Cause**: The platform core is not in the board manager index.
**Fix**: Add the appropriate board manager URL:
```
arduino-cli config set board_manager.additional_urls <URL>
arduino-cli core update-index
```
See `board-platforms.md` for the correct URL for each board family.

### "Error downloading" during core install
**Cause**: Network issues or corrupted cache.
**Fix**:
1. Retry the command
2. Clear the cache: remove `~/.arduino15/staging/` directory
3. Check internet connection
4. Run `arduino-cli core update-index` first

## macOS-Specific Issues

### Serial port naming
- Arduino boards appear as `/dev/cu.usbmodem*` or `/dev/cu.usbserial*`
- Do NOT use `/dev/tty.*` variants (they can hang)
- Bluetooth serial ports (`/dev/cu.Bluetooth*`) are not Arduino boards

### "Operation not permitted" when accessing USB
**Fix**: Go to System Settings > Privacy & Security > Files and Folders (or Full Disk Access) and grant access to your terminal application.

### Board not appearing after macOS update
**Fix**: macOS updates sometimes reset USB driver permissions or remove third-party drivers. Reinstall any needed USB-to-serial drivers (e.g., CH340 for cheap Arduino Nano clones, CP2102 for some ESP32 boards).

## Linux-Specific Issues

### ModemManager interfering with uploads
**Cause**: ModemManager probes serial devices, disrupting Arduino uploads.
**Fix**:
```
sudo systemctl stop ModemManager
sudo systemctl disable ModemManager
```
Or add a udev rule to prevent ModemManager from touching Arduino ports.
