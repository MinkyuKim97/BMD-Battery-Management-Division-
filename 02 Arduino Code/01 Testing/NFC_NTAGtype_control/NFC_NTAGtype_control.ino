#include <Wire.h>
#include <Adafruit_PN532.h>

#define PN532_IRQ    2
#define PN532_RESET  3
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// 스위치 게이트(ON일 때만 NFC 동작)
#define NFC_ENABLE_PIN 4   // 스위치 한쪽 D4, 다른쪽 GND (INPUT_PULLUP)

// ===== 우리가 사용할 저장 영역(짧게! 성공률↑) =====
// page 4: header(4 bytes)
// page 5~9: payload (5 pages * 4 = 20 bytes)
static const uint8_t START_PAGE = 4;
static const uint8_t END_PAGE   = 9;

static const uint8_t SIG0 = 'M';
static const uint8_t SIG1 = 'K';

// 쓰기 문자열(String)
String g_writeText = String("Minkyu Kim");

// ---- 태그 유지 체크 ----
bool tagHeld = false;
uint8_t lastUid[7] = {0};
uint8_t lastUidLen = 0;
uint32_t lastSeenMs = 0;

// ---- 유틸 ----
static void printHex(const uint8_t* data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    if (i != len - 1) Serial.print(" ");
  }
}

static bool uidEqual(const uint8_t* a, uint8_t alen, const uint8_t* b, uint8_t blen) {
  if (alen != blen) return false;
  for (uint8_t i = 0; i < alen; i++) if (a[i] != b[i]) return false;
  return true;
}

// ---- 안정성: 페이지 read/write retry ----
bool readPageRetry(uint8_t page, uint8_t out4[4], int tries = 8) {
  for (int i = 0; i < tries; i++) {
    if (nfc.ntag2xx_ReadPage(page, out4)) return true;
    delay(12);
  }
  return false;
}

bool writePageRetry(uint8_t page, const uint8_t in4[4], int tries = 8) {
  for (int i = 0; i < tries; i++) {
    if (nfc.ntag2xx_WritePage(page, in4)) return true;
    delay(12);
  }
  return false;
}

// ---- 태그가 “진짜 안정적으로 붙어있음” 확인 (연속 3회 동일 UID) ----
bool waitStableTag(uint8_t uidOut[7], uint8_t &uidLenOut, uint32_t timeoutMs = 2000) {
  uint8_t uid[7]; uint8_t uidLen = 0;
  uint8_t last[7]; uint8_t lastLen = 0;
  int stableCount = 0;

  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 120);
    if (found) {
      if (stableCount == 0) {
        memcpy(last, uid, uidLen);
        lastLen = uidLen;
        stableCount = 1;
      } else {
        bool same = uidEqual(uid, uidLen, last, lastLen);
        if (same) stableCount++;
        else {
          memcpy(last, uid, uidLen);
          lastLen = uidLen;
          stableCount = 1;
        }

        if (stableCount >= 3) {
          memcpy(uidOut, last, lastLen);
          uidLenOut = lastLen;
          delay(80); // 감지 후 RF 안정화
          return true;
        }
      }
    }
    delay(30);
  }
  return false;
}

// ---- (2) 기존 데이터 삭제(우리 구역만) ----
bool clearUserRegion() {
  uint8_t zero4[4] = {0,0,0,0};
  for (uint8_t p = START_PAGE; p <= END_PAGE; p++) {
    if (!writePageRetry(p, zero4)) return false;
    delay(8);
  }
  return true;
}

// ---- String 쓰기 ----
bool writeTextToTag(const String &text) {
  size_t len = text.length();
  if (len > 255) len = 255;

  uint16_t payloadCap = (uint16_t)((END_PAGE - START_PAGE) * 4); // header 제외 payload
  if (len > payloadCap) {
    Serial.print("[WRITE] Too long. cap=");
    Serial.println(payloadCap);
    return false;
  }

  if (!clearUserRegion()) {
    Serial.println("[WRITE] clearUserRegion failed");
    return false;
  }

  // header
  uint8_t header[4] = { SIG0, SIG1, (uint8_t)len, 0x00 };
  if (!writePageRetry(START_PAGE, header, 12)) {  // 헤더는 더 강하게 retry
    Serial.println("[WRITE] header write failed");
    return false;
  }
  delay(10);

  // payload
  const char* pch = text.c_str();
  uint16_t idx = 0;
  for (uint8_t p = START_PAGE + 1; p <= END_PAGE; p++) {
    uint8_t buf[4] = {0,0,0,0};
    for (uint8_t i = 0; i < 4; i++) {
      if (idx < len) buf[i] = (uint8_t)pch[idx++];
    }
    if (!writePageRetry(p, buf)) {
      Serial.print("[WRITE] payload write failed at page ");
      Serial.println(p);
      return false;
    }
    delay(10);
    if (idx >= len) break;
  }

  Serial.println("[WRITE] OK");
  return true;
}

