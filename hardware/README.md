# Hardware

KiCad project for the ArduinoRemote wiring. Open `ArduinoRemote.kicad_pro` in KiCad 8 or newer.

| File | Purpose |
|------|---------|
| `ArduinoRemote.kicad_sch` | Schematic: what connects where |
| `ArduinoRemote.kicad_sym` | Project symbol library (ESP32-C3 SuperMini, 2x8 headers in physical order) |
| `sym-lib-table` | Registers that library with the project |

## Wiring

| From (SuperMini pin) | Through | To |
|---|---|---|
| GPIO0 | SW1 **Forward** push button | GND |
| GPIO1 | SW2 **Back** push button | GND |
| GPIO10 | R1 220 &Omega; &rarr; D1 LED anode | LED cathode &rarr; GND |
| USB-C | &mdash; | Power + flashing |

No pull-up resistors are needed: the firmware uses `INPUT_PULLUP`, so a pressed button reads LOW.
The LED is optional (set `LED_PIN = 255` in the firmware to drop it).

Keep buttons off GPIO2, GPIO8 and GPIO9. They are boot strapping pins: GPIO9 is the SuperMini's BOOT button and GPIO8 drives its onboard LED.

## Parts

| Ref | Part | Qty | Footprint |
|---|---|---|---|
| U1 | ESP32-C3 SuperMini | 1 | &mdash; (module on 2x8 headers) |
| SW1, SW2 | 6 mm tactile push button | 2 | `Button_Switch_THT:SW_PUSH_6mm` |
| R1 | 220 &Omega; resistor, 1/4 W | 1 | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` |
| D1 | 5 mm LED, any colour | 1 | `LED_THT:LED_D5.0mm` |

The pin numbers here must match the `CONFIG` section of `firmware/ArduinoRemote/ArduinoRemote.ino`. Change both together.
