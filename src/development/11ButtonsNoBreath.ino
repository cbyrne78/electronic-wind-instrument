#include <Arduino.h>
#include <math.h>

const int numMainKeys = 6;
const int mainKeyPins[numMainKeys] = {14, 27, 26, 13, 18, 19};

#define FLAT_PIN        21
#define SHARP_PIN       22
#define OCTAVE_UP_PIN   23
#define OCTAVE_DOWN_PIN 17
#define MULTI_PIN       16
#define AUDIO_PIN       25

bool currentMainFingering[numMainKeys] = {false, false, false, false, false, false};
bool flatPressed = false;
bool sharpPressed = false;
bool octaveUpPressed = false;
bool octaveDownPressed = false;

bool instrumentEnabled = false;

// multi button debounce
int multiRawState = HIGH;
int multiStableState = HIGH;
unsigned long multiLastChangeMs = 0;
const unsigned long multiDebounceMs = 40;
bool multiHandledPress = false;

// audio
const float sampleRate = 22050.0f;
float phase = 0.0f;
float smooth1 = 0.0f;
float smooth2 = 0.0f;

float currentFreq = 277.18f;
float targetFreq  = 277.18f;

float getFreqFromFingering() {
  bool k1 = currentMainFingering[0];
  bool k2 = currentMainFingering[1];
  bool k3 = currentMainFingering[2];
  bool k4 = currentMainFingering[3];
  bool k5 = currentMainFingering[4];
  bool k6 = currentMainFingering[5];

  float freq = 277.18f; // default C#

  // check most specific combinations first
  if (k1 && k2 && k3 && k4 && k5 && k6) freq = 146.83f;            // D
  else if (k1 && k2 && k3 && k4 && k5 && !k6) freq = 164.81f;      // E
  else if (k1 && k2 && k3 && !k4 && k5 && !k6) freq = 185.00f;     // F#
  else if (k1 && k2 && k3 && k4 && !k5 && !k6) freq = 174.61f;     // F
  else if (k1 && k2 && k3 && !k4 && !k5 && !k6) freq = 196.00f;    // G
  else if (k1 && k2 && !k3 && !k4 && !k5 && !k6) freq = 220.00f;   // A
  else if (k1 && !k2 && !k3 && !k4 && !k5 && !k6) freq = 246.94f;  // B
  else if (!k1 && k2 && !k3 && !k4 && !k5 && !k6) freq = 261.63f;  // C
  else if (!k1 && !k2 && !k3 && !k4 && !k5 && !k6) freq = 277.18f; // C#

  if (flatPressed && !sharpPressed) freq *= 0.943874f;
  if (sharpPressed && !flatPressed) freq *= 1.059463f;
  if (octaveUpPressed && !octaveDownPressed) freq *= 2.0f;
  if (octaveDownPressed && !octaveUpPressed) freq *= 0.5f;

  return freq;
}

void updateMultiButton() {
  int raw = digitalRead(MULTI_PIN);

  if (raw != multiRawState) {
    multiRawState = raw;
    multiLastChangeMs = millis();
  }

  if ((millis() - multiLastChangeMs) >= multiDebounceMs) {
    if (multiStableState != multiRawState) {
      multiStableState = multiRawState;
    }
  }

  if (multiStableState == LOW && !multiHandledPress) {
    instrumentEnabled = !instrumentEnabled;
    multiHandledPress = true;

    Serial.print("Enabled = ");
    Serial.println(instrumentEnabled ? 1 : 0);
  }

  if (multiStableState == HIGH) {
    multiHandledPress = false;
  }
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < numMainKeys; i++) {
    pinMode(mainKeyPins[i], INPUT_PULLUP);
  }

  pinMode(FLAT_PIN, INPUT_PULLUP);
  pinMode(SHARP_PIN, INPUT_PULLUP);
  pinMode(OCTAVE_UP_PIN, INPUT_PULLUP);
  pinMode(OCTAVE_DOWN_PIN, INPUT_PULLUP);
  pinMode(MULTI_PIN, INPUT_PULLUP);

  multiRawState = digitalRead(MULTI_PIN);
  multiStableState = multiRawState;

  Serial.println("Button-only EWI ready");
  Serial.println("Press multi to toggle sound on/off");
}

void loop() {
  for (int i = 0; i < numMainKeys; i++) {
    currentMainFingering[i] = (digitalRead(mainKeyPins[i]) == LOW);
  }

  flatPressed = (digitalRead(FLAT_PIN) == LOW);
  sharpPressed = (digitalRead(SHARP_PIN) == LOW);
  octaveUpPressed = (digitalRead(OCTAVE_UP_PIN) == LOW);
  octaveDownPressed = (digitalRead(OCTAVE_DOWN_PIN) == LOW);

  updateMultiButton();

  targetFreq = getFreqFromFingering();
  currentFreq += 0.0025f * (targetFreq - currentFreq);

  float sample = 0.0f;

  if (instrumentEnabled) {
    phase += TWO_PI * currentFreq / sampleRate;
    if (phase >= TWO_PI) phase -= TWO_PI;

    sample =
      1.00f * sinf(phase) +
      0.20f * sinf(2.0f * phase) +
      0.05f * sinf(3.0f * phase);

    sample *= 0.35f;
  }

  smooth1 += 0.10f * (sample - smooth1);
  smooth2 += 0.06f * (smooth1 - smooth2);
  sample = smooth2;

  int dacValue = 128 + (int)(sample * 100.0f);
  if (dacValue < 0) dacValue = 0;
  if (dacValue > 255) dacValue = 255;

  dacWrite(AUDIO_PIN, dacValue);

  delayMicroseconds(45);
}