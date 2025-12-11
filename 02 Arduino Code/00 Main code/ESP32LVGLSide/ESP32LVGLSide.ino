#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_PN532.h>

#include "secrets.h"

// ----------------------------------------------------
// ESP32-32E 3.5" pin setting (TFT + Touch)
// ----------------------------------------------------
#define TFT_CS   15
#define TFT_DC   2
#define TFT_SCK  14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_BL   27
#define TFT_RST  GFX_NOT_DEFINED

// Touch (XPT2046)
#define TP_CS    33
#define TP_IRQ   36

// ===== touch calib =====
#define TS_MINX  200
#define TS_MAXX  3800
#define TS_MINY  200
#define TS_MAXY  3800

#define TOUCH_SWAP_XY   true
#define TOUCH_INVERT_X  true
#define TOUCH_INVERT_Y  false

const uint32_t debounceMs = 220;

// HSPI (TFT + Touch)
SPIClass hspi(HSPI);

// GFX (ST7796 SPI) on HSPI
Arduino_DataBus *bus = new Arduino_ESP32SPI(
  TFT_DC, TFT_CS,
  TFT_SCK, TFT_MOSI, TFT_MISO,
  HSPI
);
Arduino_GFX *gfx = new Arduino_ST7796(bus, TFT_RST, 0);

// Touch
XPT2046_Touchscreen ts(TP_CS, TP_IRQ);
// touch calib helper
int16_t obsMinX = 4095, obsMaxX = 0, obsMinY = 4095, obsMaxY = 0;
// touch edge detect
bool lastTouched = false;
uint32_t lastTouchMs = 0;

// Color base
static const uint16_t COLOR_RED       = 0xF800;
static const uint16_t COLOR_WHITE     = 0xFFFF;
static const uint16_t COLOR_BLACK     = 0x0000;
static const uint16_t COLOR_GRAY      = 0x2104;

// UI용 컬러들
static const uint16_t COLOR_BG        = 0x0000; // 전체 배경
static const uint16_t COLOR_PANEL     = 0x1082; // 패널 바탕(짙은 회색)
static const uint16_t COLOR_PANEL_IN  = 0x2104; // 카드 내부 배경(약간 밝은 회색)
static const uint16_t COLOR_ACCENT    = 0x07E0; // 강조(초록)
static const uint16_t COLOR_ACCENT2   = 0xF800; // 강조(빨강)
static const uint16_t COLOR_TEXT_DIM  = 0xC618; // 흐린 텍스트
static const uint16_t COLOR_YELLOW    = 0xFFE0; // 배터리 중간색

// Wifi bar height
static const int BAR_H = 24;

// UI box size
struct Rect { int16_t x, y, w, h; };
Rect touchBox;          // 전체 중앙 카드(터치 영역)
Rect servoResetBtnRect; // (지금은 UI만 사용)
Rect memberBox;         // 카드 왼쪽 절반 (Citizen Info 영역)
Rect nfcBox;            // 카드 오른쪽 절반 (Battery Data Status 영역)

// ----------------------------------------------------
// Firebase / Firestore
// ----------------------------------------------------
String g_idToken;
uint32_t g_tokenExpiryMs = 0;

// 기본 인덱스용 (디버그용)
const char* MEMBER_DOCS[] = {"members/m1", "members/m2", "members/m3"};
const int MEMBER_COUNT = 3;
int currentMemberIndex = 0;

// member data
struct MemberInfo {
  String docPath;     // "members/m1" 같은 경로
  String name;
  String country;

  int32_t birthDateUnix = 0;            // Unix time (sec)
  int32_t batteryDueUnix = 0;           // Unix time (sec)
  int32_t lastBatteryUnix = 0;          // Unix time (sec)

  String visaType;                      // empty => no visa
  bool canFinancial = false;

  int tendency = 0;                     // 0~10
  bool loaded = false;
};
MemberInfo currentMember;

// Replacement 패널에 표시할 상태 문자열
String g_lastNFCStatus = "Replacement init...";
String g_lastNFCText   = "";
String g_lastNFCUid    = "";

// --- 2단계 Replacement UX 상태 ---
String  g_firstTagText = "";       // 첫 번째 태그에서 읽은 문자열(이름)
bool    g_hasFirstTag = false;     // 첫 번째 태그 성공 여부
bool    g_secondWriteDone = false; // 두 번째 태그 쓰기 완료 여부
uint32_t g_secondTagRemovedAt = 0; // 두 번째 태그를 뗀 시각 (ms), 0이면 아직 아님

// ★ 두 번째 태그 유효 조건: REPLACEMENT_START(화면 터치) 이후인지
bool    g_readyForSecondTag = false;

// ★ DB 업데이트 상태 표시용
bool    g_dbUpdateAttempted = false;
bool    g_dbUpdateSuccess   = false;
int32_t g_dbLastRepNew      = 0;

// Serial Read setup (현재 안 씀)
bool readLine(String& out) {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      out = buf;
      buf = "";
      return true;
    }
    if (buf.length() < 200) buf += c;
    else buf = "";
  }
  return false;
}

//------------------------
//  utility 
int16_t clamp16(int16_t v, int16_t lo, int16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool pointInRect(int16_t x, int16_t y, const Rect &r) {
  return (x >= r.x) && (x < (r.x + r.w)) && (y >= r.y) && (y < (r.y + r.h));
}

// Unix time -> "MM/DD/YYYY" (원래 값)
String unixToMDY(int32_t ts) {
  if (ts <= 0) return "-";
  time_t t = (time_t)ts;
  struct tm *tmTime = localtime(&t);
  if (!tmTime) return "-";

  char buf[16];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d",
           tmTime->tm_mon + 1,
           tmTime->tm_mday,
           tmTime->tm_year + 1900);
  return String(buf);
}

