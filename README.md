# Sensor Layer starter

Computer Coding Studio, fall 2026. The camera, the model, one worked signal and a fake sensor, so your pair starts from a piece that already runs.

**Live:** https://flyinthelyceum.github.io/sensor-layer-starter/ (click Start the camera; press D, B, V)

## Make it yours

1. Click **Use this template**, then **Create a new repository**, under one partner's account. Add the other partner as a collaborator (Settings, Collaborators).
2. Both partners clone it, open it in VS Code, and run it with a local server. Live Server works, or in the terminal: `python3 -m http.server`, then open `http://localhost:8000`. Opening `index.html` straight from the Finder will not reach the camera.
3. Turn on GitHub Pages: Settings, Pages, branch `main`, folder `/ (root)`. That address is the live URL you submit.

## The files

| File | Job | Who edits it |
|---|---|---|
| `table.js` | Your State Table, row for row: the signals, the states, the rules, the holds | both of you, first |
| `sense.js` | Reads the camera, turns readings into signals, applies the table, writes `window.sense` | each partner, your own signal |
| `piece.js` | Reads `window.sense` every frame and answers | both of you |
| `fake-sense.js` | Writes the same object with no camera: number keys set the state, the mouse sets the signals | only if your signal names change |
| `index.html` | Loads them in order | rarely |

## The object

```js
window.sense = {
  state: 'here',                          // one word from your State Table
  signals: { present: 0.98, open: 0.02 }, // each a number from 0 to 1
  since: 184203.5                         // performance.now() when this state began
};
```

`piece.js` reads this and nothing else. It never touches the camera. That is why the piece keeps running on `fake-sense.js` when no camera is there, and why a scene you build in November can run off the sensor you build now.

## Adding your signal

1. Add its name to `signals` in `table.js`.
2. Add a function with the same name to `SIGNALS` in `sense.js`. It gets one frame's result and returns a number from 0 to 1. There are two patterns in the comments there: one of the 52 named scores from the lab, or geometry from the 478 points.
3. Use it in a rule in `table.js`. Press **D** to watch the number while you set the threshold. Thresholds that move go in your break log.

## Keys

- **D**: the object `piece.js` reads, live
- **B**: all 52 named scores, to find the one you measured in the Make a Face lab
- **V**: the camera, small, in the corner
- **1, 2, 3 …** and the **mouse**: drive the fake sensor before the camera starts

## When Start does nothing useful

If the readout says the face library did not load, the school network is blocking `cdn.jsdelivr.net`. Press **D** and keep going on the fake sensor: the piece reads the same object either way. A new state you add to `table.js` gets a bright colour of its own in `piece.js` until you give it one.

## Bench pairs: an Arduino instead of the camera

The bench track swaps the sense layer and nothing else. `piece.js` and `fake-sense.js` stay exactly as they are. The wiring, a photo of a working build and upload fixes are in `bench/README.md`.

1. Build the circuit and flash `bench/sensor_prototype/sensor_prototype.ino` from the Arduino IDE (our Nano only uploads with **Tools → Processor → ATmega328P (Old Bootloader)**). It prints one line per reading, like `cm 42.5 ping 42.5 moisture 481 breath 0`. Check it in the IDE's Serial Monitor at **9600**, then **close the Serial Monitor**: only one program can hold the port.
2. Open **`bench.html`** instead of `index.html`, on localhost in **Chrome**. It loads the bench table and the serial sense layer; nothing to edit.
3. Click **Connect the Arduino** and pick the board. Press **D** to see the raw line and your signals.
4. Your State Table lives in **`bench/table-bench.js`** (same format as `table.js`). Your signal is a function in `SIGNALS` in `bench/sense-serial.js`: it gets the latest reading (`{ cm: 42.5 }`) and returns 0 to 1. Add the sensor to the sketch's printed line, then its function, then its name in `bench/table-bench.js`.

## What stays in the browser

Nothing saves a frame or a landmark list. The model downloads once when you click Start; after that the Network panel stays quiet. Keep it that way: no `localStorage` of readings, no uploads, no screenshots of the camera image in the repo. Screenshots for your break log show the piece or the D readout.

The instrument is MediaPipe Face Landmarker, pinned to version 1.1.1, the same one the Make a Face lab used.

## Our build: Don't Leave

An Arduino Nano with an HC-SR04 ultrasonic sensor, a capacitive moisture sensor used as a breath probe, one 0.96 in OLED, a buzzer and three LEDs. The sketch is `bench/sensor_prototype/sensor_prototype.ino`.

### Wiring

| Part | Pin | Nano |
|---|---|---|
| HC-SR04 | VCC / GND | 5V / GND |
| | TRIG | D9 |
| | ECHO | D10 |
| OLED (SSD1306, I2C) | VCC / GND | 5V / GND |
| | SDA | A4 |
| | SCL | A5 |
| Breath probe (capacitive moisture v2.0) | VCC / GND | **5V** / GND (it reads 0 on 3.3 V) |
| | AOUT | A0 |
| Buzzer | + / − | D4 / GND |
| Blue LED | long leg through a resistor | D5 |
| White LED | long leg through a resistor | D6 |
| Red LED | long leg through a resistor | D7 |
| LEDs | short legs | GND |

Keep RX0, TX1 and RST empty: a wire there stops uploads. The hand-drawn version is `design-wiring.jpg` (its buzzer + and − are swapped; the table is right).

### How to run it

