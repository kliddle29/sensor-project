// sensor_prototype.ino: our Sensor Prototype. Started from the template's
// oled_demo.ino (bench.ino plus a 0.96in OLED readout).
//
// Prints "cm 42.5 moisture 512 breath 3" lines at 9600 for sense-serial.js.
// The piece wants attention: breathe on the moisture sensor and stay close and
// it is chill. Walk away and stop breathing on it and three LEDs count down
// (blue, white, red) and the beeper gets more intense. Come back and it calms down.
//
// Two screens. The first is the ultrasonic's: on the two-colour panels the top
// 16 rows are yellow: that strip is the status (level badge, readings per
// second). The blue below is the data: the distance, a sonar ping whose blip
// sits at the measured range, and a scrolling trace of the history. The second
// is the moisture sensor's: the breath number, big, and whether it counts.
//
// Wiring, Arduino Nano:
//   HC-SR04  VCC -> 5V    GND -> GND    TRIG -> D9    ECHO -> D10
//   OLED 1   VCC -> 5V    GND -> GND    SDA  -> D12   SCL  -> D11  (distance)
//   OLED 2   VCC -> 5V    GND -> GND    SDA  -> A1    SCL  -> A2   (breath)
//   Soil     VCC -> 5V    GND -> GND    AOUT -> A0   (capacitive v2.0, our breath sensor)
//   Buzzer   +   -> D4    -   -> GND
//   LEDs     D5 -> 220 ohm -> blue long leg, D6 -> white, D7 -> red; short legs -> GND
// The OLEDs are SSD1306, 128 x 64, I2C at 0x3C. Needs the U8g2 library.

#include <U8g2lib.h>

// Both screens answer at 0x3C, so each gets its own bus. Both are software
// I2C (the Nano's hardware bus is only on A4/A5), which cannot freeze on a
// loose wire but is slower: a full frame on the first screen takes a while.

// Page mode: draws the screen in eight strips with a 128-byte buffer, because
// the Nano has 2 KB of RAM and a full frame would take half of it.
U8G2_SSD1306_128X64_NONAME_1_SW_I2C oled(U8G2_R0, /* clock (SCL) = */ 11, /* data (SDA) = */ 12, U8X8_PIN_NONE);

// The second screen, on A1/A2 (used as digital pins). Text-only U8x8 mode:
// no frame buffer, and it only sends the lines we rewrite, so it is quick.
U8X8_SSD1306_128X64_NONAME_SW_I2C breathScreen(/* clock (SCL) = */ A2, /* data (SDA) = */ A1, U8X8_PIN_NONE);
const unsigned long BREATH_SCREEN_MS = 250;  // redraw it four times a second
unsigned long breathScreenAt = 0;

const int TRIG = 9;
const int ECHO = 10;
const int MOISTURE = A0;         // soil sensor AOUT (the yellow wire)
const int BUZZER = 4;            // buzzer + leg (the longer one)
const int LED_BLUE = 5;
const int LED_WHITE = 6;
const int LED_RED = 7;

// ---- Tuning: change these from what the Serial Monitor shows, and log every
// change in the README break log. ----------------------------------------------
const float FADE_CM = 12.0;      // past this, or with no breath: blue -> white
const float WARN_CM = 50.0;      // past this: white -> red
const float ALARM_CM = 150.0;    // past this: red -> alarm (the browser's "present" line)
const int BREATH_DELTA = 25;     // how far "breath" must rise to count as a breath
const unsigned long BREATH_GRACE_MS = 5000;  // one breath keeps it chill this long
const unsigned long ALARM_HOLD_MS = 1000;    // must stay past ALARM_CM this long first
const unsigned long ALARM_MAX_MS = 60000;    // buzzer gives up after this (0 = never)
// The warning beeps before the alarm: white is slow and low, red is fast and high.
const unsigned long FADE_BEEP_MS = 2000;     // white: one beep this often
const int FADE_BEEP_HZ = 1200;
const unsigned long WARN_BEEP_MS = 700;      // red: one beep this often
const int WARN_BEEP_HZ = 2200;