// ★ 화면 표시용: Unix time + 100년 → "MM/DD/YYYY"
String unixToMDYPlus100(int32_t ts) {
  if (ts <= 0) return "-";
  time_t t = (time_t)ts;
  struct tm *tmTime = localtime(&t);
  if (!tmTime) return "-";

  int y = tmTime->tm_year + 1900 + 100;
  int m = tmTime->tm_mon + 1;
  int d = tmTime->tm_mday;

  char buf[16];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d", m, d, y);
  return String(buf);
}

void sendToUnoCmd(const char* cmd) {
  Serial.print("@CMD:");
  Serial.println(cmd);
}

// ----------------------------------------------------
// WiFi / Time / Firestore
// ----------------------------------------------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  static bool began = false;
  if (!began) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    began = true;
  }
}

String wifiLine() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    return "WiFi:OK";
  }
  if (st == WL_IDLE_STATUS) return "WiFi:IDLE";
  if (st == WL_NO_SSID_AVAIL) return "WiFi:NO_SSID";
  if (st == WL_CONNECT_FAILED) return "WiFi:FAIL";
  if (st == WL_DISCONNECTED) return "WiFi:DISC";
  return "WiFi:UNK";
}

void drawWifiStatusBar(bool force = false) {
  static String last = "";
  static uint32_t lastMs = 0;

  if (!force && (millis() - lastMs) < 500) return;
  lastMs = millis();

  String s = wifiLine();
  if (!force && s == last) return;
  last = s;

  int16_t W = gfx->width();

  // 바 배경
  gfx->fillRect(0, 0, W, BAR_H, COLOR_PANEL);
  // 아래쪽 얇은 라인
  gfx->drawLine(0, BAR_H - 1, W, BAR_H - 1, COLOR_GRAY);

  gfx->setFont(u8g2_font_6x10_tr);
  gfx->setTextSize(1);

  // 왼쪽: WiFi 라벨 + 상태
  gfx->setTextColor(COLOR_ACCENT);
  gfx->setCursor(6, 16);
  gfx->print("WiFi");

  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(40, 16);
  gfx->print(s);

  // 오른쪽: 장치 라벨
  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setCursor(W - 80, 16);
  gfx->print("BMD-CONSOLE");

  gfx->setFont();
}

// 시간 동기화 (NTP) – 한 번만 수행
bool ensureTimeSynced() {
  static bool done = false;
  if (done) return true;

  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  Serial.println("[TIME] syncing...");

  for (int i = 0; i < 30; i++) { // 최대 ~6초
    time_t now = time(nullptr);
    if (now > 1609459200) { // 2021-01-01 이후면 OK
      Serial.print("[TIME] synced: ");
      Serial.println((long)now);
      done = true;
      return true;
    }
    delay(200);
  }
  Serial.println("[TIME] sync failed");
  return false;
}

// TLS
static inline void makeInsecureTLS(WiFiClientSecure &client) {
  client.setInsecure();
}

