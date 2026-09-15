#include "draw_store.h"

#include <LittleFS.h>

#include "app_state.h"
#include "config.h"

namespace {
constexpr char DRAW_FILE[] = "/draw-pages.bin";
constexpr char DRAW_BACKUP[] = "/draw-pages.bak";
constexpr char DRAW_TEMP[] = "/draw-pages.tmp";
constexpr uint32_t DRAW_MAGIC = 0x44524157UL;  // DRAW
constexpr uint16_t DRAW_VERSION = 2;
constexpr uint16_t LEGACY_DRAW_VERSION = 1;
constexpr uint8_t LEGACY_MAX_DRAW_PAGES = 5;

#pragma pack(push, 1)
struct LegacyStoredDrawPages {
  uint32_t magic;
  uint16_t version;
  uint8_t pageCount;
  uint8_t reserved;
  uint32_t intervalMs;
  uint8_t nameLength[LEGACY_MAX_DRAW_PAGES];
  char names[LEGACY_MAX_DRAW_PAGES][DRAW_PAGE_NAME_MAX_BYTES];
  Rgbw pixels[LEGACY_MAX_DRAW_PAGES][LED_COUNT];
  uint32_t crc;
};

struct StoredDrawPages {
  uint32_t magic;
  uint16_t version;
  uint8_t pageCount;
  uint8_t reserved;
  uint32_t intervalMs;
  uint8_t nameLength[MAX_DRAW_PAGES];
  char names[MAX_DRAW_PAGES][DRAW_PAGE_NAME_MAX_BYTES];
  Rgbw pixels[MAX_DRAW_PAGES][LED_COUNT];
  uint32_t crc;
};
#pragma pack(pop)

// These records are too large for the ESP32 Arduino loop-task stack,
// especially while an HTTP save request is active.
StoredDrawPages storedBuffer;
LegacyStoredDrawPages legacyBuffer;

// Compute CRC32 using the polynomial 0xEDB88320 (reversed 0x04C11DB7).
uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

void setDefaultDrawPages() {
  memset(customPages, 0, sizeof(customPages));
  for (uint8_t i = 0; i < MAX_DRAW_PAGES; ++i) {
    customPageNames[i] = "Page " + String(i + 1);
  }
  displaySettings.customPageCount = 1;
}

void toStored(StoredDrawPages &stored) {
  memset(&stored, 0, sizeof(stored));
  stored.magic = DRAW_MAGIC;
  stored.version = DRAW_VERSION;
  stored.pageCount =
      constrain(displaySettings.customPageCount, 1, MAX_DRAW_PAGES);
  stored.intervalMs = displaySettings.intervalMs;
  memcpy(stored.pixels, customPages, sizeof(customPages));
  for (uint8_t i = 0; i < MAX_DRAW_PAGES; ++i) {
    const size_t length = min(customPageNames[i].length(),
                              DRAW_PAGE_NAME_MAX_BYTES);
    stored.nameLength[i] = length;
    memcpy(stored.names[i], customPageNames[i].c_str(), length);
  }
  stored.crc = crc32(reinterpret_cast<const uint8_t *>(&stored),
                     offsetof(StoredDrawPages, crc));
}

bool readStored(const char *path, StoredDrawPages &stored) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() != sizeof(stored)) {
    if (file) file.close();
    return false;
  }
  const size_t read = file.read(reinterpret_cast<uint8_t *>(&stored),
                                sizeof(stored));
  file.close();
  if (read != sizeof(stored) || stored.magic != DRAW_MAGIC ||
      stored.version != DRAW_VERSION || stored.pageCount < 1 ||
      stored.pageCount > MAX_DRAW_PAGES) {
    return false;
  }
  return stored.crc ==
         crc32(reinterpret_cast<const uint8_t *>(&stored),
               offsetof(StoredDrawPages, crc));
}

bool readLegacyStored(const char *path, LegacyStoredDrawPages &stored) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() != sizeof(stored)) {
    if (file) file.close();
    return false;
  }
  const size_t read =
      file.read(reinterpret_cast<uint8_t *>(&stored), sizeof(stored));
  file.close();
  if (read != sizeof(stored) || stored.magic != DRAW_MAGIC ||
      stored.version != LEGACY_DRAW_VERSION || stored.pageCount < 1 ||
      stored.pageCount > LEGACY_MAX_DRAW_PAGES) {
    return false;
  }
  return stored.crc ==
         crc32(reinterpret_cast<const uint8_t *>(&stored),
               offsetof(LegacyStoredDrawPages, crc));
}

void fromStored(const StoredDrawPages &stored) {
  displaySettings.customPageCount = stored.pageCount;
  memcpy(customPages, stored.pixels, sizeof(customPages));
  for (uint8_t i = 0; i < MAX_DRAW_PAGES; ++i) {
    const size_t length = min<size_t>(stored.nameLength[i],
                                      DRAW_PAGE_NAME_MAX_BYTES);
    customPageNames[i] = String(stored.names[i], length);
    if (customPageNames[i].isEmpty()) {
      customPageNames[i] = "Page " + String(i + 1);
    }
  }
}

void fromLegacyStored(const LegacyStoredDrawPages &stored) {
  displaySettings.customPageCount = stored.pageCount;
  memcpy(customPages, stored.pixels, sizeof(stored.pixels));
  for (uint8_t i = 0; i < LEGACY_MAX_DRAW_PAGES; ++i) {
    const size_t length = min<size_t>(stored.nameLength[i],
                                      DRAW_PAGE_NAME_MAX_BYTES);
    customPageNames[i] = String(stored.names[i], length);
    if (customPageNames[i].isEmpty()) {
      customPageNames[i] = "Page " + String(i + 1);
    }
  }
}
}  // namespace

void loadDrawPages() {
  setDefaultDrawPages();
  if (!LittleFS.begin(true)) return;
  StoredDrawPages &stored = storedBuffer;
  if (readStored(DRAW_FILE, stored) || readStored(DRAW_BACKUP, stored)) {
    fromStored(stored);
    return;
  }
  LegacyStoredDrawPages &legacy = legacyBuffer;
  if (readLegacyStored(DRAW_FILE, legacy) ||
      readLegacyStored(DRAW_BACKUP, legacy)) {
    fromLegacyStored(legacy);
  }
}

bool saveDrawPages() {
  if (!LittleFS.begin(true)) return false;
  StoredDrawPages &stored = storedBuffer;
  toStored(stored);

  LittleFS.remove(DRAW_TEMP);
  File file = LittleFS.open(DRAW_TEMP, "w");
  if (!file) return false;
  const size_t written =
      file.write(reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
  file.flush();
  file.close();
  if (written != sizeof(stored)) {
    LittleFS.remove(DRAW_TEMP);
    return false;
  }

  if (!readStored(DRAW_TEMP, stored)) {
    LittleFS.remove(DRAW_TEMP);
    return false;
  }

  LittleFS.remove(DRAW_BACKUP);
  if (LittleFS.exists(DRAW_FILE) &&
      !LittleFS.rename(DRAW_FILE, DRAW_BACKUP)) {
    LittleFS.remove(DRAW_TEMP);
    return false;
  }
  if (!LittleFS.rename(DRAW_TEMP, DRAW_FILE)) return false;
  return readStored(DRAW_FILE, stored);
}
