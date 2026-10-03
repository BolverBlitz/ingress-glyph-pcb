#include "serial_config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ctype.h>
#include <esp_system.h>

#include "app_state.h"
#include "config.h"
#include "settings_store.h"

namespace {

constexpr size_t SERIAL_CONFIG_LINE_BYTES = 192;
char inputLine[SERIAL_CONFIG_LINE_BYTES];
size_t inputLength = 0;

void printHelp() {
  Serial.println("Serial configuration commands:");
  Serial.println("  show");
  Serial.println("  ip");
  Serial.println("  rev <1|2>");
  Serial.println("  wifi <1|2> \"SSID\" [\"password\"]");
  Serial.println("  wifi-clear <1|2>");
  Serial.println("Quotes allow spaces; passwords are never printed.");
}

void printIpAddress() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Device IP: ");
    Serial.print(WiFi.localIP());
    Serial.println(" (Wi-Fi)");
    return;
  }
  const IPAddress hotspotIp = WiFi.softAPIP();
  if (hotspotIp != IPAddress(0, 0, 0, 0)) {
    Serial.print("Device IP: ");
    Serial.print(hotspotIp);
    Serial.println(" (fallback hotspot)");
    return;
  }
  Serial.println("Device IP: unavailable");
}

void printStatus() {
  Serial.printf("Config: firmware %s, hardware revision %u\n",
                FIRMWARE_VERSION, globalSettings.hardwareRevision);
  for (uint8_t i = 0; i < 2; ++i) {
    const WifiProfile &profile = globalSettings.wifi[i];
    Serial.printf("Config: Wi-Fi %u: %s%s\n", i + 1,
                  profile.ssid.isEmpty() ? "<empty>" : profile.ssid.c_str(),
                  profile.password.isEmpty() ? "" : " (password set)");
  }
}

bool tokenize(const String &line, String *tokens, uint8_t &count) {
  count = 0;
  size_t position = 0;
  while (position < line.length()) {
    while (position < line.length() &&
           isspace(static_cast<unsigned char>(line[position]))) {
      ++position;
    }
    if (position == line.length()) break;
    if (count >= 4) return false;

    String &token = tokens[count++];
    token = "";
    const char quote = line[position] == '"' || line[position] == '\''
                           ? line[position++]
                           : '\0';
    bool closed = quote == '\0';
    while (position < line.length()) {
      const char character = line[position++];
      if (quote && character == quote) {
        closed = true;
        break;
      }
      if (!quote && isspace(static_cast<unsigned char>(character))) break;
      if (character == '\\' && position < line.length() && quote) {
        token += line[position++];
      } else {
        token += character;
      }
    }
    if (!closed) return false;
    if (quote && position < line.length() &&
        !isspace(static_cast<unsigned char>(line[position]))) {
      return false;
    }
  }
  return true;
}

void restartAfterSave() {
  Serial.println("Config saved. Restarting controller...");
  Serial.flush();
  delay(100);
  ESP.restart();
}

void setRevision(const String &value) {
  if (value != "1" && value != "2") {
    Serial.println("Error: use rev 1 or rev 2.");
    return;
  }
  const GlobalSettings previous = globalSettings;
  globalSettings.hardwareRevision = value.toInt();
  const uint32_t limit =
      hardwareLedLimitMw(globalSettings.hardwareRevision);
  globalSettings.minPowerMw = min(globalSettings.minPowerMw, limit);
  globalSettings.maxPowerMw = min(globalSettings.maxPowerMw, limit);
  if (!saveSettings()) {
    globalSettings = previous;
    Serial.println("Error: could not save the hardware revision.");
    return;
  }
  restartAfterSave();
}

bool parseWifiSlot(const String &value, uint8_t &index) {
  if (value != "1" && value != "2") return false;
  index = value.toInt() - 1;
  return true;
}

void setWifi(const String &slotValue, const String &ssid,
             const String &password) {
  uint8_t index = 0;
  if (!parseWifiSlot(slotValue, index)) {
    Serial.println("Error: Wi-Fi slot must be 1 or 2.");
    return;
  }
  if (ssid.isEmpty() || ssid.length() > 32) {
    Serial.println("Error: SSID must contain 1 to 32 bytes.");
    return;
  }
  if (password.length() > 64 ||
      (!password.isEmpty() && password.length() < 8)) {
    Serial.println("Error: password must be empty or contain 8 to 64 bytes.");
    return;
  }
  const GlobalSettings previous = globalSettings;
  globalSettings.wifi[index].ssid = ssid;
  globalSettings.wifi[index].password = password;
  if (!saveSettings()) {
    globalSettings = previous;
    Serial.println("Error: could not save the Wi-Fi profile.");
    return;
  }
  restartAfterSave();
}

void clearWifi(const String &slotValue) {
  uint8_t index = 0;
  if (!parseWifiSlot(slotValue, index)) {
    Serial.println("Error: Wi-Fi slot must be 1 or 2.");
    return;
  }
  const GlobalSettings previous = globalSettings;
  globalSettings.wifi[index] = {};
  if (!saveSettings()) {
    globalSettings = previous;
    Serial.println("Error: could not clear the Wi-Fi profile.");
    return;
  }
  restartAfterSave();
}

void handleLine(String line) {
  line.trim();
  if (line.isEmpty()) return;

  String tokens[4];
  uint8_t count = 0;
  if (!tokenize(line, tokens, count) || !count) {
    Serial.println("Error: invalid command or quoting. Type help.");
    return;
  }
  tokens[0].toLowerCase();
  if (tokens[0] == "help" && count == 1) {
    printHelp();
  } else if (tokens[0] == "show" && count == 1) {
    printStatus();
  } else if (tokens[0] == "ip" && count == 1) {
    printIpAddress();
  } else if (tokens[0] == "rev" && count == 2) {
    setRevision(tokens[1]);
  } else if (tokens[0] == "wifi" && (count == 3 || count == 4)) {
    setWifi(tokens[1], tokens[2], count == 4 ? tokens[3] : String());
  } else if (tokens[0] == "wifi-clear" && count == 2) {
    clearWifi(tokens[1]);
  } else {
    Serial.println("Error: invalid command. Type help.");
  }
}

}  // namespace

void beginSerialConfig() {
  inputLength = 0;
  Serial.println("Serial configuration ready. Type help.");
}

void loopSerialConfig() {
  while (Serial.available()) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\r' || character == '\n') {
      if (inputLength) {
        inputLine[inputLength] = '\0';
        handleLine(String(inputLine));
        inputLength = 0;
      }
      continue;
    }
    if (character == '\b' || character == 0x7f) {
      if (inputLength) --inputLength;
      continue;
    }
    if (inputLength + 1 < SERIAL_CONFIG_LINE_BYTES) {
      inputLine[inputLength++] = character;
    } else {
      inputLength = 0;
      Serial.println("Error: command is too long.");
    }
  }
}
