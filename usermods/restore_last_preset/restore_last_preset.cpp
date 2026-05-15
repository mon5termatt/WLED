#include "wled.h"

// Remembers the last preset slot you selected (currentPreset > 0) and writes it to
// cfg.json after a quiet period. On the next boot it reapplies that preset so an
// unexpected reset does not leave you on a different default.
// Also remembers master light on/off (bri > 0) so a reboot restores OFF when you left OFF.
// Optional GPIO headlight sense: after boot, if the pin says headlights are off, bri is
// forced to 0 even if cfg had ledsOn true (open NO contact + pull-up -> typically HIGH).

class RestoreLastPresetUsermod : public Usermod {
 private:
  static constexpr uint8_t kMaxIgnored = 16;
  // Default: GPIO / headlight automation presets (97–99) are not "user" presets.
  uint8_t ignoredPresets[kMaxIgnored] = {97, 98, 99};
  uint8_t ignoredCount = 3;

  bool initDone = false;
  bool enabled = true;
  bool restoreOnBoot = true;
  uint8_t persistedPreset = 0;
  uint16_t saveDebounceMs = 5000;
  bool persistedLedsOn = true;
  unsigned long saveAtMs = 0;
  unsigned long bootIgnoreUntil = 0;
  unsigned long bootMillis = 0;
  bool pendingBootPower = false;

  // Headlight / optocoupler input (default GPIO2 for your wiring; set headlightPin -1 to disable).
  int8_t headlightSensePin = 2;
  bool headlightOffWhenLow = false;
  bool headlightPullup = true;
  bool headlightPinOk = false;
  bool headlightPinShared = false; // true: GPIO owned by WLED Button, we only digitalRead()

  static const char _name[];
  static const char _enabled[];
  static const char _restoreOnBoot[];
  static const char _saveDebounceSec[];
  static const char _preset[];
  static const char _ignoredPresets[];
  static const char _ledsOn[];
  static const char _headlightPin[];
  static const char _headlightOffWhenLow[];
  static const char _headlightPullup[];

  bool isIgnoredPreset(uint8_t id) const {
    for (uint8_t i = 0; i < ignoredCount; i++)
      if (ignoredPresets[i] == id) return true;
    return false;
  }

  bool headlightsOffInhibit() const {
    if (!headlightPinOk || headlightSensePin < 0) return false;
    const int v = digitalRead((uint8_t)headlightSensePin);
    return headlightOffWhenLow ? (v == LOW) : (v == HIGH);
  }

  bool headlightsOn() const {
    return headlightPinOk && headlightSensePin >= 0 && !headlightsOffInhibit();
  }

  // If allocate fails because a Button already uses this GPIO, share the line (same optocoupler).
  void initHeadlightSense() {
    headlightPinOk = false;
    headlightPinShared = false;
    if (headlightSensePin < 0) return;

    const uint8_t pin = (uint8_t)headlightSensePin;
    if (PinManager::allocatePin(pin, false, PinOwner::UM_Unspecified)) {
      pinMode(pin, headlightPullup ? INPUT_PULLUP : INPUT);
      headlightPinOk = true;
      return;
    }
    if (PinManager::isPinAllocated(pin, PinOwner::Button)) {
      headlightPinOk = true;
      headlightPinShared = true;
      for (const Button &btn : buttons) {
        if (btn.pin != headlightSensePin || btn.type == BTN_TYPE_NONE) continue;
        switch (btn.type) {
          case BTN_TYPE_PUSH:
          case BTN_TYPE_SWITCH:
            headlightOffWhenLow = false; // active LOW (typical opto + pull-up)
            break;
          case BTN_TYPE_PUSH_ACT_HIGH:
          case BTN_TYPE_PIR_SENSOR:
            headlightOffWhenLow = true;  // active HIGH
            break;
          default:
            break;
        }
        break;
      }
    }
  }

  void loadIgnoredFromConfig(JsonObject& top) {
    JsonArray arr = top[FPSTR(_ignoredPresets)].as<JsonArray>();
    if (arr.isNull()) {
      ignoredPresets[0] = 97;
      ignoredPresets[1] = 98;
      ignoredPresets[2] = 99;
      ignoredCount = 3;
      return;
    }
    ignoredCount = 0;
    for (JsonVariant v : arr) {
      if (ignoredCount >= kMaxIgnored) break;
      uint8_t id = v.as<uint8_t>();
      if (id > 0 && id < 251) ignoredPresets[ignoredCount++] = id;
    }
  }

 public:
  void setup() override {
    bootMillis = millis();
    bootIgnoreUntil = bootMillis + 3000;
    initHeadlightSense();
    if (enabled && restoreOnBoot) {
      if (persistedPreset > 0 && persistedPreset < 251 && !isIgnoredPreset(persistedPreset))
        applyPreset(persistedPreset, CALL_MODE_INIT);
      pendingBootPower = true;
    }
    initDone = true;
  }