// The four levels, worst last. Plain numbers rather than an enum, because the
// Arduino IDE's auto-generated prototypes can trip over enums.
const uint8_t CHILL = 0;
const uint8_t FADING = 1;
const uint8_t WARN = 2;
const uint8_t ALARM = 3;
uint8_t level = CHILL;

const float NEAR_CM = 30.0;      // blip size on the sonar, screen only
const float MAX_CM = 200.0;      // top of the graph and the sonar range

const int GRAPH_H = 10;          // the trace, rows 53 to 63
uint8_t hist[128];               // one column per reading, 0..GRAPH_H
uint8_t head = 0;

float smooth = MAX_CM;
float sweep = 0;                 // sonar sweep angle, degrees
int sweepDir = 1;
uint8_t ping = 0;                // expanding ring radius
uint8_t rate = 0;                // readings in the last second, for the header
uint8_t count = 0;
unsigned long rateStart = 0;

float moistSmooth = 0;           // the moisture reading, smoothed
float baseline = 0;              // what the sensor reads with nobody breathing on it
unsigned long lastBreath = 0;    // millis() of the last breath
unsigned long farSince = 0;      // when they went past ALARM_CM; 0 = not past it
unsigned long alarmSince = 0;    // when the alarm started
unsigned long sirenStep = 0;     // when the siren last changed pitch
bool sirenHigh = false;
unsigned long lastBeep = 0;      // when the last warning beep started; 0 = beep now

float readCm() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long us = pulseIn(ECHO, HIGH, 30000);
  return us ? us / 58.0 : 400.0;
}

void splash() {
  for (uint8_t r = 2; r < 70; r += 6) {
    oled.firstPage();
    do {
      oled.drawCircle(64, 32, r);
      if (r > 8) oled.drawCircle(64, 32, r - 8);
      oled.setFont(u8g2_font_6x12_tr);
      oled.drawStr(28, 36, "SENSOR LAYER");
    } while (oled.nextPage());
  }
}

void calibrateBreath() {
  // Average the sensor for about a second while nobody breathes on it. All
  // three LEDs stay on meanwhile, which also proves they are wired.
  digitalWrite(LED_BLUE, HIGH);
  digitalWrite(LED_WHITE, HIGH);
  digitalWrite(LED_RED, HIGH);
  long sum = 0;
  for (int i = 0; i < 50; i++) {
    sum += analogRead(MOISTURE);
    delay(20);
  }
  baseline = sum / 50.0;
  moistSmooth = baseline;
}

void updateLevel(float cm, unsigned long now) {
  // Only breath AND closeness together keep it chill (and silent). Missing
  // either one is at least FADING; past that, the further away, the worse.
  bool breathedRecently = now - lastBreath < BREATH_GRACE_MS;
  uint8_t next;
  if (breathedRecently && cm < FADE_CM) next = CHILL;
  else if (cm < WARN_CM) next = FADING;
  else if (cm < ALARM_CM) next = WARN;
  else {
    // One bad echo can read as far away, so wait ALARM_HOLD_MS before the alarm.
    if (farSince == 0) farSince = now;
    next = now - farSince >= ALARM_HOLD_MS ? ALARM : WARN;
  }
  if (cm < ALARM_CM) farSince = 0;

  if (next == level) return;
  if (next == ALARM) {
    alarmSince = now;
    sirenStep = 0;
  }
  if (level == ALARM) {
    noTone(BUZZER);
    tone(BUZZER, 2500, 60);      // they came back: one short chirp
  }
  lastBeep = 0;                  // a new level beeps straight away
  level = next;
}

