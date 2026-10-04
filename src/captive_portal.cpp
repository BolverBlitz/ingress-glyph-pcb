#include "captive_portal.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "app_state.h"
#include "config.h"
#include "draw_store.h"
#include "display_store.h"
#include "glyph_drawer.h"
#include "glyphs.h"
#include "glyph_sequences.h"
#include "misc_controller.h"
#include "proximity_manager.h"
#include "proximity_store.h"
#include "settings_store.h"
#include "temperature_controller.h"
#include "time_service.h"
#include "localized_web_ui.h"
#include "effect_settings_json.h"

static Rgbw jsonColor(JsonArrayConst a, const Rgbw &fallback) {
  if (a.size() != 4) return fallback;
  return {static_cast<uint8_t>(constrain(a[0] | 0, 0, 255)),
          static_cast<uint8_t>(constrain(a[1] | 0, 0, 255)),
          static_cast<uint8_t>(constrain(a[2] | 0, 0, 255)),
          static_cast<uint8_t>(constrain(a[3] | 0, 0, 255))};
}

static String networkHostname() {
  String hostname = String(DEFAULT_AP_SSID_PREFIX) + globalSettings.username;
  if (hostname.length() <= 32) return hostname;
  size_t length = 32;
  while (length &&
         (static_cast<uint8_t>(hostname[length]) & 0xc0) == 0x80) {
    --length;
  }
  return hostname.substring(0, length);
}

static void applyStationHostname() {
  const String hostname = networkHostname();
  WiFi.setHostname(hostname.c_str());
  Serial.printf("Wi-Fi: network hostname '%s'\n", hostname.c_str());
}

namespace {
struct CsvStreamContext {
  WebServer *server;
  String buffer;
};

void flushCsv(CsvStreamContext &context) {
  if (context.buffer.isEmpty()) return;
  context.server->sendContent(context.buffer);
  context.buffer = "";
}

void appendCsv(CsvStreamContext &context, const String &line) {
  context.buffer += line;
  if (context.buffer.length() >= 2048) flushCsv(context);
}

String csvText(String value) {
  if (!value.isEmpty() && strchr("=+-@", value[0])) value = "'" + value;
  value.replace("\"", "\"\"");
  return "\"" + value + "\"";
}

String csvDeviceId(const uint8_t deviceId[6]) {
  char text[13];
  snprintf(text, sizeof(text), "%02X%02X%02X%02X%02X%02X", deviceId[0],
           deviceId[1], deviceId[2], deviceId[3], deviceId[4], deviceId[5]);
  return String(text);
}

void exportUserRecord(const KnownUser &user, void *opaque) {
  CsvStreamContext &context = *static_cast<CsvStreamContext *>(opaque);
  appendCsv(context, String(user.userId) + "," + csvDeviceId(user.deviceId) +
                         "," + (user.faction ? "ENL" : "RES") + "," +
                         (user.friendFlag ? "1" : "0") + "," +
                         csvText(user.username) + "\r\n");
}

void exportEncounterRecord(uint16_t userId, bool last, uint32_t timestamp,
                           void *opaque) {
  CsvStreamContext &context = *static_cast<CsvStreamContext *>(opaque);
  appendCsv(context, String(userId) + "," + (last ? "LAST" : "FIRST") +
                         "," + String(timestamp) + "\r\n");
}
}  // namespace

void GlyphWebServer::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(250);
  WiFi.mode(WIFI_STA);
  applyStationHostname();
  WiFi.setSleep(globalSettings.bleBroadcastEnabled);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        Serial.printf("Wi-Fi: disconnected, reason %u\n", info.wifi_sta_disconnected.reason);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  const unsigned configuredProfiles = !globalSettings.wifi[0].ssid.isEmpty() + !globalSettings.wifi[1].ssid.isEmpty();
  Serial.printf("Wi-Fi: %u saved profile(s) configured\n", configuredProfiles);
  const int networkCount = WiFi.scanNetworks(false, true);
  for (uint8_t profileIndex = 0; profileIndex < 2; ++profileIndex) {
    if (globalSettings.wifi[profileIndex].ssid.isEmpty()) continue;
    bool found = false;
    for (int networkIndex = 0; networkIndex < networkCount; ++networkIndex) {
      if (WiFi.SSID(networkIndex) != globalSettings.wifi[profileIndex].ssid) {
        continue;
      }
      found = true;
      wifiBssidKnown_[profileIndex] = true;
      wifiChannel_[profileIndex] = WiFi.channel(networkIndex);
      memcpy(wifiBssid_[profileIndex], WiFi.BSSID(networkIndex), 6);
      Serial.printf(
          "Wi-Fi: profile %u visible (RSSI %d dBm, channel %d, auth %d)\n",
          profileIndex + 1, WiFi.RSSI(networkIndex),
          WiFi.channel(networkIndex), WiFi.encryptionType(networkIndex));
      break;
    }
    if (!found) {
      Serial.printf("Wi-Fi: profile %u is not visible in the boot scan\n", profileIndex + 1);
    }
  }
  WiFi.scanDelete();
  startNextWifiProfile();
  while (WiFi.status() != WL_CONNECTED && wifiProfileIndex_ < 2) {
    manageWifi();
    delay(25);
  }
  if (WiFi.status() != WL_CONNECTED) startHotspot();
  registerRoutes();
  server_.begin();
}

