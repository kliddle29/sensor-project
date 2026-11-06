// sensor_prototype.ino: our Sensor Prototype. Started from the template's
// oled_demo.ino (bench.ino plus a 0.96in OLED readout).
//
// Prints "cm 42.5 ping 42.5 moisture 512 breath 3" lines at 9600 for
// sense-serial.js: cm is the cleaned-up distance, ping the raw one (0 = no echo).
// The piece names you with the charter's words. Close and breathing on the
// probe: DEVOTED, blue LED, silent. Close but not breathing, or drifting off:
// DESERTER, white LED and a slow beep, then from 50 cm a flashing red LED and
// a siren that speeds up. Past 150 cm for 5 s: NOBODY, everything quiet.
//
// One screen. On the two-colour panels the top 16 rows are yellow: that strip
// is the status (level badge, readings per second). The blue below is the data:
// the distance, big, then the breath number and whether it counts.
//
// Wiring, Arduino Nano:
//   HC-SR04  VCC -> 5V    GND -> GND    TRIG -> D9    ECHO -> D10
//   OLED     VCC -> 5V    GND -> GND    SDA  -> A4    SCL  -> A5
//   Soil     VCC -> 5V    GND -> GND    AOUT -> A0   (capacitive v2.0, our breath sensor)
//   Buzzer   +   -> D4    -   -> GND
//   LEDs     D5 -> 5k ohm -> blue long leg, D6 -> white, D7 -> red; short legs -> GND
// The OLED is an SSD1306, 128 x 64, I2C at 0x3C. Needs the U8g2 library.

#include <U8g2lib.h>
#include <Wire.h>

// Page mode: draws the screen in eight strips with a 128-byte buffer, because
// the Nano has 2 KB of RAM and a full frame would take half of it.
U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

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
const float WARN_CM = 50.0;      // past this: white -> red, and the siren starts
const float ALARM_CM = 150.0;    // past this: nobody (the browser's "present" line)
const int BREATH_DELTA = 18;     // how far "breath" must rise to count as a breath
const unsigned long BREATH_GRACE_MS = 5000;  // one breath keeps it chill this long
const unsigned long ALARM_HOLD_MS = 5000;    // must stay past ALARM_CM this long first (the charter's 5 s)
const unsigned long ALARM_MAX_MS = 60000;    // the siren gives up after this (0 = never)
// White's warning beep, before the siren.
const unsigned long FADE_BEEP_MS = 2000;     // white: one beep this often
const int FADE_BEEP_HZ = 1200;

// The four levels. Plain numbers rather than an enum, because the Arduino IDE's
// auto-generated prototypes can trip over enums. The screen shows each one by
// its charter name: CHILL is DEVOTED, FADING and WARN are DESERTER, ALARM is
// NOBODY.
const uint8_t CHILL = 0;
const uint8_t FADING = 1;
const uint8_t WARN = 2;
const uint8_t ALARM = 3;
uint8_t level = CHILL;

float smooth = 200.0;            // the distance, smoothed; starts far away

// Cleaning up the distance (filterCm). A ping that hears no echo is a miss,
// not a reading of 400 cm: misses only count as "nothing there" once there
// are MISSES_FOR_FAR in a row.
const float FAR_CM = 400.0;      // what "nothing there" counts as
const uint8_t MISSES_FOR_FAR = 5;  // about 0.8 s of misses at 6 pings a second
float recent[3];                 // the last three real readings, for the median
uint8_t recentNext = 0;
uint8_t recentCount = 0;
uint8_t misses = 0;              // no-echo pings in a row
float held = FAR_CM;             // the last cleaned-up distance
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
  // Some HC-SR04s hold ECHO high for a while after a ping that heard nothing,
  // and ignore a new trigger until it drops, so one miss turns into many.
  // Wait for ECHO to go low first.
  unsigned long waitStart = micros();
  while (digitalRead(ECHO) == HIGH && micros() - waitStart < 60000) {}
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long us = pulseIn(ECHO, HIGH, 30000);
  return us ? us / 58.0 : 0;      // 0 = no echo
}

float median3(float a, float b, float c) {
  return max(min(a, b), min(max(a, b), c));
}