void updateOutputs(unsigned long now) {
  bool blink = (now / 400) % 2;  // slow blink: the warning
  bool flash = (now / 150) % 2;  // fast flash: the alarm
  digitalWrite(LED_BLUE, level == CHILL || (level == ALARM && flash));
  digitalWrite(LED_WHITE, level == FADING || (level == ALARM && flash));
  digitalWrite(LED_RED, (level == WARN && blink) || (level == ALARM && !flash));

  // White and red beep before the alarm, so the sound steps up with the LEDs.
  if (level == FADING || level == WARN) {
    unsigned long every = level == FADING ? FADE_BEEP_MS : WARN_BEEP_MS;
    if (lastBeep == 0 || now - lastBeep >= every) {
      tone(BUZZER, level == FADING ? FADE_BEEP_HZ : WARN_BEEP_HZ, 80);
      lastBeep = now;
    }
    return;
  }
  if (level != ALARM) return;
  if (ALARM_MAX_MS != 0 && now - alarmSince >= ALARM_MAX_MS) {
    noTone(BUZZER);              // gave up; the LEDs keep flashing
    return;
  }
  // A two-tone siren that speeds up the longer they stay away: each pitch
  // lasts 300 ms at first, down to 60 ms after 12 seconds.
  unsigned long step = 300 - min(now - alarmSince, 12000UL) / 50;
  if (sirenStep == 0 || now - sirenStep >= step) {
    sirenHigh = !sirenHigh;
    tone(BUZZER, sirenHigh ? 3000 : 1800);
    sirenStep = now;
  }
}

void drawHeader() {
  // Rows 0-15, the yellow strip: level badge left, readings per second right.
  const char *labels[] = {"CHILL", "FADING", "WARN", "ALARM"};
  const char *label = labels[level];
  oled.setFont(u8g2_font_6x12_tr);
  int w = oled.getStrWidth(label) + 8;
  if (level >= WARN) {             // warn and alarm: solid badge
    oled.drawRBox(0, 1, w, 13, 3);
    oled.setDrawColor(0);
    oled.drawStr(4, 11, label);
    oled.setDrawColor(1);
  } else {
    oled.drawRFrame(0, 1, w, 13, 3);
    oled.drawStr(4, 11, label);
  }
  char buf[8];
  oled.setFont(u8g2_font_5x8_tr);
  itoa(rate, buf, 10);
  strcat(buf, "/s");
  int rw = oled.getStrWidth(buf);
  oled.drawStr(127 - rw, 10, buf);
  // a pulse that blinks once per reading, beside the rate
  if (head & 1) oled.drawDisc(127 - rw - 6, 7, 2);
  else oled.drawCircle(127 - rw - 6, 7, 2);
}

void drawNumber(float cm) {
  char buf[8];
  oled.setFont(u8g2_font_logisoso22_tn);
  // The smoothed value creeps toward 400 (no echo) but never reaches it.
  if (cm >= 399.0) {
    strcpy(buf, "---");
  } else {
    dtostrf(cm, 0, cm < 100 ? 1 : 0, buf);
  }
  oled.drawStr(0, 45, buf);
  int x = oled.getStrWidth(buf) + 2;
  oled.setFont(u8g2_font_5x8_tr);
  oled.drawStr(x, 45, "cm");
}