void GlyphWebServer::loop() {
  if (hotspotActive_) dns_.processNextRequest();
  server_.handleClient();
  manageWifi();
  if (restartAtMs_ && static_cast<int32_t>(millis() - restartAtMs_) >= 0) {
    ESP.restart();
  }
}

void GlyphWebServer::startNextWifiProfile() {
  do {
    ++wifiProfileIndex_;
  } while (wifiProfileIndex_ < 2 && globalSettings.wifi[wifiProfileIndex_].ssid.isEmpty());

  if (wifiProfileIndex_ >= 2) {
    WiFi.disconnect(false, false);
    wifiAttemptStartedMs_ = millis();
    startHotspot();
    Serial.println("Wi-Fi: hotspot fallback; retrying saved profiles in 10 s");
    return;
  }

  const WifiProfile &profile = globalSettings.wifi[wifiProfileIndex_];
  WiFi.disconnect(false, false);
  delay(50);
  Serial.printf("Wi-Fi: trying profile %d (SSID length %u, password length %u)\n",
                wifiProfileIndex_ + 1, profile.ssid.length(),
                profile.password.length());
  if (wifiBssidKnown_[wifiProfileIndex_]) {
    WiFi.begin(profile.ssid.c_str(), profile.password.c_str(),  wifiChannel_[wifiProfileIndex_], wifiBssid_[wifiProfileIndex_], false);
  } else {
    WiFi.begin(profile.ssid.c_str(), profile.password.c_str(), 0, nullptr, false);
  }
  wifi_config_t stationConfig = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &stationConfig) == ESP_OK) {
    stationConfig.sta.pmf_cfg.capable = true;
    stationConfig.sta.pmf_cfg.required = false;
    stationConfig.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_wifi_set_config(WIFI_IF_STA, &stationConfig);
  }
  esp_wifi_connect();
  wifiAttemptStartedMs_ = millis();
}

void GlyphWebServer::manageWifi() {
  const wl_status_t status = WiFi.status();
  if (status != lastWifiStatus_) {
    lastWifiStatus_ = status;
    Serial.printf("Wi-Fi: status %d\n", static_cast<int>(status));
    if (status == WL_CONNECTED) {
      Serial.print("Wi-Fi: connected, address ");
      Serial.println(WiFi.localIP());
    }
  }
  if (status == WL_CONNECTED) {
    stopHotspot();
    return;
  }
  if (wifiProfileIndex_ >= 2) {
    if (millis() - wifiAttemptStartedMs_ >= WIFI_RETRY_INTERVAL_MS) {
      wifiProfileIndex_ = -1;
      startNextWifiProfile();
    }
    return;
  }
  if (millis() - wifiAttemptStartedMs_ >= WIFI_CONNECT_TIMEOUT_MS) {
    startNextWifiProfile();
  }
}

void GlyphWebServer::startHotspot() {
  if (hotspotActive_) return;
  WiFi.mode(WIFI_AP_STA);
  const String hostname = networkHostname();
  WiFi.softAPsetHostname(hostname.c_str());
  if (!WiFi.softAP(globalSettings.apSsid.c_str(),
                   globalSettings.apPassword.c_str())) {
    Serial.println("Wi-Fi: could not start hotspot fallback");
    return;
  }
  hotspotActive_ = true;
  dns_.start(53, "*", WiFi.softAPIP());
  Serial.print("Wi-Fi: hotspot fallback at http://");
  Serial.println(WiFi.softAPIP());
}

void GlyphWebServer::stopHotspot() {
  if (!hotspotActive_) return;
  dns_.stop();
  WiFi.softAPdisconnect(true);
  hotspotActive_ = false;
  Serial.println("Wi-Fi: hotspot fallback stopped");
}