// ---- String 읽기 ----
bool readTextFromTag(String &outText) {
  outText = "";

  uint8_t header[4];
  if (!readPageRetry(START_PAGE, header, 12)) return false; // 헤더 강화

  if (header[0] != SIG0 || header[1] != SIG1) return false;

  uint8_t len = header[2];
  uint16_t payloadCap = (uint16_t)((END_PAGE - START_PAGE) * 4);
  if (len > payloadCap) return false;

  outText.reserve(len);
  uint16_t idx = 0;

  for (uint8_t p = START_PAGE + 1; p <= END_PAGE; p++) {
    uint8_t buf[4];
    if (!readPageRetry(p, buf)) return false;

    for (uint8_t i = 0; i < 4; i++) {
      if (idx < len) outText += (char)buf[i];
      idx++;
      if (idx >= len) return true;
    }
    delay(4);
  }

  return true;
}

// 전체 읽기 “robust” 재시도
bool readTextFromTagRobust(String &outText) {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (readTextFromTag(outText)) return true;
    delay(60);
  }
  return false;
}

// ---- 스위치 게이트 ----
bool nfcEnabled() {
  // INPUT_PULLUP: 스위치 ON(=GND 연결) -> LOW
  return (digitalRead(NFC_ENABLE_PIN) == LOW);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  pinMode(NFC_ENABLE_PIN, INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(25000); // ✅ 더 안정적으로

  Serial.println("PN532 I2C NTAG (improved reliability)");
  Serial.print("Write text = ");
  Serial.println(g_writeText);
  Serial.println("Switch ON to enable NFC. Tap tag to READ->WRITE->VERIFY once (hold until done).");

  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) {
    Serial.println("PN532 not found. Check wiring + I2C mode.");
    while (1) delay(10);
  }else{
    Serial.println("PN532 Found");
  }
  nfc.SAMConfig();
}

void loop() {
  if (!nfcEnabled()) {
    // OFF일 땐 상태 리셋해서 ON 직후 깔끔하게 1회 동작하도록
    tagHeld = false;
    lastUidLen = 0;
    Serial.println("Switch ON");
    delay(50);
    return;
  }

  // 먼저 안정 태그를 기다림(짧게)
  uint8_t uid[7]; uint8_t uidLen = 0;
  bool stable = waitStableTag(uid, uidLen, 1500);
  if (!stable) {
    delay(20);
    return;
  }

  lastSeenMs = millis();

  // 같은 태그를 계속 대고 있으면 반복 처리 방지
  if (tagHeld && uidEqual(uid, uidLen, lastUid, lastUidLen)) {
    return;
  }

  tagHeld = true;
  memcpy(lastUid, uid, uidLen);
  lastUidLen = uidLen;

  Serial.print("\nUID: ");
  printHex(uid, uidLen);
  Serial.println();

  // READ
  String before;
  if (readTextFromTagRobust(before)) {
    Serial.print("[READ] text = ");
    Serial.println(before);
  } else {
    Serial.println("[READ] (failed or no valid MK data)");
  }

  // WRITE(덮어쓰기)
  Serial.print("[WRITE] overwrite with: ");
  Serial.println(g_writeText);

  if (writeTextToTag(g_writeText)) {
    String after;
    if (readTextFromTagRobust(after)) {
      Serial.print("[VERIFY] text = ");
      Serial.println(after);
    } else {
      Serial.println("[VERIFY] read failed");
    }
  } else {
    Serial.println("[WRITE] failed");
  }

  Serial.println("Remove tag to allow next detection.");

  // 태그를 떼기 전까지는 다시 실행하지 않도록
  // (여기서 조금 쉬어주면 I2C/RF도 안정됨)
  delay(150);
}