#include "settings_store.h"

#include "firmware_json.h"
#include <LittleFS.h>

#include "app_state.h"
#include "config.h"
#include "proximity_store.h"

namespace {
constexpr char SETTINGS_FILE[] = "/global-settings.json";
constexpr char SETTINGS_BACKUP[] = "/global-settings.bak";
constexpr char SETTINGS_TEMP[] = "/global-settings.tmp";
bool mounted = false;

String efuseMacHex() {
  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[13];
  snprintf(suffix, sizeof(suffix), "%04X%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint32_t>(chipId));
  return String(suffix);
}

String defaultApSsid() {
  return String(DEFAULT_AP_SSID_PREFIX) + efuseMacHex().substring(0, 6);
}

String previousDefaultApSsid() {
  return String(DEFAULT_AP_SSID_PREFIX) + efuseMacHex();
}

bool mountStorage() {
  if (mounted) return true;
  mounted = LittleFS.begin(true);
  return mounted;
}

void settingsToJson(JsonDocument &doc) {
  doc["version"] = 1;
  doc["username"] = globalSettings.username;
  doc["apSsid"] = globalSettings.apSsid;
  doc["apPassword"] = globalSettings.apPassword;
  doc["bleBroadcastEnabled"] = globalSettings.bleBroadcastEnabled;
  doc["friendModeEnabled"] = globalSettings.friendModeEnabled;
  doc["enlightened"] = globalSettings.enlightened;
  doc["use24HourTime"] = globalSettings.use24HourTime;
  doc["hardwareRevision"] = globalSettings.hardwareRevision;
  doc["rotationQuarterTurns"] = globalSettings.rotationQuarterTurns;
  doc["minPowerMw"] = globalSettings.minPowerMw;
  doc["maxPowerMw"] = globalSettings.maxPowerMw;
  doc["brightnessPercent"] = globalSettings.brightnessPercent;
  for (uint8_t i = 0; i < 2; ++i) {
    JsonObject wifi = doc["wifi"][i].to<JsonObject>();
    wifi["ssid"] = globalSettings.wifi[i].ssid;
    wifi["password"] = globalSettings.wifi[i].password;
  }
}

bool jsonToSettings(JsonDocument &doc, GlobalSettings &target) {
  if ((doc["version"] | 0) != 1) return false;
  target.username = String(doc["username"] | "Agent").substring(
      0, REMOTE_USERNAME_MAX_BYTES);
  target.apSsid = String(doc["apSsid"] | "").substring(0, 32);
  if (target.apSsid.isEmpty() || target.apSsid == previousDefaultApSsid()) {
    target.apSsid = defaultApSsid();
  }
  target.apPassword =
      String(doc["apPassword"] | DEFAULT_AP_PASSWORD).substring(0, 63);
  if (target.apPassword.length() < 8) target.apPassword = DEFAULT_AP_PASSWORD;
  target.bleBroadcastEnabled = doc["bleBroadcastEnabled"] | true;
  target.friendModeEnabled = doc["friendModeEnabled"] | false;
  target.enlightened = doc["enlightened"] | false;
  target.use24HourTime = doc["use24HourTime"] | true;
  target.hardwareRevision = (doc["hardwareRevision"] | 1) == 2 ? 2 : 1;
  target.rotationQuarterTurns = (doc["rotationQuarterTurns"] | 0) == 2 ? 2 : 0;
  const uint32_t hardwareLimitMw =
      hardwareLedLimitMw(target.hardwareRevision);
  target.minPowerMw = min<uint32_t>(doc["minPowerMw"] | DEFAULT_MIN_POWER_MW,
                                    hardwareLimitMw);
  target.maxPowerMw = min<uint32_t>(doc["maxPowerMw"] | DEFAULT_MAX_POWER_MW,
                                    hardwareLimitMw);
  if (target.maxPowerMw < target.minPowerMw) return false;
  target.brightnessPercent = constrain(doc["brightnessPercent"] | 100, 0, 100);
  for (uint8_t i = 0; i < 2; ++i) {
    target.wifi[i].ssid = String(doc["wifi"][i]["ssid"] | "").substring(0, 32);
    target.wifi[i].password =
        String(doc["wifi"][i]["password"] | "").substring(0, 64);
  }
  return true;
}

bool readSettingsFile(const char *path, GlobalSettings &target) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  return !error && jsonToSettings(doc, target);
}

bool sameSettings(const GlobalSettings &a, const GlobalSettings &b) {
  if (a.username != b.username || a.apSsid != b.apSsid ||
      a.apPassword != b.apPassword || a.enlightened != b.enlightened ||
      a.bleBroadcastEnabled != b.bleBroadcastEnabled ||
      a.friendModeEnabled != b.friendModeEnabled ||
      a.use24HourTime != b.use24HourTime ||
      a.hardwareRevision != b.hardwareRevision ||
      a.rotationQuarterTurns != b.rotationQuarterTurns ||
      a.minPowerMw != b.minPowerMw ||
      a.maxPowerMw != b.maxPowerMw ||
      a.brightnessPercent != b.brightnessPercent) {
    return false;
  }
  for (uint8_t i = 0; i < 2; ++i) {
    if (a.wifi[i].ssid != b.wifi[i].ssid ||
        a.wifi[i].password != b.wifi[i].password) {
      return false;
    }
  }
  return true;
}

}  // namespace

void loadSettings() {
  globalSettings = GlobalSettings{};
  globalSettings.apSsid = defaultApSsid();
  if (mountStorage()) {
    GlobalSettings loaded;
    if (readSettingsFile(SETTINGS_FILE, loaded) ||
        readSettingsFile(SETTINGS_BACKUP, loaded)) {
      globalSettings = loaded;
    }
  }
  updatePowerLimit();
}

bool saveSettings() {
  if (!mountStorage()) return false;

  JsonDocument doc;
  settingsToJson(doc);
  LittleFS.remove(SETTINGS_TEMP);
  File file = LittleFS.open(SETTINGS_TEMP, "w");
  if (!file) return false;
  const size_t bytesWritten = serializeJson(doc, file);
  file.flush();
  file.close();
  if (!bytesWritten) {
    LittleFS.remove(SETTINGS_TEMP);
    return false;
  }

  GlobalSettings verified;
  if (!readSettingsFile(SETTINGS_TEMP, verified) ||
      !sameSettings(globalSettings, verified)) {
    LittleFS.remove(SETTINGS_TEMP);
    return false;
  }

  LittleFS.remove(SETTINGS_BACKUP);
  if (LittleFS.exists(SETTINGS_FILE) &&
      !LittleFS.rename(SETTINGS_FILE, SETTINGS_BACKUP)) {
    LittleFS.remove(SETTINGS_TEMP);
    return false;
  }
  if (!LittleFS.rename(SETTINGS_TEMP, SETTINGS_FILE)) return false;

  GlobalSettings committed;
  return readSettingsFile(SETTINGS_FILE, committed) &&
         sameSettings(globalSettings, committed);
}
