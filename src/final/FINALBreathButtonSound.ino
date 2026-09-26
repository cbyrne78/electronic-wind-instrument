  #include <Arduino.h>
  #include <math.h>

  // main fingering keys (6)
  const int mainKeyCount = 6;
  const int mainKeyPins[mainKeyCount] = { 14, 27, 26, 13, 18, 19 };

  // modifier buttons
  #define FLAT_PIN 21
  #define SHARP_PIN 22
  #define OCTAVE_UP_PIN 23
  #define OCTAVE_DOWN_PIN 17
  #define MULTI_PIN 16

  // hardware pins
  #define AUDIO_PIN 25
  #define HX_DOUT 32
  #define HX_SCK 33

  // debounce timing
  unsigned long debounceDelayMs = 25;

  // key states
  int mainKeyStableState[mainKeyCount];
  int mainKeyRawState[mainKeyCount];
  unsigned long mainKeyLastChangeMs[mainKeyCount];
  bool mainKeyPressed[mainKeyCount] = { false, false, false, false, false, false };

  // modifier states
  int flatStableState, flatRawState;
  unsigned long flatLastChangeMs = 0;
  bool flatPressed = false;

  int sharpStableState, sharpRawState;
  unsigned long sharpLastChangeMs = 0;
  bool sharpPressed = false;

  int octaveUpStableState, octaveUpRawState;
  unsigned long octaveUpLastChangeMs = 0;
  bool octaveUpPressed = false;

  int octaveDownStableState, octaveDownRawState;
  unsigned long octaveDownLastChangeMs = 0;
  bool octaveDownPressed = false;

  // multi button (toggle sound on/off)
  int multiStableState, multiRawState;
  unsigned long multiLastChangeMs = 0;
  bool instrumentOn = false;

  // audio variables
  const float sampleRate = 22050.0f;
  float phase = 0.0f;
  float audioSmooth1 = 0.0f;
  float audioSmooth2 = 0.0f;
  float outputLevel = 0.0f;

  float currentFrequency = 261.63f;
  float targetFrequency = 261.63f;

  // timing
  unsigned long lastControlUpdateMs = 0;
  unsigned long controlIntervalMs = 2;

  // breath control
  float breathBaseline = 0.0f;
  float targetBreathAmount = 0.0f;
  float smoothedBreathAmount = 0.0f;

  unsigned long lastBreathPollMs = 0;
  unsigned long breathPollIntervalMs = 25;
  unsigned long lastBreathReadMs = 0;

  #define BREATH_MAX_COUNTS 100000.0f
  #define BREATH_MIN_COUNTS 2000.0f
  #define BREATH_TIMEOUT_MS 150

  // read HX710B ADC
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

  // calibrate baseline pressure (no blowing)
  void calibrateBreathBaseline() {
    long total = 0;
    int valid = 0;

    for (int i = 0; i < 100; i++) {
      while (digitalRead(HX_DOUT) == HIGH) delay(1);

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
    lastBreathReadMs = millis();

    Serial.print("Baseline = ");
    Serial.println((long)breathBaseline);
  }

  // convert key combination to frequency
  float getFrequencyFromKeys() {
    bool k1 = mainKeyPressed[0];
    bool k2 = mainKeyPressed[1];
    bool k3 = mainKeyPressed[2];
    bool k4 = mainKeyPressed[3];
    bool k5 = mainKeyPressed[4];
    bool k6 = mainKeyPressed[5];

    float freq = 277.18f;  // default C#

    if (k1 && k2 && k3 && k4 && k5 && k6) freq = 146.83f;             // D
    else if (k1 && k2 && k3 && k4 && k5 && !k6) freq = 164.81f;       // E
    else if (k1 && k2 && k3 && !k4 && k5 && !k6) freq = 185.00f;      // F#
    else if (k1 && k2 && k3 && k4 && !k5 && !k6) freq = 174.61f;      // F
    else if (k1 && k2 && k3 && !k4 && !k5 && !k6) freq = 196.00f;     // G
    else if (k1 && k2 && !k3 && !k4 && !k5 && !k6) freq = 220.00f;    // A
    else if (k1 && !k2 && !k3 && !k4 && !k5 && !k6) freq = 246.94f;   // B
    else if (!k1 && k2 && !k3 && !k4 && !k5 && !k6) freq = 261.63f;   // C
    else if (!k1 && !k2 && !k3 && !k4 && !k5 && !k6) freq = 277.18f;  // C#

    // modifiers
    if (flatPressed && !sharpPressed) freq *= 0.943874f;
    if (sharpPressed && !flatPressed) freq *= 1.059463f;
    if (octaveUpPressed && !octaveDownPressed) freq *= 2.0f;
    if (octaveDownPressed && !octaveUpPressed) freq *= 0.5f;

    return freq;
  }

  // update main key states with debounce
  void updateMainKeys() {
    for (int i = 0; i < mainKeyCount; i++) {
      int state = digitalRead(mainKeyPins[i]);

      if (state != mainKeyRawState[i]) {
        mainKeyLastChangeMs[i] = millis();
        mainKeyRawState[i] = state;
      }

      if (millis() - mainKeyLastChangeMs[i] > debounceDelayMs) {
        mainKeyStableState[i] = state;
      }

      mainKeyPressed[i] = (mainKeyStableState[i] == LOW);
    }
  }

  // generic debounce for single buttons
  void updateButton(int pin, int &stable, int &raw, unsigned long &t, bool &pressed) {
    int state = digitalRead(pin);

    if (state != raw) {
      t = millis();
      raw = state;
    }

    if (millis() - t > debounceDelayMs) {
      stable = state;
    }

    pressed = (stable == LOW);
  }

  // toggle instrument on/off
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

  // read breath and convert to level
  void updateBreath() {
    long raw = readHX710BRaw();
    if (raw == 0x7FFFFFFF) return;

    float pressure = (float)raw - breathBaseline;
    if (pressure < 0) pressure = 0;

    if (pressure < BREATH_MIN_COUNTS) pressure = 0;
    if (pressure > BREATH_MAX_COUNTS) pressure = BREATH_MAX_COUNTS;

    targetBreathAmount = pressure / BREATH_MAX_COUNTS;
    lastBreathReadMs = millis();
  }

  void setup() {
    Serial.begin(115200);

    pinMode(HX_SCK, OUTPUT);
    pinMode(HX_DOUT, INPUT);
    digitalWrite(HX_SCK, LOW);

    for (int i = 0; i < mainKeyCount; i++) {
      pinMode(mainKeyPins[i], INPUT_PULLUP);
      int s = digitalRead(mainKeyPins[i]);
      mainKeyStableState[i] = s;
      mainKeyRawState[i] = s;
    }

    pinMode(FLAT_PIN, INPUT_PULLUP);
    flatStableState = flatRawState = digitalRead(FLAT_PIN);

    pinMode(SHARP_PIN, INPUT_PULLUP);
    sharpStableState = sharpRawState = digitalRead(SHARP_PIN);

    pinMode(OCTAVE_UP_PIN, INPUT_PULLUP);
    octaveUpStableState = octaveUpRawState = digitalRead(OCTAVE_UP_PIN);

    pinMode(OCTAVE_DOWN_PIN, INPUT_PULLUP);
    octaveDownStableState = octaveDownRawState = digitalRead(OCTAVE_DOWN_PIN);

    pinMode(MULTI_PIN, INPUT_PULLUP);
    multiStableState = multiRawState = digitalRead(MULTI_PIN);

    Serial.println("Calibrating...");
    delay(500);
    calibrateBreathBaseline();
    Serial.println("Ready.");
  }

  void loop() {

    // update keys and buttons
    if (millis() - lastControlUpdateMs >= controlIntervalMs) {
      lastControlUpdateMs = millis();

      updateMainKeys();
      updateButton(FLAT_PIN, flatStableState, flatRawState, flatLastChangeMs, flatPressed);
      updateButton(SHARP_PIN, sharpStableState, sharpRawState, sharpLastChangeMs, sharpPressed);
      updateButton(OCTAVE_UP_PIN, octaveUpStableState, octaveUpRawState, octaveUpLastChangeMs, octaveUpPressed);
      updateButton(OCTAVE_DOWN_PIN, octaveDownStableState, octaveDownRawState, octaveDownLastChangeMs, octaveDownPressed);

      updateMultiButton();

      targetFrequency = getFrequencyFromKeys();
    }

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

    // amplitude control
    float targetLevel = instrumentOn ? smoothedBreathAmount : 0.0f;

    if (targetLevel > outputLevel)
      outputLevel += 0.003f * (targetLevel - outputLevel);
    else
      outputLevel += 0.020f * (targetLevel - outputLevel);

    if (outputLevel < 0.001f) outputLevel = 0.0f;

    // smooth frequency changes
    currentFrequency += 0.0025f * (targetFrequency - currentFrequency);

    // oscillator
    phase += (2.0f * PI * currentFrequency / sampleRate);
    if (phase >= 2.0f * PI) phase -= 2.0f * PI;

    float sample =
      sinf(phase) + 0.20f * sinf(2.0f * phase) + 0.05f * sinf(3.0f * phase);

    sample *= outputLevel;

    // simple low-pass smoothing
    audioSmooth1 += 0.10f * (sample - audioSmooth1);
    audioSmooth2 += 0.06f * (audioSmooth1 - audioSmooth2);

    int dacValue = 128 + (int)(audioSmooth2 * 82.0f);
    if (dacValue < 0) dacValue = 0;
    if (dacValue > 255) dacValue = 255;

    dacWrite(AUDIO_PIN, dacValue);

    delayMicroseconds(45);
  }