# ArduinoRemote

ESP32-C3 SuperMini Bluetooth Low Energy remote control. Two buttons &mdash; **Forward** and **Back** &mdash; send single-character commands over BLE to a phone, tablet, or computer. The board also accepts commands written to it over BLE.

## What's in this repo

| Path | Purpose |
|------|---------|
| `firmware/ArduinoRemote/ArduinoRemote.ino` | ESP32-C3 source. Compile & upload with Arduino IDE or `arduino-cli`. |
| `docs/index.html` | GitHub Pages site &mdash; wiring diagram, flash steps, BLE protocol table. |
| `docs/controller.html` | Web Bluetooth controller with Forward / Back buttons (works on GitHub Pages over HTTPS). |
| `.github/workflows/` | CI: firmware compile check + GitHub Pages deploy of `docs/` on every push to `master`. |

## Hardware

- Board: any ESP32-C3 (tested on **MakerGO ESP32 C3 SuperMini**)
- GPIO 0 &rarr; Forward button &rarr; GND (active LOW, internal pull-up)
- GPIO 1 &rarr; Back button &rarr; GND (active LOW, internal pull-up)
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
- TX (notify): `7f4f1236-...` &mdash; sends `F`/`B` when a physical button is pressed, echoes each RX command, and reports rename result (`R` ok / `r` failed)
- RN (write): `7f4f1237-...` &mdash; host writes a new device name string to rename the BLE device. Name is stored in ESP32 Preferences (NVS) and survives reboot. Device soft-reboots after rename.

## Renaming the device

The BLE device name is stored in ESP32 Preferences (NVS), so it survives reboot. Names must be 1-31 printable ASCII characters and cannot start or end with a space.

1. Open `docs/controller.html` and tap **Connect**.
2. Type a 1&ndash;31 character printable ASCII name in the **Rename device** field.
3. Tap **Rename**. The firmware stores the name in NVS, confirms via the TX characteristic, then restarts.
4. After restart, the device advertises under the new name. Reconnect to it.

> Renaming causes a soft-reboot so the new name takes effect in BLE advertising. Any active connection will drop.

## Connection troubleshooting

1. Use Chrome or Edge on HTTPS, or open the controller from `localhost`.
2. If a renamed device is not found, flash or reset the board once, wait a few seconds, then reconnect.
3. The controller requires the `7f4f1234-...` service; remove old Bluetooth pairing or cached devices and scan again.
4. The RX characteristic accepts commands. The TX characteristic is optional for notifications, but command controls remain available if notifications are unsupported.
5. Open Serial Monitor at 115200 baud. A successful GATT connection prints `Client connected`; completed writes print `RX command: F/B/S`.

## Controller

Open `docs/controller.html` (served over HTTPS, e.g. GitHub Pages) &rarr; tap **Connect** &rarr; choose `ArduinoRemote` &rarr; press Forward / Back.

## License

MIT
