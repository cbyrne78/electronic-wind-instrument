#include <Arduino.h>
#include <math.h>

#define AUDIO_PIN 25
#define HX_DOUT   32
#define HX_SCK    33

#define MULTI_PIN 16   // mute toggle button

//  Audio 
const float sampleRate = 22050.0f;
const float fixedFrequency = 220.0f;
float phase = 0.0f;

float audioSmooth1 = 0.0f;
float audioSmooth2 = 0.0f;
float outputLevel = 0.0f;

//  Timing 
unsigned long lastBreathPollMs = 0;
unsigned long breathPollIntervalMs = 25;
unsigned long lastBreathReadMs = 0;

//  Breath 
float breathBaseline = 0.0f;
float targetBreathAmount = 0.0f;
float smoothedBreathAmount = 0.0f;

#define BREATH_MAX_COUNTS 100000.0f
#define BREATH_MIN_COUNTS   2000.0f
#define BREATH_TIMEOUT_MS    150

//  Mute Button 
unsigned long debounceDelayMs = 25;

int multiStableState, multiRawState;
unsigned long multiLastChangeMs = 0;
bool instrumentOn = false;

//  HX710B 
long readHX710BRaw() {
  if (digitalRead(HX_DOUT) == HIGH) return 0x7FFFFFFF;

  noInterrupts();

  long value = 0;
  for (int i = 0; i < 24; i++) {
    digitalWrite(HX_SCK, HIGH);
    delayMicroseconds(1);
    value = (value << 1) | (digitalRead(HX_DOUT) & 1);
    digitalWrite(HX_SCK, LOW);
    delayMicroseconds(1);
  }

  digitalWrite(HX_SCK, HIGH);
  delayMicroseconds(1);
  digitalWrite(HX_SCK, LOW);
  delayMicroseconds(1);

  interrupts();

  if (value & 0x800000) value |= 0xFF000000;
  return value;
}

//  Baseline calibration 
void calibrateBreathBaseline() {
  long total = 0;
  int valid = 0;

  for (int i = 0; i < 100; i++) {
    unsigned long t0 = millis();
    while (digitalRead(HX_DOUT) == HIGH && millis() - t0 < 1000) {
      delay(1);
    }

    long r = readHX710BRaw();
    if (r != 0x7FFFFFFF) {
      total += r;
      valid++;
    }

    delay(10);
  }

  breathBaseline = (valid > 0) ? (float)(total / valid) : 0.0f;
  targetBreathAmount = 0.0f;
  smoothedBreathAmount = 0.0f;
  outputLevel = 0.0f;
  lastBreathReadMs = millis();

  Serial.print("Baseline = ");
  Serial.println((long)breathBaseline);
}

//  Breath update 
void updateBreath() {
  long raw = readHX710BRaw();
  if (raw == 0x7FFFFFFF) return;

  float pressure = (float)raw - breathBaseline;
  if (pressure < 0.0f) pressure = 0.0f;

  if (pressure < BREATH_MIN_COUNTS) pressure = 0.0f;
  if (pressure > BREATH_MAX_COUNTS) pressure = BREATH_MAX_COUNTS;

  targetBreathAmount = pressure / BREATH_MAX_COUNTS;
  lastBreathReadMs = millis();
}

//  Mute toggle 
void updateMultiButton() {
  int state = digitalRead(MULTI_PIN);

  if (state != multiRawState) {
    multiLastChangeMs = millis();
    multiRawState = state;
  }

  if (millis() - multiLastChangeMs > debounceDelayMs) {
    if (state != multiStableState) {
      multiStableState = state;

      if (multiStableState == LOW) {
        instrumentOn = !instrumentOn;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(HX_SCK, OUTPUT);
  pinMode(HX_DOUT, INPUT);
  digitalWrite(HX_SCK, LOW);

  pinMode(MULTI_PIN, INPUT_PULLUP);
  multiStableState = multiRawState = digitalRead(MULTI_PIN);

  Serial.println("Calibrating baseline, do not blow...");
  delay(500);
  calibrateBreathBaseline();
  Serial.println("Ready. Press button to toggle sound.");
}

void loop() {
  // update mute button
  updateMultiButton();

  // update breath
  if (millis() - lastBreathPollMs >= breathPollIntervalMs) {
    lastBreathPollMs = millis();

    if (digitalRead(HX_DOUT) == LOW) {
      updateBreath();
    }
  }

  // timeout
  if (millis() - lastBreathReadMs > BREATH_TIMEOUT_MS) {
    targetBreathAmount = 0.0f;
  }

  // smooth breath
  if (targetBreathAmount > smoothedBreathAmount)
    smoothedBreathAmount += 0.002f * (targetBreathAmount - smoothedBreathAmount);
  else
    smoothedBreathAmount += 0.010f * (targetBreathAmount - smoothedBreathAmount);

  if (smoothedBreathAmount < 0.001f) smoothedBreathAmount = 0.0f;

  // apply mute
  float targetLevel = instrumentOn ? smoothedBreathAmount : 0.0f;

  if (targetLevel > outputLevel)
    outputLevel += 0.003f * (targetLevel - outputLevel);
  else
    outputLevel += 0.020f * (targetLevel - outputLevel);

  if (outputLevel < 0.001f) outputLevel = 0.0f;

  // oscillator
  phase += (2.0f * PI * fixedFrequency / sampleRate);
  if (phase >= 2.0f * PI) phase -= 2.0f * PI;

  float sample =
    sinf(phase) +
    0.20f * sinf(2.0f * phase) +
    0.05f * sinf(3.0f * phase);

  sample *= outputLevel;

  // smoothing filter
  audioSmooth1 += 0.10f * (sample - audioSmooth1);
  audioSmooth2 += 0.06f * (audioSmooth1 - audioSmooth2);

  int dacValue = 128 + (int)(audioSmooth2 * 82.0f);
  if (dacValue < 0) dacValue = 0;
  if (dacValue > 255) dacValue = 255;

  dacWrite(AUDIO_PIN, dacValue);

  delayMicroseconds(45);
}