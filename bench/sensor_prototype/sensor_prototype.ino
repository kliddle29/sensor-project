// sensor_prototype.ino: our Sensor Prototype. Started from the template's
// oled_demo.ino (bench.ino plus a 0.96in OLED readout).
//
// Prints "cm 42.5 ping 42.5 moisture 512 breath 3 name nobody" lines at 9600
// for sense-serial.js: cm is the cleaned-up distance, ping the raw one (0 = no
// echo), name what the piece calls you right now.
//
// Don't Leave. The piece names who it noticed, from SYSTEM_CHARTER.md and the
// README's State Table:
//   devoted   breath over 25, held 3 s:   blue LED steady, silent, OLED DEVOTED
//   deserter  cm 50 to 150, held 2 s:     red LED flashes, siren, OLED DESERTER
//   nobody    cm over 150, held 5 s:      everything off, one dot blinks every 5 s
// Rows are checked top to bottom; the first one true for its whole hold wins.
// When no row is true, it keeps the last name. It starts as nobody.
//
// Wiring, Arduino Nano:
//   HC-SR04  VCC -> 5V    GND -> GND    TRIG -> D9    ECHO -> D10
//   OLED     VCC -> 5V    GND -> GND    SDA  -> A4    SCL  -> A5
//   Soil     VCC -> 5V    GND -> GND    AOUT -> A0   (capacitive v2.0, our breath sensor)
//   Buzzer   +   -> D4    -   -> GND
//   LEDs     D5 -> 5k ohm -> blue long leg, D6 -> white, D7 -> red; short legs -> GND
//            (the charter doesn't use white yet)
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

// ---- The State Table, from the README. Change these from what the Serial
// Monitor shows, and log every change in the README break log. ----------------
// LINE is where a name starts. GAP is how far back past the LINE a reading must
// go before the piece lets go of that name, so a reading wobbling on the LINE
// doesn't flicker between names.
const int DEVOTED_LINE = 25;     // breath over this
const int DEVOTED_GAP = 3;
const unsigned long DEVOTED_HOLD_MS = 3000;
const float DESERTER_LINE = 50;  // cm from here up to NOBODY_LINE
const float DESERTER_GAP = 5;
const unsigned long DESERTER_HOLD_MS = 2000;
const float NOBODY_LINE = 150;   // cm over this
const float NOBODY_GAP = 10;
const unsigned long NOBODY_HOLD_MS = 5000;
const unsigned long SIREN_MAX_MS = 60000;    // the siren gives up after this (0 = never)

// The names, in the order the rows are checked. Plain numbers rather than an
// enum, because the Arduino IDE's auto-generated prototypes can trip over enums.
const uint8_t DEVOTED = 0;
const uint8_t DESERTER = 1;
const uint8_t NOBODY = 2;
const uint8_t NONE = 255;
const char *NAMES[] = {"devoted", "deserter", "nobody"};
uint8_t name = NOBODY;
unsigned long nameSince = 0;     // when the current name started
uint8_t candidate = NONE;        // a name waiting out its hold
unsigned long candidateSince = 0;

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
float moistSmooth = 0;           // the moisture reading, smoothed
float baseline = 0;              // what the sensor reads with nobody breathing on it
unsigned long sirenStep = 0;     // when the siren last changed pitch
bool sirenHigh = false;

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

bool rowTrue(uint8_t row, float cm, int breath) {
  // Each row's rule. The name the piece already has gets its GAP, so it holds on
  // a little past its LINE before letting go.
  bool current = row == name;
  if (row == DEVOTED) {
    return breath > DEVOTED_LINE - (current ? DEVOTED_GAP : 0);
  }
  if (row == DESERTER) {
    float low = DESERTER_LINE - (current ? DESERTER_GAP : 0);
    float high = NOBODY_LINE + (current ? DESERTER_GAP : 0);
    return cm >= low && cm <= high;
  }
  return cm > NOBODY_LINE - (current ? NOBODY_GAP : 0);
}

unsigned long holdFor(uint8_t row) {
  if (row == DEVOTED) return DEVOTED_HOLD_MS;
  if (row == DESERTER) return DESERTER_HOLD_MS;
  return NOBODY_HOLD_MS;
}

void updateName(float cm, int breath, unsigned long now) {
  // The first row that's true is the candidate. It becomes the name once it has
  // stayed the first true row for its whole hold. No true row keeps the name.
  uint8_t winner = NONE;
  for (uint8_t row = DEVOTED; row <= NOBODY; row++) {
    if (rowTrue(row, cm, breath)) {
      winner = row;
      break;
    }
  }
  if (winner == NONE || winner == name) {
    candidate = NONE;
    return;
  }
  if (winner != candidate) {
    candidate = winner;
    candidateSince = now;
  }
  if (now - candidateSince < holdFor(winner)) return;

  if (name == DESERTER) noTone(BUZZER);
  if (winner == DESERTER) sirenStep = 0;
  name = winner;
  nameSince = now;
  candidate = NONE;
}

void updateOutputs(unsigned long now) {
  bool flash = (now / 150) % 2;
  digitalWrite(LED_BLUE, name == DEVOTED);
  digitalWrite(LED_WHITE, LOW);
  digitalWrite(LED_RED, name == DESERTER && flash);

  if (name != DESERTER) return;
  unsigned long away = now - nameSince;
  if (SIREN_MAX_MS != 0 && away >= SIREN_MAX_MS) {
    noTone(BUZZER);              // gave up; the red LED keeps flashing
    return;
  }
  // A two-tone siren that speeds up the longer they stay a deserter: each pitch
  // lasts 300 ms at first, down to 60 ms after 12 seconds.
  unsigned long step = 300 - min(away, 12000UL) / 50;
  if (sirenStep == 0 || now - sirenStep >= step) {
    sirenHigh = !sirenHigh;
    tone(BUZZER, sirenHigh ? 3000 : 1800);
    sirenStep = now;
  }
}

void drawName(float cm, int breath, unsigned long now) {
  if (name == NOBODY) {
    // Blank but for one dot that blinks every 5 s: it waits. The dot is big and
    // stays lit for most of a second, so the screen doesn't look broken.
    if ((now - nameSince) % 5000 < 800) oled.drawDisc(64, 32, 6);
    return;
  }
  const char *word = name == DEVOTED ? "DEVOTED" : "DESERTER";
  oled.setFont(u8g2_font_logisoso18_tr);
  oled.drawStr((128 - oled.getStrWidth(word)) / 2, 38, word);
  // The numbers it named you from, small, along the bottom.
  char buf[24];
  oled.setFont(u8g2_font_5x8_tr);
  if (cm >= FAR_CM - 1) snprintf(buf, sizeof buf, "cm ---  breath %d", breath);
  else snprintf(buf, sizeof buf, "cm %d  breath %d", (int)cm, breath);
  oled.drawStr((128 - oled.getStrWidth(buf)) / 2, 62, buf);
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
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_WHITE, LOW);
  digitalWrite(LED_RED, LOW);
  nameSince = millis();          // it starts as nobody
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

  Serial.print("cm ");
  Serial.print(cm, 1);
  Serial.print(" ping ");
  Serial.print(ping, 1);
  Serial.print(" moisture ");
  Serial.print(moisture);
  Serial.print(" breath ");
  Serial.print(breath);
  Serial.print(" name ");
  Serial.print(NAMES[name]);
  Serial.println();

  smooth += (cm - smooth) * 0.35;

  updateName(smooth, breath, now);
  updateOutputs(now);

  oled.firstPage();
  do {
    drawName(smooth, breath, now);
  } while (oled.nextPage());
}