  void loop() override {
    unsigned long now = millis();

    // After boot preset (if any) is applied: GPIO "headlights off" overrides saved ledsOn so LEDs stay off.
    if (pendingBootPower && enabled && restoreOnBoot && (now - bootMillis > 150) && presetToApply == 0) {
      const bool gpioHeadlightsOff = headlightsOffInhibit();
      if (gpioHeadlightsOff || !persistedLedsOn) {
        bri = 0;
        stateUpdated(CALL_MODE_INIT);
      }
      pendingBootPower = false;
    }

    if (saveAtMs && now >= saveAtMs && enabled) {
      persistedLedsOn = (bri > 0);
      if (currentPreset > 0 && !isIgnoredPreset(currentPreset)) persistedPreset = currentPreset;
      saveAtMs = 0;
      configNeedsWrite = true;
    }

    if (!enabled || !initDone || now < bootIgnoreUntil) return;

    // Ignored automation presets never update the saved preset ID (only bri / ledsOn can still dirty-save).
    bool dirtyLeds = ((bri > 0) != persistedLedsOn);
    bool dirtyPreset = (currentPreset > 0 && !isIgnoredPreset(currentPreset) && currentPreset != persistedPreset);

    if (dirtyLeds || dirtyPreset) {
      if (saveAtMs == 0) saveAtMs = now + saveDebounceMs;
    } else {
      saveAtMs = 0;
    }
  }

  void addToJsonState(JsonObject& root) override {
    if (!enabled) return;
    JsonObject um = root.createNestedObject(FPSTR(_name));
    const bool sense = headlightPinOk && headlightSensePin >= 0;
    um["sense"] = sense;
    if (sense) {
      um["on"] = headlightsOn();
      um["shared"] = headlightPinShared;
    }
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top[FPSTR(_enabled)] = enabled;
    top[FPSTR(_restoreOnBoot)] = restoreOnBoot;
    top[FPSTR(_saveDebounceSec)] = (uint8_t)((saveDebounceMs + 500) / 1000);
    top[FPSTR(_preset)] = persistedPreset;
    top[FPSTR(_ledsOn)] = persistedLedsOn;
    top[FPSTR(_headlightPin)] = headlightSensePin;
    top[FPSTR(_headlightOffWhenLow)] = headlightOffWhenLow;
    top[FPSTR(_headlightPullup)] = headlightPullup;
    JsonArray arr = top.createNestedArray(FPSTR(_ignoredPresets));
    for (uint8_t i = 0; i < ignoredCount; i++) arr.add(ignoredPresets[i]);
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root[FPSTR(_name)];
    if (top.isNull()) return false;

    enabled = top[FPSTR(_enabled)] | enabled;
    restoreOnBoot = top[FPSTR(_restoreOnBoot)] | restoreOnBoot;
    {
      uint16_t sec = top[FPSTR(_saveDebounceSec)] | (uint16_t)((saveDebounceMs + 500) / 1000);
      if (sec < 2) sec = 2;
      if (sec > 120) sec = 120;
      saveDebounceMs = sec * 1000;
    }
    loadIgnoredFromConfig(top);
    uint8_t p = top[FPSTR(_preset)] | persistedPreset;
    if (p < 251) persistedPreset = p;
    if (persistedPreset > 0 && isIgnoredPreset(persistedPreset)) persistedPreset = 0;
    if (!top[FPSTR(_ledsOn)].isNull()) persistedLedsOn = top[FPSTR(_ledsOn)].as<bool>();
    if (!top[FPSTR(_headlightPin)].isNull()) {
      int pin = top[FPSTR(_headlightPin)].as<int>();
      if (pin < -1 || pin > 50) headlightSensePin = -1;
      else headlightSensePin = (int8_t)pin;
    }
    if (!top[FPSTR(_headlightOffWhenLow)].isNull())
      headlightOffWhenLow = top[FPSTR(_headlightOffWhenLow)].as<bool>();
    if (!top[FPSTR(_headlightPullup)].isNull())
      headlightPullup = top[FPSTR(_headlightPullup)].as<bool>();
    return true;
  }
};

const char RestoreLastPresetUsermod::_name[] PROGMEM = "RestoreLastPreset";
const char RestoreLastPresetUsermod::_enabled[] PROGMEM = "enabled";
const char RestoreLastPresetUsermod::_restoreOnBoot[] PROGMEM = "restoreOnBoot";
const char RestoreLastPresetUsermod::_saveDebounceSec[] PROGMEM = "saveDebounceSec";
const char RestoreLastPresetUsermod::_preset[] PROGMEM = "preset";
const char RestoreLastPresetUsermod::_ignoredPresets[] PROGMEM = "ignoredPresets";
const char RestoreLastPresetUsermod::_ledsOn[] PROGMEM = "ledsOn";
const char RestoreLastPresetUsermod::_headlightPin[] PROGMEM = "headlightPin";
const char RestoreLastPresetUsermod::_headlightOffWhenLow[] PROGMEM = "headlightOffWhenLow";
const char RestoreLastPresetUsermod::_headlightPullup[] PROGMEM = "headlightPullup";

static RestoreLastPresetUsermod restoreLastPreset;
REGISTER_USERMOD(restoreLastPreset);