float filterCm(float raw) {
  // A miss, or a reading past the sensor's range, keeps the last distance until
  // there are MISSES_FOR_FAR in a row. Real readings go through a median of the
  // last three, which drops a single wild one.
  if (raw <= 0 || raw > FAR_CM) {
    if (misses < 255) misses++;
    if (misses >= MISSES_FOR_FAR) held = FAR_CM;
    return held;
  }
  misses = 0;
  recent[recentNext] = raw;
  recentNext = (recentNext + 1) % 3;
  if (recentCount < 3) recentCount++;
  held = recentCount < 3 ? raw : median3(recent[0], recent[1], recent[2]);
  return held;
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
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_6x12_tr);
    oled.drawStr(28, 36, "hands off...");
  } while (oled.nextPage());
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
  if (level == WARN) noTone(BUZZER);
  if (next == WARN) {
    alarmSince = now;
    sirenStep = 0;
  }
  if (level == ALARM) tone(BUZZER, 2500, 60);  // someone arrived: one short chirp
  lastBeep = 0;                  // a new level beeps straight away
  level = next;
}

void updateOutputs(unsigned long now) {
  bool flash = (now / 150) % 2;  // fast flash: the siren
  digitalWrite(LED_BLUE, level == CHILL);
  digitalWrite(LED_WHITE, level == FADING);
  digitalWrite(LED_RED, level == WARN && flash);

  // White beeps slowly before the siren.
  if (level == FADING) {
    if (lastBeep == 0 || now - lastBeep >= FADE_BEEP_MS) {
      tone(BUZZER, FADE_BEEP_HZ, 80);
      lastBeep = now;
    }
    return;
  }
  // DEVOTED and NOBODY are silent; only the red level sirens.
  if (level != WARN) return;
  if (ALARM_MAX_MS != 0 && now - alarmSince >= ALARM_MAX_MS) {
    noTone(BUZZER);              // gave up; the red LED keeps flashing
    return;
  }
  // A two-tone siren that speeds up the longer they stay a deserter: each pitch
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
  const char *labels[] = {"DEVOTED", "DESERTER", "DESERTER", "NOBODY"};
  const char *label = labels[level];
  oled.setFont(u8g2_font_6x12_tr);
  int w = oled.getStrWidth(label) + 8;
  if (level == WARN) {             // the siren level: solid badge
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
  if (count & 1) oled.drawDisc(127 - rw - 6, 7, 2);
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
  oled.drawStr(0, 40, buf);
  int x = oled.getStrWidth(buf) + 2;
  oled.setFont(u8g2_font_5x8_tr);
  oled.drawStr(x, 40, "cm");
}

void drawBreath(int moisture, int breath, unsigned long now) {
  // The two bottom rows: the breath number against what it needs, the raw
  // reading, and whether a breath still counts.
  char buf[22];
  oled.setFont(u8g2_font_6x12_tr);
  snprintf(buf, sizeof buf, "breath %d / %d", breath, BREATH_DELTA);
  oled.drawStr(0, 52, buf);
  oled.drawStr(0, 63, now - lastBreath < BREATH_GRACE_MS ? "BREATHING" : "no breath");
  snprintf(buf, sizeof buf, "raw %d", moisture);
  oled.drawStr(127 - oled.getStrWidth(buf), 63, buf);
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
  // A loose OLED wire can freeze the I2C bus forever. The timeout gives up
  // after 25 ms instead, so the sensors, LEDs and buzzer keep working.
  Wire.begin();
  Wire.setWireTimeout(25000, true);
  oled.begin();
  oled.setBusClock(400000);
  Serial.println("oled ok");
  splash();
  calibrateBreath();
  lastBreath = millis();         // start chill, with the usual grace period
  Serial.println("ready");
}

void loop() {
  unsigned long now = millis();
  float ping = readCm();
  float cm = filterCm(ping);

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
  Serial.print(" ping ");
  Serial.print(ping, 1);
  Serial.print(" moisture ");
  Serial.print(moisture);
  Serial.print(" breath ");
  Serial.print(breath);
  Serial.println();

  smooth += (cm - smooth) * 0.35;

  updateLevel(smooth, now);
  updateOutputs(now);

  count++;
  if (millis() - rateStart >= 1000) {
    rate = count;
    count = 0;
    rateStart = millis();
  }

  oled.firstPage();
  do {
    drawHeader();
    drawNumber(smooth);
    drawBreath(moisture, breath, now);
  } while (oled.nextPage());
}