void GlyphWebServer::registerRoutes() {
  // Static resources
  server_.on("/", HTTP_GET, [this] {
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "text/html", reinterpret_cast<const char *>(MAIN_PAGE_GZIP), sizeof(MAIN_PAGE_GZIP));
  });
  server_.on("/draw", HTTP_GET, [this] {
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "text/html", reinterpret_cast<const char *>(DRAW_PAGE_GZIP), sizeof(DRAW_PAGE_GZIP));
  });
  server_.on("/settings", HTTP_GET, [this] {
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "text/html",
                   reinterpret_cast<const char *>(SETTINGS_PAGE_GZIP),
                   sizeof(SETTINGS_PAGE_GZIP));
  });
  server_.on("/help", HTTP_GET, [this] {
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "text/html",
                   reinterpret_cast<const char *>(HELP_BUTTONS_PAGE_GZIP),
                   sizeof(HELP_BUTTONS_PAGE_GZIP));
  });
  server_.on("/help/settings", HTTP_GET, [this] {
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "text/html",
                   reinterpret_cast<const char *>(HELP_SETTINGS_PAGE_GZIP),
                   sizeof(HELP_SETTINGS_PAGE_GZIP));
  });
  server_.on("/nearby", HTTP_GET, [this] {
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "text/html", reinterpret_cast<const char *>(NEARBY_PAGE_GZIP), sizeof(NEARBY_PAGE_GZIP));
  });
  server_.on("/index.css", HTTP_GET, [this] { server_.send_P(200, "text/css", MAIN_CSS); });
  server_.on("/draw.css", HTTP_GET, [this] { server_.send_P(200, "text/css", DRAW_CSS); });
  server_.on("/settings.css", HTTP_GET, [this] { server_.send_P(200, "text/css", SETTINGS_CSS); });
  server_.on("/nearby.css", HTTP_GET, [this] { server_.send_P(200, "text/css", NEARBY_CSS); });
  server_.on("/flashlight.css", HTTP_GET, [this] { server_.send_P(200, "text/css", FLASHLIGHT_CSS); });
  server_.on("/flashlight.js", HTTP_GET, [this] { server_.send_P(200, "application/javascript; charset=utf-8", FLASHLIGHT_JS); });
  // WPA support
  server_.on("/manifest.webmanifest", HTTP_GET, [this] { server_.send_P(200, "application/manifest+json", PWA_MANIFEST); });
  server_.on("/sw.js", HTTP_GET, [this] { server_.send_P(200, "application/javascript", SERVICE_WORKER); });
  server_.on("/icon.svg", HTTP_GET, [this] { server_.send_P(200, "image/svg+xml", APP_ICON); });
  // API endpoints
  server_.on("/api/status", HTTP_GET, [this] { sendStatus(); });
  server_.on("/api/settings", HTTP_GET, [this] { sendSettings(); });
  server_.on("/api/wifi/scan", HTTP_GET, [this] { scanWifi(); });
  server_.on("/api/glyphs", HTTP_GET, [this] { sendGlyphs(); });
  server_.on("/api/glyph-sequences", HTTP_GET, [this] { sendGlyphSequences(); });
  server_.on("/api/display", HTTP_GET, [this] { sendDisplay(); });
  server_.on("/api/layout", HTTP_GET, [this] { sendLayout(); });
  server_.on("/api/custom", HTTP_GET, [this] { sendCustom(); });
  server_.on("/api/storage", HTTP_GET, [this] { sendStorage(); });
  server_.on("/api/misc", HTTP_GET, [this] { sendMisc(); });
  server_.on("/api/nearby", HTTP_GET, [this] { sendNearby(); });
  server_.on("/api/people", HTTP_GET, [this] { sendPeople(); });
  server_.on("/api/export/users.csv", HTTP_GET, [this] { exportUsers(); });
  server_.on("/api/export/encounters.csv", HTTP_GET, [this] { exportEncounters(); });
  server_.on("/api/display", HTTP_POST, [this] { setDisplay(); });
  server_.on("/api/display/preview", HTTP_POST, [this] { previewDisplay(); });
  server_.on("/api/custom", HTTP_POST, [this] { setCustom(); });
  server_.on("/api/brightness", HTTP_POST, [this] { setBrightness(); });
  server_.on("/api/flashlight", HTTP_POST, [this] { setFlashlight(); });
  server_.on("/api/time", HTTP_POST, [this] { setUtcFromBrowser(); });
  server_.on("/api/proximity/reset", HTTP_POST, [this] { resetProximityData(); });
  server_.on("/api/friend", HTTP_POST, [this] { setFriend(); });
  server_.on("/api/settings", HTTP_POST, [this] { applySettings(); });
  server_.on("/api/restart", HTTP_POST, [this] { restartController(); });
  server_.on("/api/ota", HTTP_POST, [this] { finishOta(); }, [this] { handleOtaUpload(); });
  for (const char *path : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/connecttest.txt", "/ncsi.txt", "/redirect"}) {
    server_.on(path, HTTP_GET, [this] {
      if (!hotspotActive_) {
        server_.send(404, "text/plain", "Not found");
        return;
      }
      server_.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
      server_.send(302, "text/plain", "");
    });
  }
  server_.onNotFound([this] {
    if (!hotspotActive_) {
      server_.send(404, "text/plain", "Not found");
      return;
    }
    server_.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server_.send(302, "text/plain", "");
  });
}