// Firebase Auth 
bool firebaseSignIn() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  makeInsecureTLS(client);

  HTTPClient https;
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY;

  if (!https.begin(client, url)) {
    Serial.println("[AUTH] https.begin failed");
    return false;
  }
  https.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> req;
  req["email"] = FIREBASE_EMAIL;
  req["password"] = FIREBASE_PASSWORD;
  req["returnSecureToken"] = true;

  String body;
  serializeJson(req, body);

  int code = https.POST(body);
  String resp = https.getString();
  https.end();

  Serial.printf("[AUTH] HTTP %d\n", code);
  if (code != 200) {
    Serial.println(resp);
    return false;
  }

  StaticJsonDocument<4096> doc;
  auto err = deserializeJson(doc, resp);
  if (err) {
    Serial.print("[AUTH] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  g_idToken = doc["idToken"].as<String>();
  int expiresInSec = doc["expiresIn"].as<int>();
  g_tokenExpiryMs = millis() + (uint32_t)(max(60, expiresInSec - 60)) * 1000UL;

  Serial.println("[AUTH] idToken OK");
  return true;
}

// Firestore raw GET (단일 문서)
bool firestoreGetDocumentRaw(const String &docPath, int &httpCode, String &respOut) {
  respOut = "";
  httpCode = -1;

  if (WiFi.status() != WL_CONNECTED) return false;

  if (g_idToken.length() == 0 || millis() > g_tokenExpiryMs) {
    if (!firebaseSignIn()) return false;
  }

  WiFiClientSecure client;
  makeInsecureTLS(client);

  HTTPClient https;
  String url = String("https://firestore.googleapis.com/v1/projects/")
             + FIREBASE_PROJECT_ID
             + "/databases/(default)/documents/"
             + docPath;

  if (!https.begin(client, url)) {
    Serial.println("[FS] https.begin failed");
    return false;
  }

  https.addHeader("Authorization", "Bearer " + g_idToken);

  httpCode = https.GET();
  respOut = https.getString();
  https.end();

  Serial.printf("[FS] GET %s -> HTTP %d\n", docPath.c_str(), httpCode);
  if (httpCode != 200) Serial.println(respOut);

  return (httpCode == 200);
}

// Firestore response -> MemberInfo parse 
bool parseMemberFromFirestore(const String &resp, MemberInfo &out) {
  DynamicJsonDocument doc(12288);
  auto err = deserializeJson(doc, resp);
  if (err) {
    Serial.print("[PARSE] JSON error: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonObject fields = doc["fields"];
  if (fields.isNull()) return false;

  out.name    = fields["name"]["stringValue"] | "";
  out.country = fields["country"]["stringValue"] | "";

  // birthDate integerValue
  out.birthDateUnix = 0;
  if (!fields["birthDate"].isNull() && !fields["birthDate"]["integerValue"].isNull()) {
    String v = fields["birthDate"]["integerValue"].as<String>();
    out.birthDateUnix = (int32_t)v.toInt();
  }

  // batteryDueDate integerValue
  out.batteryDueUnix = 0;
  if (!fields["batteryDueDate"].isNull() && !fields["batteryDueDate"]["integerValue"].isNull()) {
    String v = fields["batteryDueDate"]["integerValue"].as<String>();
    out.batteryDueUnix = (int32_t)v.toInt();
  }

  // lastBatteryReplacementDate integerValue
  out.lastBatteryUnix = 0;
  if (!fields["lastBatteryReplacementDate"].isNull() && !fields["lastBatteryReplacementDate"]["integerValue"].isNull()) {
    String v = fields["lastBatteryReplacementDate"]["integerValue"].as<String>();
    out.lastBatteryUnix = (int32_t)v.toInt();
  }

  // visaType string
  out.visaType = fields["visaType"]["stringValue"] | "";

  // canFinancial
  out.canFinancial = fields["canFinancialTransactions"]["booleanValue"] | false;

  // tendency: integer 0~10
  out.tendency = 0;
  if (!fields["tendency"].isNull() && !fields["tendency"]["integerValue"].isNull()) {
    String v = fields["tendency"]["integerValue"].as<String>();
    out.tendency = clampInt(v.toInt(), 0, 10);
  }

  out.loaded = true;
  return true;
}

// name 필드 기반 Firestore 쿼리 (runQuery 사용)
bool firestoreGetMemberByName(const String &name, MemberInfo &out) {
  if (WiFi.status() != WL_CONNECTED) return false;

  if (g_idToken.length() == 0 || millis() > g_tokenExpiryMs) {
    if (!firebaseSignIn()) return false;
  }

  WiFiClientSecure client;
  makeInsecureTLS(client);

  HTTPClient https;
  String url = String("https://firestore.googleapis.com/v1/projects/")
             + FIREBASE_PROJECT_ID
             + "/databases/(default)/documents:runQuery";

  if (!https.begin(client, url)) {
    Serial.println("[FS-QUERY] https.begin failed");
    return false;
  }

  https.addHeader("Authorization", "Bearer " + g_idToken);
  https.addHeader("Content-Type", "application/json");

  // structuredQuery body: SELECT * FROM members WHERE name == <name> LIMIT 1
  StaticJsonDocument<512> root;
  JsonObject sq = root.createNestedObject("structuredQuery");

  JsonArray fromArr = sq.createNestedArray("from");
  JsonObject fromObj = fromArr.createNestedObject();
  fromObj["collectionId"] = "members";

  JsonObject where = sq.createNestedObject("where");
  JsonObject fieldFilter = where.createNestedObject("fieldFilter");
  fieldFilter["field"]["fieldPath"] = "name";
  fieldFilter["op"] = "EQUAL";
  fieldFilter["value"]["stringValue"] = name;

  sq["limit"] = 1;

  String body;
  serializeJson(root, body);

  int code = https.POST(body);
  String resp = https.getString();
  https.end();

  Serial.printf("[FS-QUERY] runQuery(name=%s) -> HTTP %d\n", name.c_str(), code);
  if (code != 200) {
    Serial.println(resp);
    return false;
  }

  DynamicJsonDocument doc(16384);
  auto err = deserializeJson(doc, resp);
  if (err) {
    Serial.print("[FS-QUERY] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    Serial.println("[FS-QUERY] empty result");
    return false;
  }

  JsonObject docObj;
  for (JsonObject v : arr) {
    JsonObject d = v["document"];
    if (!d.isNull()) {
      docObj = d;
      break;
    }
  }

  if (docObj.isNull()) {
    Serial.println("[FS-QUERY] no document field in result");
    return false;
  }

  // docObj는 단일 문서 JSON이므로, serialize해서 기존 파서 재사용
  String docStr;
  serializeJson(docObj, docStr);

  if (!parseMemberFromFirestore(docStr, out)) {
    Serial.println("[FS-QUERY] parseMemberFromFirestore failed");
    return false;
  }

  // docPath 채우기
  String fullName = docObj["name"] | "";
  if (fullName.length()) {
    int pos = fullName.indexOf("/documents/");
    if (pos >= 0) {
      out.docPath = fullName.substring(pos + String("/documents/").length());
    } else {
      out.docPath = fullName;
    }
  }

  return true;
}

// ★ lastBatteryReplacementDate만 PATCH ★
bool firestorePatchLastReplacementDate(const String &docPath, int32_t lastRep) {
  if (WiFi.status() != WL_CONNECTED) return false;

  if (g_idToken.length() == 0 || millis() > g_tokenExpiryMs) {
    if (!firebaseSignIn()) return false;
  }

  WiFiClientSecure client;
  makeInsecureTLS(client);

  HTTPClient https;
  String url = String("https://firestore.googleapis.com/v1/projects/")
             + FIREBASE_PROJECT_ID
             + "/databases/(default)/documents/"
             + docPath
             + "?updateMask.fieldPaths=lastBatteryReplacementDate";

  if (!https.begin(client, url)) {
    Serial.println("[FS-PATCH] https.begin failed");
    return false;
  }

  https.addHeader("Authorization", "Bearer " + g_idToken);
  https.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> root;
  JsonObject fields = root.createNestedObject("fields");
  fields["lastBatteryReplacementDate"]["integerValue"] = String(lastRep);

  String body;
  serializeJson(root, body);

  int code = https.sendRequest("PATCH", (uint8_t*)body.c_str(), body.length());
  String resp = https.getString();
  https.end();

  Serial.printf("[FS-PATCH] lastBatteryReplacementDate -> HTTP %d\n", code);
  if (code != 200) {
    Serial.println(resp);
    return false;
  }
  return true;
}

// ----------------------------------------------------
// 배터리 잔량 계산 (lastRep ~ due 사이에서 현재 시간을 100 → 0으로 맵핑)
// ----------------------------------------------------
int computeBatteryPercent(const MemberInfo &m) {
  if (m.lastBatteryUnix <= 0 || m.batteryDueUnix <= 0) return -1;
  if (m.batteryDueUnix <= m.lastBatteryUnix) return -1;

  if (!ensureTimeSynced()) return -1;

  time_t nowT = time(nullptr);
  if (nowT <= 0) return -1;

  int32_t last = m.lastBatteryUnix;
  int32_t due  = m.batteryDueUnix;

  if (nowT <= last) return 100;
  if (nowT >= due)  return 0;

  float ratio = (nowT - last) / float(due - last); // 0~1
  int pct = int(100.0f - ratio * 100.0f + 0.5f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// ----------------------------------------------------
// Display layout / rendering
// ----------------------------------------------------
void computeLayout() {
  int16_t W = gfx->width();
  int16_t H = gfx->height();

  int16_t top = BAR_H + 12;     // WiFi 바와 카드 사이 간격
  int16_t sideMargin = W / 14;  // 좌우 여백
  int16_t bottomMargin = 16;

  int16_t cardW = W - sideMargin * 2;
  int16_t cardH = H - top - bottomMargin;

  // 전체 카드(터치 영역)
  touchBox.x = sideMargin;
  touchBox.y = top;
  touchBox.w = cardW;
  touchBox.h = cardH;

  // 좌/우 절반으로 분할
  int16_t halfW = cardW / 2;
  memberBox.x = touchBox.x + 8;
  memberBox.y = touchBox.y + 8;
  memberBox.w = halfW - 12;
  memberBox.h = cardH - 16;

  nfcBox.x = touchBox.x + halfW + 4;
  nfcBox.y = touchBox.y + 8;
  nfcBox.w = cardW - halfW - 12;
  nfcBox.h = cardH - 16;

  // 우측 상단 Reset 버튼 (지금은 UI만)
  const int16_t btnMarginX = 8;
  const int16_t btnMarginY = 3;
  const int16_t btnW = 70;
  const int16_t btnH = BAR_H - btnMarginY * 2;

  servoResetBtnRect.w = btnW;
  servoResetBtnRect.h = btnH;
  servoResetBtnRect.x = W - btnMarginX - btnW; 
  servoResetBtnRect.y = btnMarginY; 
}

// --- 글자 크기: 헤더는 2, 본문은 1 (중간 정도 크기) ---
void drawMemberInfo(const MemberInfo &m) {
  // 패널 배경
  gfx->fillRect(memberBox.x, memberBox.y, memberBox.w, memberBox.h, COLOR_PANEL);
  gfx->drawRect(memberBox.x, memberBox.y, memberBox.w, memberBox.h, COLOR_WHITE);

  // 헤더 바
  int16_t headerH = 32;
  gfx->fillRect(memberBox.x, memberBox.y, memberBox.w, headerH, COLOR_ACCENT);
  gfx->drawLine(memberBox.x, memberBox.y + headerH, 
                memberBox.x + memberBox.w, memberBox.y + headerH, COLOR_BLACK);

  gfx->setFont(u8g2_font_6x10_tr);

  // 헤더 텍스트: 크게(2배)
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_BLACK);
  gfx->setCursor(memberBox.x + 6, memberBox.y + 22);
  gfx->print("CITIZEN INFO");

  // 본문 시작 위치
  const int padX = 8;
  const int padY = headerH + 18;
  int16_t x = memberBox.x + padX;
  int16_t y = memberBox.y + padY;

  // 본문은 1배 크기
  gfx->setTextSize(1);

  if (!m.loaded) {
    // 데이터가 없을 때 안내 문구
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->setCursor(x, y);
    gfx->print("Tap Replacement tag");

    gfx->setCursor(x, y + 16);
    gfx->print("to start ID check");

    gfx->setCursor(x, y + 32);
    gfx->print("(Step 1: ID)");

    gfx->setFont();
    return;
  }

  int lineH = 16;

  auto drawLabelValue = [&](const char* label, const String &value, int line) {
    int16_t ly = y + line * lineH;
    gfx->setCursor(x, ly);
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->print(label);

    gfx->setCursor(x + 80, ly);
    gfx->setTextColor(COLOR_WHITE);
    gfx->print(value);
  };

  // ★ 화면에는 +100년 더한 날짜로 표시 ★
  drawLabelValue("Name",     m.name,                             0);
  drawLabelValue("Country",  m.country,                          1);
  drawLabelValue("Birth",    unixToMDYPlus100(m.birthDateUnix),  2);
  drawLabelValue("Due",      unixToMDYPlus100(m.batteryDueUnix), 3);
  drawLabelValue("Last rep", unixToMDYPlus100(m.lastBatteryUnix),4);

  String visaStr = (m.visaType.length() > 0) ? m.visaType : String("NONE");
  drawLabelValue("Visa",     visaStr,                            5);

  drawLabelValue("Finance",  m.canFinancial ? "YES" : "NO",      6);

  String tend = String(m.tendency) + "/10";
  drawLabelValue("Tendency", tend,                               7);

  // ★ 배터리 잔량 바 추가 ★
  int pct = computeBatteryPercent(m);
  if (pct >= 0) {
    int16_t baseY = y + 8 * lineH + 4;
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->setCursor(x, baseY);
    gfx->print("Battery");

    int barW = memberBox.w - padX * 2;
    int barH = 10;
    int barX = x;
    int barY = baseY + 6;

    // 바 테두리
    gfx->drawRect(barX, barY, barW, barH, COLOR_WHITE);
    // 채워진 부분
    int fillW = barW * pct / 100;

    uint16_t barColor = COLOR_ACCENT; // 기본 초록
    if (pct <= 30) barColor = COLOR_RED;
    else if (pct <= 60) barColor = COLOR_YELLOW;

    gfx->fillRect(barX + 1, barY + 1, fillW - 2 > 0 ? fillW - 2 : 0, barH - 2, barColor);

    // 오른쪽에 % 텍스트
    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(barX + barW - 36, baseY);
    gfx->print(pct);
    gfx->print("%");
  }

  gfx->setFont();
}

void drawNFCInfo() {
  // 패널 배경
  gfx->fillRect(nfcBox.x, nfcBox.y, nfcBox.w, nfcBox.h, COLOR_PANEL);
  gfx->drawRect(nfcBox.x, nfcBox.y, nfcBox.w, nfcBox.h, COLOR_WHITE);

  // 헤더 바 (한 줄 "BATTERY DATA")
  int16_t headerH = 32;
  gfx->fillRect(nfcBox.x, nfcBox.y, nfcBox.w, headerH, COLOR_ACCENT2);
  gfx->drawLine(nfcBox.x, nfcBox.y + headerH, 
                nfcBox.x + nfcBox.w, nfcBox.y + headerH, COLOR_BLACK);

  gfx->setFont(u8g2_font_6x10_tr);

  // 헤더 텍스트: 2배, 한 줄
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(nfcBox.x + 6, nfcBox.y + 22);
  gfx->print("BATTERY DATA");

  // 본문 시작 위치
  const int padX = 8;
  const int padY = headerH + 18;
  int16_t x = nfcBox.x + padX;
  int16_t y = nfcBox.y + padY;

  // 본문은 1배 크기
  gfx->setTextSize(1);
  int lineH = 16;

  // 단계 안내
  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setCursor(x, y);
  gfx->print("Step 1: Identification");

  gfx->setCursor(x, y + lineH);
  gfx->print("Step 2: Replacement Confirm");

  // 현재 상태
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(x, y + lineH * 3);
  gfx->print("Status:");

  gfx->setCursor(x, y + lineH * 4);
  gfx->print(g_lastNFCStatus);

  // ★ 현재 사이클의 대상 클라이언트 이름 표시 ★
  if (g_hasFirstTag && g_firstTagText.length() > 0) {
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->setCursor(x, y + lineH * 6);
    gfx->print("Client:");

    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(x + 60, y + lineH * 6);
    gfx->print(g_firstTagText);
  }

  // ★ DB 업데이트 결과 정보 표시 ★
  if (g_dbUpdateAttempted) {
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->setCursor(x, y + lineH * 7);
    gfx->print("Last rep(new):");

    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(x + 110, y + lineH * 7);
    if (g_dbLastRepNew > 0) {
      gfx->print(unixToMDY(g_dbLastRepNew)); // 실제 DB에 저장된 날짜(원래 값)
    } else {
      gfx->print("-");
    }
  }

  gfx->setFont();
}

void renderMemberScreen() {
  computeLayout();

  // 전체 배경
  gfx->fillScreen(COLOR_BG);

  // 카드 그림자
  gfx->fillRect(touchBox.x + 4, touchBox.y + 4, touchBox.w, touchBox.h, COLOR_BLACK);
  // 카드 본체
  gfx->fillRect(touchBox.x, touchBox.y, touchBox.w, touchBox.h, COLOR_PANEL_IN);
  gfx->drawRect(touchBox.x, touchBox.y, touchBox.w, touchBox.h, COLOR_WHITE);

  // 좌측: Citizen Info
  drawMemberInfo(currentMember);

  // 우측: Battery Data Status
  drawNFCInfo();

  // 상단 WiFi 바
  drawWifiStatusBar(true);
}

// touch mapping
bool mapTouchToScreen(const TS_Point &p, int16_t &sx, int16_t &sy) {
  int16_t rx = p.x;
  int16_t ry = p.y;

  obsMinX = min(obsMinX, rx); obsMaxX = max(obsMaxX, rx);
  obsMinY = min(obsMinY, ry); obsMaxY = max(obsMaxY, ry);

  if (TOUCH_SWAP_XY) { int16_t t = rx; rx = ry; ry = t; }
  if (TOUCH_INVERT_X) rx = (TS_MAXX + TS_MINX) - rx;
  if (TOUCH_INVERT_Y) ry = (TS_MAXY + TS_MINY) - ry;

  rx = clamp16(rx, TS_MINX, TS_MAXX);
  ry = clamp16(ry, TS_MINY, TS_MAXY);

  int16_t W = gfx->width();
  int16_t H = gfx->height();
  sx = map(rx, TS_MINX, TS_MAXX, 0, W - 1);
  sy = map(ry, TS_MINY, TS_MAXY, 0, H - 1);
  return true;
}

void printTouchDebug(const TS_Point &p, int16_t sx, int16_t sy, bool inBox) {
  Serial.printf("RAW(%d,%d) -> SCREEN(%d,%d) inBox=%d | OBS X[%d..%d] Y[%d..%d]\n",
                p.x, p.y, sx, sy, inBox ? 1 : 0,
                obsMinX, obsMaxX, obsMinY, obsMaxY);
}

// member load and transition (index 기반 – 디버그용)
bool loadMemberByIndex(int idx) {
  currentMember = MemberInfo{};
  currentMember.docPath = MEMBER_DOCS[idx];

  int code;
  String resp;
  bool ok = firestoreGetDocumentRaw(currentMember.docPath, code, resp);
  if (!ok) {
    currentMember.loaded = false;
    return false;
  }
  return parseMemberFromFirestore(resp, currentMember);
}

// name 기반 멤버 로딩 (Firestore runQuery 이용)
bool loadMemberByName(const String &name) {
  MemberInfo m;
  bool ok = firestoreGetMemberByName(name, m);
  if (!ok) return false;

  currentMember = m;
  return true;
}

void nextMember() {
  currentMemberIndex = (currentMemberIndex + 1) % MEMBER_COUNT;
  loadMemberByIndex(currentMemberIndex);
  renderMemberScreen();
}

// ----------------------------------------------------
// PN532 + NTAG 코드 (MK 헤더 + 포맷 후 쓰기 + Ten 제거 하이브리드 읽기)
// ----------------------------------------------------

// PN532 SPI wiring (VSPI)
#define PN532_SCK   18
#define PN532_MISO  19
#define PN532_MOSI  23
#define PN532_SS    21   // CS

// Switch gate (INPUT_PULLUP)
#define NFC_ENABLE_PIN 35  // switch: one side -> GPIO35, other side -> GND

// ===== User region (NTAG2xx) =====
// page 4: header (4 bytes)
// page 5~9: payload (5 pages * 4 = 20 bytes)
static const uint8_t START_PAGE = 4;
static const uint8_t END_PAGE   = 9;

static const uint8_t SIG0 = 'M';
static const uint8_t SIG1 = 'K';

String g_writeText = String("Minkyu Kim");

// ---- tag hold / re-arm ----
bool tagHeld = false;
uint8_t lastUid[7] = {0};
uint8_t lastUidLen = 0;

uint32_t lastSeenMs = 0;
const uint32_t TAG_RELEASE_MS = 600; // if tag not seen for this long => consider removed

// PN532
Adafruit_PN532 nfc(PN532_SS);

// ---- utils ----
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

// NOTE: Adafruit_PN532::ntag2xx_WritePage takes (uint8_t*), not (const uint8_t*)
// so we copy to a non-const buffer.
bool writePageRetry(uint8_t page, const uint8_t in4[4], int tries = 8) {
  uint8_t buf[4];
  memcpy(buf, in4, 4);

  for (int i = 0; i < tries; i++) {
    if (nfc.ntag2xx_WritePage(page, buf)) return true;
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
          delay(80); // RF 안정화
          return true;
        }
      }
    }
    delay(30);
  }
  return false;
}

// ---- 기존 데이터 삭제(우리 구역만 포맷) ----
bool clearUserRegion() {
  Serial.println("[FORMAT] clear user region pages 4..9");
  uint8_t zero4[4] = {0,0,0,0};
  for (uint8_t p = START_PAGE; p <= END_PAGE; p++) {
    if (!writePageRetry(p, zero4)) {
      Serial.print("[FORMAT] failed at page ");
      Serial.println(p);
      return false;
    }
    delay(8);
  }
  return true;
}

// ---- String 쓰기 (쓰기 전 항상 포맷, MK 헤더 사용) ----
bool writeTextToTag(const String &text) {
  size_t len = text.length();
  if (len > 255) len = 255;

  uint16_t payloadCap = (uint16_t)((END_PAGE - START_PAGE) * 4); // header 제외 payload
  if (len > payloadCap) {
    Serial.print("[WRITE] Too long. cap=");
    Serial.println(payloadCap);
    return false;
  }

  // ★ 포맷 후 쓰기 ★
  if (!clearUserRegion()) {
    Serial.println("[WRITE] clearUserRegion failed");
    return false;
  }

  // header: 'M', 'K', length, 0x00
  uint8_t header[4] = { SIG0, SIG1, (uint8_t)len, 0x00 };
  if (!writePageRetry(START_PAGE, header, 12)) {
    Serial.println("[WRITE] header write failed");
    return false;
  }
  delay(10);

  // payload (page 5~9)
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

// ---- String 읽기 (MK 헤더 기반, strict) ----
bool readTextFromTagStrict(String &outText) {
  outText = "";

  uint8_t header[4];
  if (!readPageRetry(START_PAGE, header, 12)) return false;

  // 시그니처 확인
  if (header[0] != SIG0 || header[1] != SIG1) {
    Serial.print("[READ STRICT] invalid header: ");
    Serial.print(header[0], HEX); Serial.print(" ");
    Serial.println(header[1], HEX);
    return false;
  }

  uint8_t len = header[2];
  uint16_t payloadCap = (uint16_t)((END_PAGE - START_PAGE) * 4);
  if (len > payloadCap) {
    Serial.println("[READ STRICT] length too large");
    return false;
  }

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

// ---- MK 헤더 없이 "대충" 읽기 (Ten 프리픽스 제거용) ----
bool readTextFromTagLoose(String &outText) {
  outText = "";
  // page 4~9 전체를 읽고, ASCII 범위(0x20~0x7E)만 추출
  for (uint8_t p = START_PAGE; p <= END_PAGE; p++) {
    uint8_t buf[4];
    if (!readPageRetry(p, buf)) {
      Serial.print("[READ LOOSE] readPage failed at ");
      Serial.println(p);
      return false;
    }
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t c = buf[i];
      if (c == 0x00) continue;
      if (c >= 0x20 && c <= 0x7E) {
        outText += (char)c;
      }
    }
  }

  outText.trim();

  if (outText.length() == 0) {
    Serial.println("[READ LOOSE] empty after filter");
    return false;
  }

  // 앞에 'Ten' 붙어 있을 경우 제거 (TenAugie Fesh → Augie Fesh)
  if (outText.startsWith("Ten")) {
    outText.remove(0, 3); // "Ten" 제거
    outText.trim();
  }

  Serial.print("[READ LOOSE] text = ");
  Serial.println(outText);

  return (outText.length() > 0);
}

// ---- 하이브리드 읽기: 1) MK strict → 2) loose(+Ten 제거) ----
bool readTextFromTagHybridOnce(String &outText) {
  // 1. MK 헤더 strict 시도
  if (readTextFromTagStrict(outText)) {
    Serial.print("[READ HYBRID] strict(MK) OK: ");
    Serial.println(outText);
    return (outText.length() > 0);
  }

  // 2. 실패하면 loose 모드 (Ten 제거 포함)
  if (readTextFromTagLoose(outText)) {
    Serial.print("[READ HYBRID] loose OK: ");
    Serial.println(outText);
    return true;
  }

  return false;
}

bool readTextFromTagHybridRobust(String &outText) {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (readTextFromTagHybridOnce(outText)) {
      return true;
    }
    delay(60);
  }
  Serial.println("[READ HYBRID] failed after retries");
  return false;
}

// ---- 스위치 게이트 ----
bool nfcEnabled() {
  // INPUT_PULLUP: 스위치 ON(=GND 연결) -> LOW
  return (digitalRead(NFC_ENABLE_PIN) == LOW);
}

// ★ 첫 번째 태그 처리 공용 함수
void handleFirstNfcTag(const String &text) {
  g_firstTagText      = text;              // 이름 저장
  g_writeText         = g_firstTagText;    // 두 번째 태그에 쓸 내용
  g_hasFirstTag       = true;
  g_secondWriteDone   = false;
  g_secondTagRemovedAt = 0;

  // 두 번째 태그는 아직 허용되지 않음 (화면 터치 후에만 허용)
  g_readyForSecondTag = false;

  // DB 업데이트 상태 초기화
  g_dbUpdateAttempted = false;
  g_dbUpdateSuccess   = false;
  g_dbLastRepNew      = 0;

  // 이름으로 Firestore 멤버 로딩
  bool ok = loadMemberByName(g_firstTagText);

  if (ok) {
    g_lastNFCStatus = "First tag OK";
  } else {
    g_lastNFCStatus = "Name not found";
    // 매칭 실패 시에도 currentMember는 빈 상태로 두고,
    // 이후에는 DB 업데이트 불가 → 두 번째 태그 때도 No member to update 가능
  }

  g_lastNFCText = g_firstTagText;

  Serial.print("[Replacement] First tag name = ");
  Serial.println(g_firstTagText);

  renderMemberScreen();
}

// 2단계 Replacement UX 포함 PN532 처리
void handleNFC() {
  // 스위치로 Replacement 리더 끈 상태면, 감지 상태만 초기화
  if (!nfcEnabled()) {
    tagHeld = false;
    lastUidLen = 0;
    lastSeenMs = 0;
    delay(50);
    return;
  }

  // 1) 현재 태그 존재 여부 먼저 확인
  uint8_t probeUid[7]; 
  uint8_t probeLen = 0;
  bool present = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, probeUid, &probeLen, 40);

  if (!present) {
    // 태그가 더 이상 안 보이는데, 이전에 붙어있던 상태였다면 → "떼어졌음"
    if (tagHeld && lastSeenMs != 0 && (millis() - lastSeenMs) > TAG_RELEASE_MS) {
      tagHeld = false;
      lastUidLen = 0;

      // ★ 여기서는 currentMember를 비우지 않는다 ★
      // 이유: 첫 번째 카드 떼고 두 번째 카드 준비하는 동안에도
      //       왼쪽 Citizen Info는 살아 있어야 하고,
      //       두 번째 태그 시 DB 업데이트에 사용되어야 함.

      if (g_secondWriteDone && g_secondTagRemovedAt == 0) {
        // 두 번째 태그까지 작업 끝난 뒤에 떨어진 첫 순간
        g_secondTagRemovedAt = millis();
        g_lastNFCStatus = "Second tag removed";
      } else {
        g_lastNFCStatus = "Tag removed";
      }
      g_lastNFCText = "";
      g_lastNFCUid  = "";
      renderMemberScreen();
    }
    delay(20);
    return;
  } else {
    // 태그가 보이는 동안에는 lastSeenMs 갱신
    lastSeenMs = millis();
  }

  // 2) 안정 태그(연속 동일 UID 확인)
  uint8_t uid[7]; 
  uint8_t uidLen = 0;
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

  // UID 문자열 (화면에는 안 쓰지만 유지)
  String uidStr;
  uidStr.reserve(uidLen * 3);
  for (uint8_t i = 0; i < uidLen; i++) {
    if (i > 0) uidStr += " ";
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
  }
  uidStr.toUpperCase();
  g_lastNFCUid = uidStr;

  Serial.print("\n[Replacement] UID: ");
  printHex(uid, uidLen);
  Serial.println();

  // 3) 태그 안의 텍스트 읽기 (MK 우선 + Ten 제거 fallback)
  String text;
  if (!readTextFromTagHybridRobust(text)) {
    Serial.println("[Replacement READ] failed");
    g_lastNFCStatus = "Read failed";
    g_lastNFCText   = "";
    renderMemberScreen();
    delay(100);
    return;
  }

  Serial.print("[Replacement READ] text = ");
  Serial.println(text);

  // 4) 단계에 따라 분기
  if (!g_hasFirstTag) {
    // ==============================
    // 1단계: 첫 번째 태그
    // ==============================
    handleFirstNfcTag(text);
  }
  else if (!g_secondWriteDone) {
    // ==============================
    // 2단계: 아직 두 번째 쓰기 전 상태
    // ==============================
    if (!g_readyForSecondTag) {
      // 화면 터치(REPLACEMENT_START) 전에 다시 태그됨:
      // → 새로운 첫 태그처럼 동작 (ID 교체) / 카운트 리셋 느낌
      Serial.println("[Replacement] New first tag (screen not touched yet)");
      handleFirstNfcTag(text);
      return;
    }

    // 여기까지 왔다는 것은:
    // - g_hasFirstTag == true
    // - g_readyForSecondTag == true (화면 터치 완료)
    // → 진짜 "2번째 태그" 로 인정
    Serial.print("[Replacement SECOND] write name = ");
    Serial.println(g_firstTagText);

    if (writeTextToTag(g_firstTagText)) {
      String after;
      if (readTextFromTagHybridRobust(after)) {
        Serial.print("[Replacement VERIFY] text = ");
        Serial.println(after);

        g_lastNFCText     = after;
        g_secondWriteDone = true;
        // 2번째 태그 처리 후에는 더 이상 second tag 대기 X
        g_readyForSecondTag = false;

        // DB 업데이트 상태 초기화
        g_dbUpdateAttempted = false;
        g_dbUpdateSuccess   = false;
        g_dbLastRepNew      = 0;

        // Firestore 업데이트 진행
        if (currentMember.loaded && currentMember.docPath.length() > 0) {
          // === 상태: DB 업데이트 시도 중 ===
          g_dbUpdateAttempted = true;
          g_lastNFCStatus     = "DB updating...";
          renderMemberScreen();

          if (ensureTimeSynced()) {
            time_t nowT = time(nullptr);
            if (nowT > 0) {
              int32_t lastRep = (int32_t)nowT;

              if (firestorePatchLastReplacementDate(currentMember.docPath, lastRep)) {
                Serial.println("[Replacement] Firestore lastReplacementDate updated");

                // 로컬 캐시도 갱신해서 즉시 화면에 반영
                currentMember.lastBatteryUnix = lastRep;

                g_dbUpdateSuccess = true;
                g_dbLastRepNew    = lastRep;

                // 실제 DB에 입력된 날짜(원래 값, +100 아님)
                String realDate = unixToMDY(lastRep);
                g_lastNFCStatus = "DB OK";
                Serial.print("[Replacement] DB OK date=");
                Serial.println(realDate);
              } else {
                Serial.println("[Replacement] Firestore update failed");
                g_dbUpdateSuccess = false;
                g_lastNFCStatus   = "DB update failed";
              }
            } else {
              Serial.println("[Replacement] invalid time()");
              g_dbUpdateSuccess = false;
              g_lastNFCStatus   = "DB update failed (time)";
            }
          } else {
            Serial.println("[Replacement] time sync failed, skip Firestore date update");
            g_dbUpdateSuccess = false;
            g_lastNFCStatus   = "Time sync failed";
          }
        } else {
          Serial.println("[Replacement] no loaded member to update");
          g_lastNFCStatus = "No member to update";
          // g_dbUpdateAttempted는 false로 남겨도 되고 true로 남겨도 되는데,
          // 여기서는 별도 DB 처리가 없으니 false로 둠
          g_dbUpdateAttempted = false;
        }

      } else {
        Serial.println("[Replacement VERIFY] read failed");
        g_lastNFCStatus = "Verify failed";
        g_lastNFCText   = "";
      }
    } else {
      Serial.println("[Replacement WRITE] failed");
      g_lastNFCStatus = "Write failed";
      g_lastNFCText   = "";
    }

    renderMemberScreen();
  }
  else {
    // 이미 1단계 + 2단계 모두 끝난 상태에서 또 태그가 오면
    // → 새 사이클을 시작하도록 전체 상태 리셋
    Serial.println("[Replacement] New cycle start");
    g_hasFirstTag        = false;
    g_secondWriteDone    = false;
    g_secondTagRemovedAt = 0;
    g_firstTagText       = "";
    g_readyForSecondTag  = false;

    g_dbUpdateAttempted  = false;
    g_dbUpdateSuccess    = false;
    g_dbLastRepNew       = 0;

    g_lastNFCStatus      = "New cycle";
    g_lastNFCText        = "";
    g_lastNFCUid         = "";

    // 새 사이클 시작: currentMember 비우기
    currentMember        = MemberInfo{};
    renderMemberScreen();
  }

  delay(150);
}

// ----------------------------------------------------
// setup / loop (최종 통합)
// ----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // TFT 백라이트
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // TFT + GFX
  gfx->begin(40000000);
  gfx->setRotation(1);

  // Touch (XPT2046) on HSPI
  hspi.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  ts.begin(hspi);

  // Replacement 리더용 GPIO (스위치)
  pinMode(NFC_ENABLE_PIN, INPUT_PULLUP);

  // VSPI (PN532 전용)
  SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

  Serial.println("PN532 SPI NTAG (ESP32-32E Display board) - MK write + hybrid read");
  Serial.print("Write text = ");
  Serial.println(g_writeText);
  Serial.println("1st tag = read name, 2nd tag = write MK name.");

  // PN532 초기화
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) {
    Serial.println("PN532 not found. Check wiring + PN532 SPI mode setting + power.");
    g_lastNFCStatus = "Replacement reader not found";
  } else {
    Serial.println("PN532 Found");
    g_lastNFCStatus = "Replacement ready";
  }
  nfc.SAMConfig();

  // 처음엔 멤버 정보 비움
  currentMember = MemberInfo{};
  renderMemberScreen();

  // WiFi / Firestore 준비
  connectWiFi();
  drawWifiStatusBar(true);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
    drawWifiStatusBar(true);
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    drawWifiStatusBar(true);

    if (firebaseSignIn()) {
      Serial.println("[AUTH] Firestore ready");
      renderMemberScreen();
    } else {
      Serial.println("[AUTH] sign-in failed");
    }
  } else {
    Serial.println("[WiFi] connect timeout (15s)");
    drawWifiStatusBar(true);
  }
}

