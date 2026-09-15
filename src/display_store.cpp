#include "display_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_state.h"
#include "glyphs.h"

namespace {
constexpr char DISPLAY_FILE[] = "/display-settings.json";
constexpr char DISPLAY_BACKUP[] = "/display-settings.bak";
constexpr char DISPLAY_TEMP[] = "/display-settings.tmp";

void addColor(JsonArray array, const Rgbw &color) {
  array.add(color.r);
  array.add(color.g);
  array.add(color.b);
  array.add(color.w);
}

bool readColor(JsonArrayConst array, Rgbw &color) {
  if (array.size() != 4) return false;
  color = {static_cast<uint8_t>(constrain(array[0] | 0, 0, 255)),
           static_cast<uint8_t>(constrain(array[1] | 0, 0, 255)),
           static_cast<uint8_t>(constrain(array[2] | 0, 0, 255)),
           static_cast<uint8_t>(constrain(array[3] | 0, 0, 255))};
  return true;
}

void toJson(JsonDocument &doc) {
  doc["version"] = 2;
  doc["mode"] = static_cast<uint8_t>(displaySettings.mode);
  doc["intervalMs"] = displaySettings.intervalMs;
  JsonArray sequence = doc["sequence"].to<JsonArray>();
  for (uint8_t i = 0; i < displaySettings.glyphCount; ++i) {
    sequence.add(displaySettings.glyphs[i]);
  }
  addColor(doc["line"].to<JsonArray>(), displaySettings.lineColor);
  addColor(doc["core"].to<JsonArray>(), displaySettings.coreColor);
  doc["lineBrightness"] = displaySettings.lineBrightnessPercent;
  doc["coreBrightness"] = displaySettings.coreBrightnessPercent;
}

bool fromJson(JsonDocument &doc, DisplaySettings &target) {
  if ((doc["version"] | 0) != 2) return false;
  const uint8_t mode = doc["mode"] | 0;
  if (mode > static_cast<uint8_t>(DisplayMode::Draw)) return false;
  JsonArrayConst sequence = doc["sequence"];
  if (sequence.size() < 1 || sequence.size() > MAX_GLYPH_PAGES) return false;
  target.mode = static_cast<DisplayMode>(mode);
  target.glyphCount = sequence.size();
  for (uint8_t i = 0; i < target.glyphCount; ++i) {
    const uint16_t glyph = sequence[i] | 0;
    if (glyph >= GLYPH_COUNT) return false;
    target.glyphs[i] = glyph;
  }
  target.intervalMs = constrain(doc["intervalMs"] | DEFAULT_INTERVAL_MS,
                                100UL, 3600000UL);
  if (!readColor(doc["line"].as<JsonArrayConst>(), target.lineColor) ||
      !readColor(doc["core"].as<JsonArrayConst>(), target.coreColor)) {
    return false;
  }
  target.lineBrightnessPercent =
      constrain(doc["lineBrightness"] | 100, 0, 100);
  target.coreBrightnessPercent =
      constrain(doc["coreBrightness"] | 100, 0, 100);
  return true;
}

bool readFile(const char *path, DisplaySettings &target) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  return !error && fromJson(doc, target);
}

bool sameColor(const Rgbw &a, const Rgbw &b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.w == b.w;
}
}  // namespace

void applyFactionColorDefaults(bool enlightened) {
  constexpr Rgbw resistanceLine{0, 160, 255, 0};
  constexpr Rgbw resistanceCore{0, 255, 255, 0};
  constexpr Rgbw previousEnlightenedLine{40, 220, 45, 0};
  constexpr Rgbw previousEnlightenedCore{110, 255, 80, 0};
  constexpr Rgbw enlightenedLine{0, 200, 0, 0};
  constexpr Rgbw enlightenedCore{0, 255, 0, 0};
  if (enlightened) {
    if (sameColor(displaySettings.lineColor, resistanceLine) ||
        sameColor(displaySettings.lineColor, previousEnlightenedLine)) {
      displaySettings.lineColor = enlightenedLine;
    }
    if (sameColor(displaySettings.coreColor, resistanceCore) ||
        sameColor(displaySettings.coreColor, previousEnlightenedCore)) {
      displaySettings.coreColor = enlightenedCore;
    }
  } else {
    if (sameColor(displaySettings.lineColor, enlightenedLine) ||
        sameColor(displaySettings.lineColor, previousEnlightenedLine)) {
      displaySettings.lineColor = resistanceLine;
    }
    if (sameColor(displaySettings.coreColor, enlightenedCore) ||
        sameColor(displaySettings.coreColor, previousEnlightenedCore)) {
      displaySettings.coreColor = resistanceCore;
    }
  }
}

void loadDisplaySettings() {
  const uint8_t customPageCount = displaySettings.customPageCount;
  DisplaySettings loaded;
  if (LittleFS.begin(true) &&
      (readFile(DISPLAY_FILE, loaded) || readFile(DISPLAY_BACKUP, loaded))) {
    displaySettings = loaded;
  }
  displaySettings.customPageCount = customPageCount;
  applyFactionColorDefaults(globalSettings.enlightened);
}

bool saveDisplaySettings() {
  if (!LittleFS.begin(true)) return false;
  JsonDocument doc;
  toJson(doc);
  LittleFS.remove(DISPLAY_TEMP);
  File file = LittleFS.open(DISPLAY_TEMP, "w");
  if (!file) return false;
  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  DisplaySettings verified;
  if (!written || !readFile(DISPLAY_TEMP, verified)) {
    LittleFS.remove(DISPLAY_TEMP);
    return false;
  }
  LittleFS.remove(DISPLAY_BACKUP);
  if (LittleFS.exists(DISPLAY_FILE) &&
      !LittleFS.rename(DISPLAY_FILE, DISPLAY_BACKUP)) {
    LittleFS.remove(DISPLAY_TEMP);
    return false;
  }
  if (!LittleFS.rename(DISPLAY_TEMP, DISPLAY_FILE)) return false;
  return readFile(DISPLAY_FILE, verified);
}