void GlyphWebServer::sendStatus() {
  JsonDocument doc;
  const uint32_t hardwareLimitMw =
      hardwareLedLimitMw(globalSettings.hardwareRevision);
  doc["apSsid"] = globalSettings.apSsid;
  doc["hotspotActive"] = hotspotActive_;
  doc["apIp"] = hotspotActive_ ? WiFi.softAPIP().toString() : String();
  doc["connected"] = WiFi.status() == WL_CONNECTED;
  doc["ssid"] = WiFi.SSID();
  doc["staIp"] = WiFi.localIP().toString();
  doc["powerMw"] = powerLimitMw;
  doc["hardwareLedLimitMw"] = hardwareLimitMw;
  doc["currentLedPowerMw"] = currentLedPowerMw;
  switch (ledPowerLimitState) {
    case LedPowerLimitState::Software:
      doc["powerLimitSource"] = "SW Limited";
      break;
    case LedPowerLimitState::Hardware:
      doc["powerLimitSource"] = "HW Limited";
      break;
    default:
      doc["powerLimitSource"] = "Not Limited";
      break;
  }
  doc["brightness"] = globalSettings.brightnessPercent;
  doc["username"] = globalSettings.username;
  doc["hostname"] = WiFi.getHostname();
  doc["enlightened"] = globalSettings.enlightened;
  doc["use24HourTime"] = globalSettings.use24HourTime;
  doc["rotation"] = globalSettings.rotationQuarterTurns * 90U;
  doc["hardwareRevision"] = globalSettings.hardwareRevision;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["uiLanguage"] = UI_LANGUAGE;
  doc["chipTemperatureC"] = temperatureRead();
  if (temperatureController.centerValid()) {
    doc["centerTemperatureC"] = temperatureController.centerC();
  } else {
    doc["centerTemperatureC"] = nullptr;
  }
  if (temperatureController.bottomValid()) {
    doc["bottomTemperatureC"] = temperatureController.bottomC();
  } else {
    doc["bottomTemperatureC"] = nullptr;
  }
  doc["timeValid"] = utcTimeValid();
  doc["unixTime"] = utcNow();
  doc["ntpActive"] = ntpActive();
  doc["lastNtpSync"] = lastNtpSyncUnix();
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendSettings() {
  JsonDocument doc;
  doc["username"] = globalSettings.username;
  doc["apSsid"] = globalSettings.apSsid;
  doc["bleBroadcastEnabled"] = globalSettings.bleBroadcastEnabled;
  doc["friendModeEnabled"] = globalSettings.friendModeEnabled;
  doc["enlightened"] = globalSettings.enlightened;
  doc["use24HourTime"] = globalSettings.use24HourTime;
  doc["hardwareRevision"] = globalSettings.hardwareRevision;
  doc["hardwareMaxW"] =
      hardwareLedLimitMw(globalSettings.hardwareRevision) / 1000.0f;
  doc["rotation"] = globalSettings.rotationQuarterTurns * 90U;
  doc["minW"] = globalSettings.minPowerMw / 1000.0f;
  doc["maxW"] = globalSettings.maxPowerMw / 1000.0f;
  doc["brightness"] = globalSettings.brightnessPercent;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["uiLanguage"] = UI_LANGUAGE;
  doc["wifi1"] = globalSettings.wifi[0].ssid;
  doc["wifi2"] = globalSettings.wifi[1].ssid;
  doc["connectedSsid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String();
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void GlyphWebServer::scanWifi() {
  // Disable Wi-Fi scanning if a friend pairing animation is in progress. (Uses too much CPU and can interfere with timing)
  if (proximityManager.friendPairingBusy()) {
    server_.send(409, "application/json", "{\"error\":\"Friend pairing in progress; retry after the animation\"}");
    return;
  }
  const int count = WiFi.scanNetworks(false, true);
  JsonDocument doc;
  JsonArray networks = doc.to<JsonArray>();
  for (int i = 0; i < count; ++i) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  String body;
  serializeJson(doc, body);
  WiFi.scanDelete();
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendGlyphs() {
  String body;
  body.reserve(GLYPH_COUNT * 42);
  body += '[';
  for (size_t i = 0; i < GLYPH_COUNT; ++i) {
    if (i) body += ',';
    JsonDocument item;
    item["id"] = i;
    item["name"] = GLYPHS[i].name;
    item["pattern"] = GLYPHS[i].pattern;
    String serialized;
    serializeJson(item, serialized);
    body += serialized;
  }
  body += ']';
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendGlyphSequences() {
  String body;
  body.reserve(GLYPH_SEQUENCE_COUNT * 18);
  body += '[';
  for (size_t sequenceIndex = 0; sequenceIndex < GLYPH_SEQUENCE_COUNT; ++sequenceIndex) {
    if (sequenceIndex) body += ',';
    body += '[';
    const GlyphSequenceDefinition &sequence = GLYPH_SEQUENCES[sequenceIndex];
    for (uint8_t item = 0; item < sequence.length; ++item) {
      if (item) body += ',';
      body += String(GLYPH_SEQUENCE_IDS[sequence.offset + item]);
    }
    body += ']';
  }
  body += ']';
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendDisplay() {
  JsonDocument doc;
  switch (displaySettings.mode) {
    case DisplayMode::GlyphRandom:
      doc["mode"] = "random";
      break;
    case DisplayMode::Draw:
      doc["mode"] = "draw";
      break;
    default:
      doc["mode"] = "sequence";
      break;
  }
  doc["interval"] = displaySettings.intervalMs;
  JsonArray sequence = doc["sequence"].to<JsonArray>();
  for (uint8_t i = 0; i < displaySettings.glyphCount; ++i) {
    sequence.add(displaySettings.glyphs[i]);
  }
  auto addColor = [](JsonArray array, const Rgbw &color) {
    array.add(color.r);
    array.add(color.g);
    array.add(color.b);
    array.add(color.w);
  };
  addColor(doc["line"].to<JsonArray>(), displaySettings.lineColor);
  addColor(doc["core"].to<JsonArray>(), displaySettings.coreColor);
  doc["lineBrightness"] = displaySettings.lineBrightnessPercent;
  doc["coreBrightness"] = displaySettings.coreBrightnessPercent;
  effectSettingsToJson(doc["effects"].to<JsonObject>(), displaySettings.effects);
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendLayout() {
  String body = "[";
  for (size_t i = 0; i < LED_COUNT; ++i) {
    if (i) body += ',';
    body += '[' + String(LED_POSITIONS[i].x) + ',' + String(LED_POSITIONS[i].y) + ']';
  }
  body += ']';
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendCustom() {
  JsonDocument doc;
  doc["interval"] = displaySettings.intervalMs;
  JsonArray names = doc["names"].to<JsonArray>();
  JsonArray pages = doc["pages"].to<JsonArray>();
  for (uint8_t pageIndex = 0;
       pageIndex < displaySettings.customPageCount; ++pageIndex) {
    names.add(customPageNames[pageIndex]);
    JsonArray pixels = pages.add<JsonArray>();
    for (size_t ledIndex = 0; ledIndex < LED_COUNT; ++ledIndex) {
      const Rgbw &pixel = customPages[pageIndex][ledIndex];
      JsonArray color = pixels.add<JsonArray>();
      color.add(pixel.r);
      color.add(pixel.g);
      color.add(pixel.b);
      color.add(pixel.w);
    }
  }
  String body;
  body.reserve(displaySettings.customPageCount * LED_COUNT * 15);
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendStorage() {
  auto fileBytes = [](const char *path) -> size_t {
    File file = LittleFS.open(path, "r");
    if (!file) return 0;
    const size_t size = file.size();
    file.close();
    return size;
  };
  const size_t settingsBytes = fileBytes("/global-settings.json") +
                               fileBytes("/global-settings.bak") +
                               fileBytes("/global-settings.tmp") +
                               fileBytes("/utc-time.bin") +
                               fileBytes("/utc-time.tmp");
  const size_t drawBytes = fileBytes("/draw-pages.bin") +
                           fileBytes("/draw-pages.bak") +
                           fileBytes("/draw-pages.tmp");
  const size_t displayBytes = fileBytes("/display-settings.json") +
                              fileBytes("/display-settings.bak") +
                              fileBytes("/display-settings.tmp");
  const size_t totalBytes = LittleFS.totalBytes();
  const size_t usedBytes = LittleFS.usedBytes();
  const size_t peopleBytes = proximityUsersBytes();
  const size_t encounterBytes = proximityEncountersBytes();
  const size_t categorizedBytes = settingsBytes + drawBytes + displayBytes +
                                  peopleBytes + encounterBytes;

  JsonDocument doc;
  doc["total"] = totalBytes;
  doc["used"] = usedBytes;
  doc["available"] = totalBytes > usedBytes ? totalBytes - usedBytes : 0;
  doc["settings"] = settingsBytes;
  doc["draw"] = drawBytes;
  doc["display"] = displayBytes;
  doc["people"] = peopleBytes;
  doc["peopleLimit"] = PROXIMITY_USERS_MAX_BYTES;
  doc["encounters"] = encounterBytes;
  doc["encountersLimit"] = PROXIMITY_HISTORY_MAX_BYTES;
  doc["other"] = usedBytes > categorizedBytes ? usedBytes - categorizedBytes : 0;
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendMisc() {
  JsonDocument doc;
  String remoteEmergencyUsername;
  bool remoteEmergencyFaction = false;
  const bool remoteEmergency = proximityManager.firstRemoteEmergency(
      remoteEmergencyUsername, remoteEmergencyFaction);
  doc["flashlight"] = miscController.mode() == MiscMode::Flashlight;
  doc["emergency"] = miscController.mode() == MiscMode::Emergency;
  doc["remoteEmergency"] = remoteEmergency;
  doc["remoteEmergencyUsername"] = remoteEmergencyUsername;
  doc["remoteEmergencyFaction"] = remoteEmergencyFaction;
  doc["brightness"] = miscController.flashlightBrightness();
  doc["temperature"] = miscController.flashlightTemperature();
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendNearby() {
  JsonDocument doc;
  doc["enabled"] = proximityManager.enabled();
  doc["timeValid"] = utcTimeValid();
  proximityManager.writeNearby(doc["people"].to<JsonArray>());
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void GlyphWebServer::sendPeople() {
  JsonDocument doc;
  proximityManager.writePeople(doc.to<JsonArray>(), server_.arg("q"));
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void GlyphWebServer::exportUsers() {
  server_.sendHeader("Content-Disposition",
                     "attachment; filename=known-users.csv");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/csv; charset=utf-8", "");
  CsvStreamContext context{&server_, "\xef\xbb\xbfuser_id,device_id,faction,friend,username\r\n"};
  forEachKnownUser(exportUserRecord, &context);
  flushCsv(context);
  server_.sendContent("");
}

void GlyphWebServer::exportEncounters() {
  server_.sendHeader("Content-Disposition",
                     "attachment; filename=encounter-history.csv");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/csv; charset=utf-8", "");
  CsvStreamContext context{&server_, "user_id,event,unix_utc\r\n"};
  forEachEncounter(exportEncounterRecord, &context);
  flushCsv(context);
  server_.sendContent("");
}

void GlyphWebServer::resetProximityData() {
  if (!proximityManager.resetStoredData()) {
    server_.send(500, "text/plain", "Could not reset proximity data");
    return;
  }
  server_.send(204);
}

void GlyphWebServer::setFriend() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    server_.send(400, "text/plain", "Invalid friend request");
    return;
  }
  const uint16_t userId = doc["userId"] | 0;
  const String action = doc["action"] | "";
  if (action != "request" && action != "accept" && action != "cancel" && action != "remove") {
    server_.send(400, "text/plain", "Invalid friendship action");
    return;
  }
  if (const char *error = proximityManager.friendshipAction(userId, action)) {
    server_.send(409, "text/plain", error);
    return;
  }
  server_.send(204);
}

void GlyphWebServer::setDisplay() {
  applyDisplay(true);
}

void GlyphWebServer::previewDisplay() {
  applyDisplay(false);
}

void GlyphWebServer::applyDisplay(bool persist) {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    server_.send(400, "text/plain", "Invalid JSON");
    return;
  }
  const DisplaySettings previous = displaySettings;
  const String mode = doc["mode"] | "sequence";
  if (mode == "sequence") {
    displaySettings.mode = DisplayMode::GlyphSequence;
  } else if (mode == "random") {
    displaySettings.mode = DisplayMode::GlyphRandom;
  } else if (mode == "draw") {
    displaySettings.mode = DisplayMode::Draw;
  } else {
    server_.send(400, "text/plain", "Invalid display mode");
    return;
  }
  JsonArrayConst sequence = doc["sequence"];
  if (sequence.size() > MAX_GLYPH_PAGES ||
      (displaySettings.mode == DisplayMode::GlyphSequence &&
       sequence.size() == 0)) {
    displaySettings = previous;
    server_.send(400, "text/plain", "Sequence needs 1 to 5 glyphs");
    return;
  }
  if (sequence.size()) {
    displaySettings.glyphCount = sequence.size();
    for (uint8_t i = 0; i < displaySettings.glyphCount; ++i) {
      displaySettings.glyphs[i] =
          min<uint16_t>(sequence[i] | 0, GLYPH_COUNT - 1);
    }
  }
  displaySettings.intervalMs = constrain(doc["interval"] | DEFAULT_INTERVAL_MS, 100UL, 3600000UL);
  displaySettings.lineColor = jsonColor(doc["line"].as<JsonArrayConst>(), displaySettings.lineColor);
  displaySettings.coreColor = jsonColor(doc["core"].as<JsonArrayConst>(), displaySettings.coreColor);
  displaySettings.lineBrightnessPercent =
      constrain(doc["lineBrightness"] | 100, 0, 100);
  displaySettings.coreBrightnessPercent =
      constrain(doc["coreBrightness"] | 100, 0, 100);
  if (!doc["effects"].isUnbound()) {
    EffectSettings effects;
    if (!effectSettingsFromJson(doc["effects"].as<JsonObjectConst>(), effects)) {
      displaySettings = previous;
      server_.send(400, "text/plain", "Invalid display effects");
      return;
    }
    displaySettings.effects = effects;
  }
  if (persist && !saveDisplaySettings()) {
    displaySettings = previous;
    server_.send(500, "text/plain", "Could not store display settings");
    return;
  }
  ++displayRevision;
  server_.send(204);
}

void GlyphWebServer::setCustom() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    server_.send(400, "text/plain", "Invalid JSON");
    return;
  }
  JsonArrayConst pages = doc["pages"];
  JsonArrayConst names = doc["names"];
  if (pages.size() < 1 || pages.size() > MAX_DRAW_PAGES) {
    server_.send(400, "text/plain", "Drawings need 1 to 10 pages");
    return;
  }
  const uint8_t pageCount = pages.size();
  if (names.size() != pageCount) {
    server_.send(400, "text/plain", "Every page needs a name");
    return;
  }
  for (uint8_t p = 0; p < pageCount; ++p) {
    const String name = String(names[p] | "");
    if (name.isEmpty() || name.length() > DRAW_PAGE_NAME_MAX_BYTES) {
      server_.send(400, "text/plain", "Page names must contain 1 to 64 bytes");
      return;
    }
    if (pages[p].as<JsonArrayConst>().size() != LED_COUNT) {
      server_.send(400, "text/plain", "Every page needs 69 pixels");
      return;
    }
  }

  Rgbw previousPages[MAX_DRAW_PAGES][LED_COUNT];
  memcpy(previousPages, customPages, sizeof(customPages));
  String previousNames[MAX_DRAW_PAGES];
  for (uint8_t p = 0; p < MAX_DRAW_PAGES; ++p) {
    previousNames[p] = customPageNames[p];
  }
  const uint8_t previousCount = displaySettings.customPageCount;

  displaySettings.customPageCount = pageCount;
  for (uint8_t p = 0; p < pageCount; ++p) {
    customPageNames[p] = String(names[p] | "");
    JsonArrayConst pixels = pages[p];
    for (size_t i = 0; i < LED_COUNT; ++i) {
      customPages[p][i] = jsonColor(pixels[i].as<JsonArrayConst>(), {});
    }
  }
  if (!saveDrawPages()) {
    memcpy(customPages, previousPages, sizeof(customPages));
    for (uint8_t p = 0; p < MAX_DRAW_PAGES; ++p) {
      customPageNames[p] = previousNames[p];
    }
    displaySettings.customPageCount = previousCount;
    server_.send(500, "text/plain", "Could not save draw pages");
    return;
  }
  ++displayRevision;
  server_.send(204);
}

void GlyphWebServer::setBrightness() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    server_.send(400, "text/plain", "Invalid brightness");
    return;
  }
  globalSettings.brightnessPercent = constrain(doc["brightness"] | 100, 0, 100);
  updatePowerLimit();
  ++displayRevision;
  server_.send(204);
}

void GlyphWebServer::setFlashlight() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    server_.send(400, "text/plain", "Invalid flashlight settings");
    return;
  }
  miscController.setFlashlightBrightness(
      constrain(doc["brightness"] | 60, 0, 100));
  miscController.setFlashlightTemperature(
      constrain(doc["temperature"] | 50, 0, 100));
  if (!miscController.setFlashlight(doc["enabled"] | false)) {
    server_.send(409, "text/plain", "Disable Emergency with both buttons before using Flashlight");
    return;
  }
  server_.send(204);
}

void GlyphWebServer::setUtcFromBrowser() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) ||
      !setUtcTime(doc["timestamp"] | 0UL)) {
    server_.send(400, "text/plain", "Invalid UTC timestamp");
    return;
  }
  proximityManager.onTimeSynchronized();
  server_.send(204);
}

void GlyphWebServer::applySettings() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    server_.send(400, "text/plain", "Invalid settings");
    return;
  }
  const GlobalSettings previous = globalSettings;
  const String newApSsid = String(doc["apSsid"] | "").substring(0, 32);
  const String newApPassword =
      String(doc["apPassword"] | "").substring(0, 63);
  if (newApSsid.isEmpty() ||
      (!newApPassword.isEmpty() && newApPassword.length() < 8)) {
    server_.send(400, "text/plain",
                 "Hotspot SSID is required and password must be 8 to 63 bytes");
    return;
  }
  const String newUsername = String(doc["username"] | "Agent");
  if (newUsername.length() > REMOTE_USERNAME_MAX_BYTES) {
    server_.send(400, "text/plain", "Username must be at most 64 UTF-8 bytes");
    return;
  }
  const uint8_t hardwareRevision =
      doc["hardwareRevision"] | previous.hardwareRevision;
  if (hardwareRevision != 1 && hardwareRevision != 2) {
    server_.send(400, "text/plain", "Hardware revision must be 1 or 2");
    return;
  }
  globalSettings.username = newUsername;
  globalSettings.apSsid = newApSsid;
  if (!newApPassword.isEmpty()) globalSettings.apPassword = newApPassword;
  globalSettings.enlightened = doc["enlightened"] | false;
  globalSettings.use24HourTime =
      doc["use24HourTime"] | previous.use24HourTime;
  globalSettings.bleBroadcastEnabled =
      doc["bleBroadcastEnabled"] | previous.bleBroadcastEnabled;
  globalSettings.friendModeEnabled =
      doc["friendModeEnabled"] | previous.friendModeEnabled;
  globalSettings.hardwareRevision = hardwareRevision;
  if (globalSettings.enlightened != previous.enlightened) {
    applyFactionColorDefaults(globalSettings.enlightened);
  }
  const uint16_t rotation = doc["rotation"] | 0;
  if (rotation != 0 && rotation != 180) {
    globalSettings = previous;
    applyFactionColorDefaults(previous.enlightened);
    server_.send(400, "text/plain", "Rotation must be 0 or 180");
    return;
  }
  globalSettings.rotationQuarterTurns = rotation / 90;
  const uint32_t hardwareLimitMw = hardwareLedLimitMw(hardwareRevision);
  globalSettings.minPowerMw = constrain(
      static_cast<uint32_t>((doc["minW"] | 0.0f) * 1000.0f), 0UL,
      hardwareLimitMw);
  globalSettings.maxPowerMw = constrain(
      static_cast<uint32_t>((doc["maxW"] |
                             (hardwareLimitMw / 1000.0f)) *
                            1000.0f),
      globalSettings.minPowerMw, hardwareLimitMw);
  globalSettings.brightnessPercent = constrain(doc["brightness"] | 100, 0, 100);

  for (uint8_t i = 0; i < 2; ++i) {
    const char *ssidKey = i == 0 ? "wifi1" : "wifi2";
    const char *passKey = i == 0 ? "password1" : "password2";
    const String newSsid = String(doc[ssidKey] | "").substring(0, 32);
    const String newPassword = String(doc[passKey] | "").substring(0, 64);
    if (newSsid != globalSettings.wifi[i].ssid) {
      globalSettings.wifi[i].ssid = newSsid;
      globalSettings.wifi[i].password = newPassword;
    } else if (!newPassword.isEmpty()) {
      globalSettings.wifi[i].password = newPassword;
    }
    if (newSsid.isEmpty()) globalSettings.wifi[i].password = "";
  }
  if (!saveSettings()) {
    globalSettings = previous;
    applyFactionColorDefaults(previous.enlightened);
    server_.send(500, "text/plain", "Could not store globalSettings");
    return;
  }
  applyStationHostname();
  if (hotspotActive_) {
    const String hostname = networkHostname();
    WiFi.softAPsetHostname(hostname.c_str());
  }
  updatePowerLimit();
  proximityManager.setEnabled(globalSettings.bleBroadcastEnabled);
  proximityManager.refreshIdentity();
  ++displayRevision;
  if (globalSettings.hardwareRevision != previous.hardwareRevision) {
    persistUtcTime();
    server_.send(202, "application/json",
                 "{\"saved\":true,\"restarting\":true}");
    restartAtMs_ = millis() + 1200;
    return;
  }
  server_.send(204);
}