void loop() {
  // 상단 WiFi 상태 갱신
  drawWifiStatusBar();

  // WiFi 재연결 시도
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 5000) {
      lastTry = millis();
      connectWiFi();
      drawWifiStatusBar(true);
    }
  }

  // 터치 처리: 중앙 카드 전체를 터치 영역으로 사용
  bool touched = ts.touched();
  if (touched && !lastTouched && (millis() - lastTouchMs) > debounceMs) {
    TS_Point p = ts.getPoint();
    int16_t sx, sy;
    mapTouchToScreen(p, sx, sy);

    bool inMainBox = pointInRect(sx, sy, touchBox);

    printTouchDebug(p, sx, sy, inMainBox);

    if (inMainBox) {
      // 화면 터치 → REPLACEMENT_START 전송
      sendToUnoCmd("REPLACEMENT_START");

      // 이제부터 찍히는 태그는 "2번째 태그"로 인정 가능
      g_readyForSecondTag = true;

      // UX용 상태 반영
      g_lastNFCStatus = "Screen touched: ready 2nd tag";
      renderMemberScreen();
    }

    lastTouchMs = millis();
  }
  lastTouched = touched;

  // PN532 / Replacement 처리
  handleNFC();

  // --- 두 번째 태그를 떼고 3초가 지나면 SERVO_RESET ---
  if (g_secondWriteDone && g_secondTagRemovedAt != 0) {
    if (millis() - g_secondTagRemovedAt >= 3000) {
      // 3초 경과 → 서보 리셋
      sendToUnoCmd("SERVO_RESET");

      // 다음 사이클을 위해 상태 초기화
      g_hasFirstTag        = false;
      g_secondWriteDone    = false;
      g_secondTagRemovedAt = 0;
      g_firstTagText       = "";
      g_readyForSecondTag  = false;

      g_dbUpdateAttempted  = false;
      g_dbUpdateSuccess    = false;
      g_dbLastRepNew       = 0;

      currentMember        = MemberInfo{};

      g_lastNFCStatus = "Replacement cycle done";
      g_lastNFCText   = "";
      g_lastNFCUid    = "";
      renderMemberScreen();
    }
  }

  delay(10);
}