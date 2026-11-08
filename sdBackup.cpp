// Optional microSD support (LilyGO T-Dongle-S3 style slot, SD_MMC).
// The app keeps working from flash (LittleFS) if no card is present.
// With a card: saves the log to /sdcard/data/log.txt and backs up / restores
// the blocklist snapshot and custom blocklist under /sdcard/backup.

#include "appGlobals.h"

// SD_MMC pins of the GNPE Pocket-Dongle-S3 TF slot (from vendor schematic ESP32-S3-0.96LCD.pdf)
#define SD_CLK 17
#define SD_CMD 18
#define SD_D0  16
#define SD_D1  15
#define SD_D2  48
#define SD_D3  47

#define SD_BACKUP_DIR "/backup"
#define SD_LOG_MAX (16UL * 1024 * 1024) // restart log when bigger than this

static bool sdOk = false;

bool sdMounted() { return sdOk; }

static void rotateLog() {
  const char* p = "/sdcard" LOG_FILE_PATH;
  struct stat st;
  if (stat(p, &st) == 0 && (unsigned long)st.st_size > SD_LOG_MAX) {
    SD_MMC.remove(LOG_FILE_PATH ".old");
    SD_MMC.rename(LOG_FILE_PATH, LOG_FILE_PATH ".old");
  }
}

void sdStart() {
  // 4 bit mode first, then 1 bit fallback
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  sdOk = SD_MMC.begin("/sdcard", false, false, 20000);
  if (!sdOk) {
    SD_MMC.end();
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
    sdOk = SD_MMC.begin("/sdcard", true, false, 20000);
  }
  if (!sdOk || SD_MMC.cardType() == CARD_NONE) {
    sdOk = false;
    LOG_WRN("No microSD card, running from flash only");
    return;
  }
  LOG_INF("microSD mounted, size %s", fmtSize(SD_MMC.cardSize()));
  SD_MMC.mkdir(DATA_DIR);
  SD_MMC.mkdir(SD_BACKUP_DIR);
  SD_MMC.mkdir(SD_BACKUP_DIR DATA_DIR);
  rotateLog();
}

void sdLogStart() {
  if (!sdOk) return;
  sdLog = true; // log to SD (as well as RAM / web)
  remote_log_init();
}

static bool copyFile(fs::FS& from, const char* src, fs::FS& to, const char* dst) {
  File in = from.open(src, FILE_READ);
  if (!in || in.isDirectory()) return false;
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
  to.remove(tmp);
  File out = to.open(tmp, FILE_WRITE);
  if (!out) { in.close(); return false; }
  const size_t BUFSZ = 4096;
  uint8_t* buf = (uint8_t*)malloc(BUFSZ);
  if (!buf) { in.close(); out.close(); return false; }
  bool ok = true;
  size_t total = 0;
  while (in.available()) {
    size_t n = in.read(buf, BUFSZ);
    if (n == 0) break;
    if (out.write(buf, n) != n) { ok = false; break; }
    total += n;
    delay(0);
  }
  free(buf);
  size_t expect = in.size();
  in.close();
  out.close();
  if (ok && total == expect) {
    to.remove(dst);
    ok = to.rename(tmp, dst);
  } else ok = false;
  if (!ok) to.remove(tmp);
  return ok;
}

// copy flash file to SD backup
bool sdBackupFile(const char* path) {
  if (!sdOk || !STORAGE.exists(path)) return false;
  char dst[96];
  snprintf(dst, sizeof(dst), SD_BACKUP_DIR "%s", path);
  bool ok = copyFile(STORAGE, path, SD_MMC, dst);
  if (ok) LOG_INF("Backed up %s to SD", path);
  else LOG_WRN("SD backup of %s failed", path);
  return ok;
}

// restore file from SD backup only if it is missing in flash
bool sdRestoreFile(const char* path) {
  if (!sdOk || STORAGE.exists(path)) return false;
  char src[96];
  snprintf(src, sizeof(src), SD_BACKUP_DIR "%s", path);
  if (!SD_MMC.exists(src)) return false;
  bool ok = copyFile(SD_MMC, src, STORAGE, path);
  if (ok) LOG_INF("Restored %s from SD backup", path);
  else LOG_WRN("SD restore of %s failed", path);
  return ok;
}