1. Open `bench/sensor_prototype/sensor_prototype.ino` in the Arduino IDE. It needs the **U8g2** library.
2. **Tools → Board → Arduino Nano**, **Tools → Processor → ATmega328P (Old Bootloader)**, and pick the Nano's port.
3. Upload. All three LEDs light for about a second and the screen says "hands off...": keep away from the breath probe while it learns its baseline.
4. Open the Serial Monitor at **9600**. Each line reads like `cm 42.5 ping 42.5 moisture 481 breath 0 name deserter`.
5. Close the Serial Monitor before opening `bench.html`; only one program can hold the port.

What you should see:

| Screen badge | When | LEDs | Sound |
|---|---|---|---|
| DEVOTED | under 12 cm and breath over 18 | blue | silent |
| DESERTER | close without breath, or 12–50 cm | white | a beep every 2 s |
| DESERTER | 50–150 cm | red flashing | siren that speeds up (stops after 60 s) |
| NOBODY | past 150 cm for 5 s | off | silent; one chirp when someone arrives |

## State Table

Rows are checked top to bottom, and the first one true for its whole hold wins.

| Name | Readings at the edge | LINE | GAP | Hold | Visitor learns it by | Piece does | For them / To them | Owner |
|---|---|---|---|---|---|---|---|---|
| devoted | 24, 25, 27 breath | 18 | 3 | 3 s | the word DEVOTED on the OLED, with the blue LED | D5 blue steady; buzzer silent; OLED prints DEVOTED | For them: it goes calm and quiet the moment you give it your breath. To them: it only counts breath, so someone with asthma or in a mask can never be devoted. | @BrysonAI30 |
| deserter | 49, 48, 46 cm | 50 | 5 | 2 s | the word DESERTER on the OLED, with the red LED flashing and the siren | D7 red flashes; D4 siren speeds up; OLED prints DESERTER | For them: it tells you, out loud, that it noticed you drifting. To them: it calls stepping back to talk to a friend desertion, and sirens at you for it. | @kliddle29 |
| nobody | 213.9, 214.8, 213.2 cm (empty table, Nov 2, before the rewire) | 150 | 10 | 5 s | the screen goes blank but for one blinking dot | D5, D6, D7 off; buzzer silent; OLED blank but for a dot every 5 s | For them: it rests instead of shouting at an empty room. To them: it can't tell someone who left from someone standing quietly just past 150 cm. | @kliddle29 @BrysonAI30 |

## For them, to them

- **For them:** it notices you and says so, out loud and on the screen, the moment you drift away, so you always know where you stand with it.
- **To them:** it only calls you devoted if you lean in close and breathe on it, so anyone who can't, like someone with asthma, in a mask, or in a wheelchair, is named a deserter.

## Lineage

- **LAUREN** (Lauren Lee McCarthy, 2017): an artist acts as a live smart speaker in a stranger's house, watching through cameras to serve them. Ours turns that around: it watches you to make you serve it. [See the project](https://get-lauren.net/LAUREN)

## Break log

What broke, and what we changed because of it. Threshold changes go here too.

| Date | Commit | What broke | What changed |
|---|---|---|---|
| 2026-10-29 | `6496e7c` | The page read nothing: the sketch was at 9600, the page at 115200 | `bench/sense-serial.js` reads at 9600 |
| 2026-10-29 | `52a1141` | A loose OLED wire froze the whole Nano; the moisture sensor read 0 on 3.3 V | Added a Wire timeout; moved the moisture sensor to 5V; added breath, LEDs, buzzer and a second screen |
| 2026-10-29 | `1b38a15` | Too easy to stay quiet from across the desk | Breath line 10 → 30; blue under 12 cm, white 12–50, red past 50 |
| 2026-10-29 | `220d29f` | It stayed quiet from one sensor alone | Quiet now needs breath **and** under 12 cm |
| 2026-10-29 | `eeca326` | The breath screen stayed black on D2/D3 | Breath screen to A1/A2, distance screen to D11/D12 (which made it slow) |
| 2026-10-30 | `e7d0cb1` | A breath of 30 was too hard to reach | Breath line 30 → 25 |
| 2026-11-02 | `5a58b36` | A commit on protected `main` couldn't be pushed and included `.DS_Store` | Moved the edits to a branch; added `.gitignore` |
| 2026-11-03 | `50998a2` | Two software-I2C screens slowed the Nano to about 1 reading a second | One OLED back on A4/A5 with distance and breath together |
| 2026-11-06 | `e1fc065` | The distance jumped: up to 90% of pings missed and counted as 400 cm | Misses ignored until 5 in a row, median of 3 drops spikes; rewired the HC-SR04 |
| 2026-11-06 | `6bd9713` | The sketch still used the old four levels, not the charter's names | Rewrote it around devoted / deserter / nobody |
| 2026-11-06 | `895784d` | That screen went blank most of the time; breath 25 was too hard to reach | Brought back the one-OLED screen; breath line 25 → 18 |
| 2026-11-06 | `1d2d7ff` | The screen said CHILL / WARN, not the charter's names, and nobody sirened | Badge shows DEVOTED / DESERTER / NOBODY; nobody is quiet; only red flashes during the siren |

## Credits

- **Kenzo Liddle** (@kliddle29): the `near` signal and the **deserter** name; the charter's first draft and `design.jpg`
- **Bryson England** (@BrysonAI30): the `present` signal and the **devoted** name; the breath probe, OLED and LED wiring
- **Both:** the **nobody** name, the State Table and the hand-drawn `design-wiring.jpg`
- Started from the class's `sensor-layer-starter` template; the OLED sketch began as its `oled_demo.ino`
- Repo: https://github.com/kliddle29/sensor-project
