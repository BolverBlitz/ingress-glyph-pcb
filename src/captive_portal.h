#pragma once

#include <DNSServer.h>
#include <WebServer.h>

class GlyphWebServer {
 public:
  void begin();
  void loop();

 private:
  DNSServer dns_;
  WebServer server_{80};
  uint32_t restartAtMs_ = 0;
  uint32_t wifiAttemptStartedMs_ = 0;
  int8_t wifiProfileIndex_ = -1;
  int16_t lastWifiStatus_ = -1;
  bool wifiBssidKnown_[2] = {};
  int32_t wifiChannel_[2] = {};
  uint8_t wifiBssid_[2][6] = {};
  bool hotspotActive_ = false;
  bool otaReady_ = false;
  bool otaFailed_ = false;
  String otaError_;
  void registerRoutes();
  void startNextWifiProfile();
  void manageWifi();
  void startHotspot();
  void stopHotspot();
  void sendStatus();
  void sendSettings();
  void scanWifi();
  void sendGlyphs();
  void sendGlyphSequences();
  void sendDisplay();
  void sendLayout();
  void sendCustom();
  void sendStorage();
  void sendMisc();
  void sendNearby();
  void sendPeople();
  void exportUsers();
  void exportEncounters();
  void resetProximityData();
  void setFriend();
  void setDisplay();
  void previewDisplay();
  void applyDisplay(bool persist);
  void setCustom();
  void setBrightness();
  void setFlashlight();
  void setUtcFromBrowser();
  void applySettings();
  void restartController();
  void handleOtaUpload();
  void finishOta();
};