void GlyphWebServer::restartController() {
  persistUtcTime();
  server_.send(202, "application/json", "{\"restarting\":true}");
  restartAtMs_ = millis() + 1200;
}

// OTA update and abort handling is based on the ArduinoOTA example from the ESP32 core:
void GlyphWebServer::handleOtaUpload() {
  HTTPUpload &upload = server_.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START: {
      otaReady_ = false;
      otaFailed_ = false;
      otaError_ = "";
      String filename = upload.filename;
      filename.toLowerCase();
      if (!filename.endsWith(".bin")) {
        otaFailed_ = true;
        otaError_ = "Select a PlatformIO firmware.bin file";
        return;
      }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        otaFailed_ = true;
        otaError_ = "Could not start firmware update";
      }
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (!otaFailed_ &&
          Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        otaFailed_ = true;
        otaError_ = "Firmware write failed";
        Update.abort();
      }
      break;
    case UPLOAD_FILE_END:
      if (!otaFailed_) {
        if (Update.end(true)) {
          otaReady_ = true;
        } else {
          otaFailed_ = true;
          otaError_ = "Firmware validation failed";
        }
      }
      break;
    case UPLOAD_FILE_ABORTED:
      Update.abort();
      otaFailed_ = true;
      otaError_ = "Firmware upload was cancelled";
      break;
    default:
      break;
  }
}

void GlyphWebServer::finishOta() {
  if (otaFailed_ || !otaReady_) {
    const String message = otaError_.isEmpty() ? "Incomplete firmware upload" : otaError_;
    server_.send(400, "text/plain", message);
    return;
  }
  persistUtcTime();
  server_.send(200, "application/json", "{\"updated\":true,\"restarting\":true}");
  otaReady_ = false;
  restartAtMs_ = millis() + 1200;
}
