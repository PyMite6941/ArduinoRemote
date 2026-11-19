# ArduinoRemote

ESP32-C3 SuperMini Bluetooth Low Energy remote control. Two buttons &mdash; **Forward** and **Back** &mdash; send single-character commands over BLE to a phone, tablet, or computer. The board also accepts commands written to it over BLE.

## What's in this repo

| Path | Purpose |
|------|---------|
| `firmware/ArduinoRemote/ArduinoRemote.ino` | ESP32-C3 source. Compile & upload with Arduino IDE or `arduino-cli`. |
| `docs/index.html` | GitHub Pages site &mdash; wiring diagram, flash steps, BLE protocol table. |
| `docs/controller.html` | Web Bluetooth controller with Forward / Back buttons (works on GitHub Pages over HTTPS). |
| `.github/workflows/` | CI: compile check on every push. |

## Hardware

- Board: any ESP32-C3 (tested on **MakerGO ESP32 C3 SuperMini**)
- GPIO 8 &rarr; Forward button &rarr; GND (active LOW, internal pull-up)
- GPIO 9 &rarr; Back button &rarr; GND (active LOW, internal pull-up)
- GPIO 10 &rarr; LED &rarr; 220&Omega; &rarr; GND (optional; set `LED_PIN = 255` to skip)

## Flash

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli compile --fqbn esp32:esp32:makergo_c3_supermini firmware/ArduinoRemote
arduino-cli upload -p COM3 --fqbn esp32:esp32:makergo_c3_supermini firmware/ArduinoRemote
```

Replace `COM3` with your serial port. Or open the `.ino` in the Arduino IDE, pick your ESP32-C3 board and port, and click Upload.

## BLE protocol

- Service: `7f4f1234-1234-1234-1234-123456789abc`
- RX (write): `7f4f1235-...` &mdash; host sends `F` (forward), `B` (back), `S` (stop)
- TX (notify): `7f4f1236-...` &mdash; ESP32 echoes each command back

## Controller

Open `docs/controller.html` (served over HTTPS, e.g. GitHub Pages) &rarr; tap **Connect** &rarrow; choose `ArduinoRemote` &rarr; press Forward / Back.

## License

MIT
