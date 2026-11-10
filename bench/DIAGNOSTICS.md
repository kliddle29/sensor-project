# Diagnostics

Readings off the Nano's serial port with nobody touching the board. Each run is the sketch on `main` at that date, read at 9600 baud for about 40 seconds after the `ready` line.

## How to run one

1. Upload `bench/sensor_prototype/sensor_prototype.ino` (Tools → Processor → ATmega328P (Old Bootloader)).
2. Open the Serial Monitor at 9600, or read the port from a terminal. Only one program can hold the port.
3. Note what's in front of the ultrasonic, then leave the board alone for 40 s.
4. Count the lines (readings a second), the `ping 0.0` lines (missed echoes), and the spread of `cm`, `moisture` and `breath`.

## 2026-11-10

Sketch: `main` at `07fe6be` (after #27). Nobody at the board; things on the desk in front of the sensor.

| Check | Result | What it means |
|---|---|---|
| Startup | `boot`, `oled ok`, `ready` within 4 s of reset | The OLED answered on A4/A5 and the breath baseline was taken |
| Speed | 6.9 readings a second (one every 0.14 s, never slower than 0.16 s) | The one-OLED screen keeps the loop fast |
| Missed echoes | 0 of 284 | The HC-SR04 wiring is solid |
| `ping` | 8.4 cm average, ±0.3 cm | Steady, but **something was 8.4 cm in front of the sensor**: see #30 |
| `moisture` (raw) | 481 to 487 | About 6 counts of idle noise |
| `breath` | −1 to 0 | Idle breath sits far below the line of 18, so nothing counts as a breath by itself |

Because the sensor saw something under 12 cm with no breath, the piece named it DESERTER and beeped every 2 s with nobody there. That's the case #30 is about.