void drawSonar(float cm) {
  const int ox = 99, oy = 50, R = 28;
  // range rings and the frame of the fan
  oled.drawCircle(ox, oy, R / 3, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  oled.drawCircle(ox, oy, 2 * R / 3, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  oled.drawCircle(ox, oy, R, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  // the outgoing ping
  if (ping < R) oled.drawCircle(ox, oy, ping, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  // the sweep line
  float a = (sweep - 90) * DEG_TO_RAD;
  oled.drawLine(ox, oy, ox + R * cos(a), oy + R * sin(a));
  // the blip, at the measured range along the sweep
  if (cm < MAX_CM) {
    float r = cm / MAX_CM * R;
    int bx = ox + r * cos(a), by = oy + r * sin(a);
    oled.drawDisc(bx, by, cm < NEAR_CM ? 3 : 2);
  }
}

void drawGraph() {
  // A trace, oldest at the left: a line reads cleaner than solid bars, on the
  // panel and in a photo of it.
  const int y0 = 63;
  for (uint8_t x = 1; x < 128; x++) {
    uint8_t a = hist[(head + x - 1) % 128];
    uint8_t b = hist[(head + x) % 128];
    oled.drawLine(x - 1, y0 - a, x, y0 - b);
  }
}

void drawBreathScreen(int moisture, int breath, unsigned long now) {
  // Text mode writes over what was there, so every line is padded with spaces
  // to cover the old, longer value. 16 characters fill a line.
  char buf[17];
  breathScreen.setFont(u8x8_font_chroma48medium8_r);
  breathScreen.drawString(0, 0, "BREATH");
  snprintf(buf, sizeof buf, "%4d", breath);
  breathScreen.draw2x2String(0, 2, buf);     // rows 2-3, big
  snprintf(buf, sizeof buf, "raw %4d need>%-2d", moisture, BREATH_DELTA);
  breathScreen.drawString(0, 5, buf);
  bool breathing = now - lastBreath < BREATH_GRACE_MS;
  breathScreen.drawString(0, 7, breathing ? "BREATHING       " : "no breath       ");
}

void setup() {
  Serial.begin(9600);            // sense-serial.js opens the port at this speed
  Serial.println("boot");        // checkpoints: the last one printed shows where it stopped
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_WHITE, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  tone(BUZZER, 2000, 150);       // startup beep: proves the buzzer is wired
  oled.begin();
  Serial.println("oled ok");
  breathScreen.begin();
  breathScreen.setFont(u8x8_font_chroma48medium8_r);
  breathScreen.drawString(0, 0, "BREATH");
  breathScreen.drawString(0, 7, "hands off...");
  Serial.println("screen 2 ok");
  splash();
  calibrateBreath();
  lastBreath = millis();         // start chill, with the usual grace period
  Serial.println("ready");
}

void loop() {
  unsigned long now = millis();
  float cm = readCm();

  // Breath makes the sensor read lower (damper), so "breath" is how far below
  // its baseline it sits. The baseline follows slow drift, like the room's
  // humidity, but is far too slow to follow a breath.
  int moisture = analogRead(MOISTURE);   // 0-1023; lower = wetter
  moistSmooth += (moisture - moistSmooth) * 0.3;
  baseline += (moistSmooth - baseline) * 0.001;
  int breath = (int)(baseline - moistSmooth);
  if (breath > BREATH_DELTA) lastBreath = now;

  Serial.print("cm ");
  Serial.print(cm, 1);
  Serial.print(" moisture ");
  Serial.print(moisture);
  Serial.print(" breath ");
  Serial.print(breath);
  Serial.println();

  smooth += (min(cm, 400.0f) - smooth) * 0.35;

  updateLevel(smooth, now);
  updateOutputs(now);

  if (now - breathScreenAt >= BREATH_SCREEN_MS) {
    drawBreathScreen(moisture, breath, now);
    breathScreenAt = now;
  }

  count++;
  if (millis() - rateStart >= 1000) {
    rate = count;
    count = 0;
    rateStart = millis();
  }

  // closer reads taller in the graph
  float c = constrain(smooth, 0, MAX_CM);
  hist[head] = (uint8_t)((1.0 - c / MAX_CM) * GRAPH_H);
  head = (head + 1) % 128;

  sweep += 6 * sweepDir;
  if (sweep > 60 || sweep < -60) sweepDir = -sweepDir;
  ping = (ping + 3) % 36;

  oled.firstPage();
  do {
    drawHeader();
    drawNumber(smooth);
    drawSonar(smooth);
    drawGraph();
  } while (oled.nextPage());
}
