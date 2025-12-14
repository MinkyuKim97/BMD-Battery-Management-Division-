//------------------------------------------------------------
// Board info: ESP32 LVGL 3.5 innch display
// Upload Set up
// - Tool -> Board -> 'ESP32 Dev Module'
// - Flash Size: 4mb
// - PSRAM: Disabled
// - Upload Speed: 115200 (921600 might break the upload)
// - Flash Mode: DIO
// - Flash Frequency: 40MHz
// - Partition Scheme: Default
//------------------------------------------------------------
// [secret.h]
// Make sure fill up the secret infos
// 1. WIFI SSID/PASSWORD list
// 2. Firestore database API key
//------------------------------------------------------------
// GFX font
#include <U8g2lib.h>
// Display libraries
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
// WIFI
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
// JSON read
#include <ArduinoJson.h>
// Time for Unix shape
#include <time.h>
// NFC Module
#include <Adafruit_PN532.h>

#include "secrets.h"

// LVGL Display pin setting
#define TFT_CS   15
#define TFT_DC   2
#define TFT_SCK  14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_BL   27
#define TFT_RST  GFX_NOT_DEFINED

// Touch - XPT2046
#define TP_CS    33
#define TP_IRQ   36

// For touch calibration
#define TS_MINX  200
#define TS_MAXX  3800
#define TS_MINY  200
#define TS_MAXY  3800

// Touch direction set-up purpose
#define TOUCH_SWAP_XY   true
#define TOUCH_INVERT_X  true
#define TOUCH_INVERT_Y  false

// Common Debounce time
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
// Touch calib helper
int16_t obsMinX = 4095, obsMaxX = 0, obsMinY = 4095, obsMaxY = 0;

bool lastTouched = false;
uint32_t lastTouchMs = 0;

// Color base set-up
static const uint16_t COLOR_RED       = 0xF800;
static const uint16_t COLOR_WHITE     = 0xFFFF;
static const uint16_t COLOR_BLACK     = 0x0000;
static const uint16_t COLOR_GRAY      = 0x2104;
static const uint16_t COLOR_BG        = 0x0000;
static const uint16_t COLOR_PANEL     = 0x1082;
static const uint16_t COLOR_PANEL_IN  = 0x2104;
static const uint16_t COLOR_ACCENT    = 0x07E0;
static const uint16_t COLOR_ACCENT2   = 0xF800;
static const uint16_t COLOR_TEXT_DIM  = 0xC618;
static const uint16_t COLOR_YELLOW    = 0xFFE0;

// WIFI status bar height
static const int BAR_H = 24;

// UI box size set up
struct Rect { int16_t x, y, w, h; };
// Center touch-able area(?)
Rect touchBox;
// Left, member(citizen) info box
Rect memberBox;
// Right, NFC(battery) read box
Rect nfcBox;

// ----------------------------------------------------
// ----------------------------------------------------

// Firebase / Firestore
String g_idToken;
uint32_t g_tokenExpiryMs = 0;

// member(citizen) data from the database
struct MemberInfo {
  String docPath;
  String name;
  String country;
  
  // Based on Unix date -> map the battery percentage
  int32_t birthDateUnix = 0;
  int32_t batteryDueUnix = 0;
  int32_t lastBatteryUnix = 0;

  String visaType;
  bool canFinancial = false;

  int tendency = 0;
  bool loaded = false;
};
MemberInfo currentMember;

// Strings for initial replacement stage
String g_lastNFCStatus = "Replacement init...";
String g_lastNFCText   = "";
String g_lastNFCUid    = "";

// String/bools for tag status
String  g_firstTagText = "";
bool    g_hasFirstTag = false;
bool    g_secondWriteDone = false;
uint32_t g_secondTagRemovedAt = 0;

// Second tag bool -> for sending SERVO_RESET serial after the second tag
bool    g_readyForSecondTag = false;

// Database update form
bool    g_dbUpdateAttempted = false;
bool    g_dbUpdateSuccess   = false;
int32_t g_dbLastRepNew      = 0;


//------------------------
//------------------------

//  Int clamping utility 
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

// Unix time convert -> MM/DD/YYYY
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

// Date 100 years offest -> Add 100 years from the date
// Use for currnet date, replaced date, etc...
String unixToMDYOffset100(int32_t ts) {
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

// Serial message 'CMD' add
// Since using a basic RX/TX pin from both units, add 'CMD' to filter the commend line
void sendToUnoCmd(const char* cmd) {
  Serial.print("@CMD:");
  Serial.println(cmd);
}

// ----------------------------------------------------
// ----------------------------------------------------

// WIFI Connection
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  static bool started = false;
  static unsigned long lastAttempt = 0;

  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (!started) {
    Serial.println("Scanning WiFi...");
    int n = WiFi.scanNetworks();

    if (n <= 0) {
      Serial.println("No WiFi networks found");
      return;
    }

    for (int i = 0; i < n; i++) {
      String found = WiFi.SSID(i);

      for (int j = 0; j < WIFI_COUNT; j++) {
        if (found == WIFI_SSIDS[j]) {
          Serial.print("Connecting to ");
          Serial.println(WIFI_SSIDS[j]);

          WiFi.begin(WIFI_SSIDS[j], WIFI_PASSWORDS[j]);
          started = true;
          return;
        }
      }
    }
    Serial.println("No known WiFi detected");
  }
}

// WIFI status string for displaying through GFX
String wifiLine() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    return "WiFi:OK";
  }
  if (st == WL_IDLE_STATUS) return "WiFi:IDLE";
  if (st == WL_NO_SSID_AVAIL) return "WiFi:NO_SSID";
  if (st == WL_CONNECT_FAILED) return "WiFi:FAIL";
  if (st == WL_DISCONNECTED) return "WiFi:DISCONNECTED";
  return "WiFi:UNKOWN";
}

// GFX, draw wifi status on display
void drawWifiStatusBar(bool force = false) {
  static String last = "";
  static uint32_t lastMs = 0;

  if (!force && (millis() - lastMs) < 500) return;
  lastMs = millis();

  String s = wifiLine();
  if (!force && s == last) return;
  last = s;

  int16_t W = gfx->width();

  gfx->fillRect(0, 0, W, BAR_H, COLOR_PANEL);
  gfx->drawLine(0, BAR_H - 1, W, BAR_H - 1, COLOR_GRAY);

  gfx->setFont(u8g2_font_6x10_tr);
  gfx->setTextSize(1);

  gfx->setTextColor(COLOR_ACCENT);
  gfx->setCursor(6, 16);
  gfx->print("WiFi");

  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(40, 16);
  gfx->print(s);

  // Right side, BMD text
  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setCursor(W - 80, 16);
  gfx->print("BMD Replacement Facility");

  gfx->setFont();
}

// Time sync process (Once)
bool ensureTimeSynced() {
  static bool done = false;
  if (done) return true;

  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  Serial.println("[TIME] syncing...");

  for (int i = 0; i < 30; i++) { // 최대 ~6초
    time_t now = time(nullptr);
    if (now > 1609459200) {
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
// Insecure way to detect the firebase
// but, this project isn't public or official, so...
// for efficiency
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

  // Targeting JSON shape
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

  // Name and Country
  out.name = fields["name"]["stringValue"] | "";
  out.country = fields["country"]["stringValue"] | "";

  // BirthDate
  out.birthDateUnix = 0;
  if (!fields["birthDate"].isNull() && !fields["birthDate"]["integerValue"].isNull()) {
    String v = fields["birthDate"]["integerValue"].as<String>();
    out.birthDateUnix = (int32_t)v.toInt();
  }

  // Battery Due Date
  out.batteryDueUnix = 0;
  if (!fields["batteryDueDate"].isNull() && !fields["batteryDueDate"]["integerValue"].isNull()) {
    String v = fields["batteryDueDate"]["integerValue"].as<String>();
    out.batteryDueUnix = (int32_t)v.toInt();
  }

  // lastBatteryReplacementDate
  out.lastBatteryUnix = 0;
  if (!fields["lastBatteryReplacementDate"].isNull() && !fields["lastBatteryReplacementDate"]["integerValue"].isNull()) {
    String v = fields["lastBatteryReplacementDate"]["integerValue"].as<String>();
    out.lastBatteryUnix = (int32_t)v.toInt();
  }

  // visaType
  out.visaType = fields["visaType"]["stringValue"] | "";

  // canFinancial
  out.canFinancial = fields["canFinancialTransactions"]["booleanValue"] | false;

  // tendency
  out.tendency = 0;
  if (!fields["tendency"].isNull() && !fields["tendency"]["integerValue"].isNull()) {
    String v = fields["tendency"]["integerValue"].as<String>();
    out.tendency = clampInt(v.toInt(), 0, 10);
  }

  out.loaded = true;
  return true;
}

// Use name string to get database infos
// Use NFC to check the assigned user name on the NFC tag 
// -> search the name through DB and get the data
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

  String docStr;
  serializeJson(docObj, docStr);

  if (!parseMemberFromFirestore(docStr, out)) {
    Serial.println("[FS-QUERY] parseMemberFromFirestore failed");
    return false;
  }

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

// Update 'lastBatteryReplacementDate'
// For recalcualting the battery percentage
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

// Battery percentage calcualation
// Map the 'lastBatteryReplacementDate' and 'batteryDueDate'
// and use current date to check the precentage
int calBatteryPercent(const MemberInfo &m) {
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

// Display layout / rendering
void basicLayout() {
  int16_t W = gfx->width();
  int16_t H = gfx->height();

  int16_t top = BAR_H + 12;     // WiFi 바와 카드 사이 간격
  int16_t sideMargin = W / 14;  // 좌우 여백
  int16_t bottomMargin = 16;

  int16_t cardW = W - sideMargin * 2;
  int16_t cardH = H - top - bottomMargin;

  // Touch area set
  touchBox.x = sideMargin;
  touchBox.y = top;
  touchBox.w = cardW;
  touchBox.h = cardH;

  // Split in half
  // Left, member(citizen) info
  int16_t halfW = cardW / 2;
  memberBox.x = touchBox.x + 8;
  memberBox.y = touchBox.y + 8;
  memberBox.w = halfW - 12;
  memberBox.h = cardH - 16;
  // Right, NFC(battery) info
  nfcBox.x = touchBox.x + halfW + 4;
  nfcBox.y = touchBox.y + 8;
  nfcBox.w = cardW - halfW - 12;
  nfcBox.h = cardH - 16;
}

// GFX, draw member(citizen) info on the screen
// Need to draw the info manually
// If the info string goes over the box, GFX can't control it...
void drawMemberInfo(const MemberInfo &m) {
  // Change box design
  gfx->fillRect(memberBox.x, memberBox.y, memberBox.w, memberBox.h, COLOR_PANEL);
  gfx->drawRect(memberBox.x, memberBox.y, memberBox.w, memberBox.h, COLOR_WHITE);

  // Title header box
  int16_t headerH = 32;
  gfx->fillRect(memberBox.x, memberBox.y, memberBox.w, headerH, COLOR_ACCENT);
  gfx->drawLine(memberBox.x, memberBox.y + headerH, 
                memberBox.x + memberBox.w, memberBox.y + headerH, COLOR_BLACK);

  gfx->setFont(u8g2_font_6x10_tr);

  // header text
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_BLACK);
  gfx->setCursor(memberBox.x + 6, memberBox.y + 22);
  gfx->print("CITIZEN INFO");

  // Info text start point
  const int padX = 8;
  const int padY = headerH + 18;
  int16_t x = memberBox.x + padX;
  int16_t y = memberBox.y + padY;

  gfx->setTextSize(1);

  // If there's no data
  // When user didn't assign there name on the database, but only on NFC
  // Just in case...
  if (!m.loaded) {
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

  // GFX, draw infos on each line
  // Label, data
  // And the line height(manually)
  auto drawLabelValue = [&](const char* label, const String &value, int line) {
    int16_t ly = y + line * lineH;
    gfx->setCursor(x, ly);
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->print(label);

    gfx->setCursor(x + 80, ly);
    gfx->setTextColor(COLOR_WHITE);
    gfx->print(value);
  };

  // GFX, display every infos
  drawLabelValue("Name", m.name, 0);
  drawLabelValue("Country", m.country, 1);
  drawLabelValue("Birth", unixToMDYOffset100(m.birthDateUnix), 2);
  drawLabelValue("Due", unixToMDYOffset100(m.batteryDueUnix), 3);
  drawLabelValue("Last rep", unixToMDYOffset100(m.lastBatteryUnix),4);
  String visaStr = (m.visaType.length() > 0) ? m.visaType : String("NONE");
  drawLabelValue("Visa", visaStr, 5);
  drawLabelValue("Finance", m.canFinancial ? "YES" : "NO", 6);
  String tend = String(m.tendency) + "/10";
  drawLabelValue("Tendency", tend, 7);

  // Battery percentage indicator
  int pct = calBatteryPercent(m);
  if (pct >= 0) {
    int16_t baseY = y + 8 * lineH + 4;
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->setCursor(x, baseY);
    gfx->print("Battery");

    int barW = memberBox.w - padX * 2;
    int barH = 10;
    int barX = x;
    int barY = baseY + 6;

    gfx->drawRect(barX, barY, barW, barH, COLOR_WHITE);
    int fillW = barW * pct / 100;

    uint16_t barColor = COLOR_ACCENT;
    if (pct <= 30) barColor = COLOR_RED;
    else if (pct <= 60) barColor = COLOR_YELLOW;

    gfx->fillRect(barX + 1, barY + 1, fillW - 2 > 0 ? fillW - 2 : 0, barH - 2, barColor);

    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(barX + barW - 36, baseY);
    gfx->print(pct);
    gfx->print("%");
  }

  gfx->setFont();
}

void drawNFCInfo() {
  // Change box design
  gfx->fillRect(nfcBox.x, nfcBox.y, nfcBox.w, nfcBox.h, COLOR_PANEL);
  gfx->drawRect(nfcBox.x, nfcBox.y, nfcBox.w, nfcBox.h, COLOR_WHITE);

  // Title header box
  int16_t headerH = 32;
  gfx->fillRect(nfcBox.x, nfcBox.y, nfcBox.w, headerH, COLOR_ACCENT2);
  gfx->drawLine(nfcBox.x, nfcBox.y + headerH, 
                nfcBox.x + nfcBox.w, nfcBox.y + headerH, COLOR_BLACK);

  gfx->setFont(u8g2_font_6x10_tr);

  // header text
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(nfcBox.x + 6, nfcBox.y + 22);
  gfx->print("BATTERY DATA");

  const int padX = 8;
  const int padY = headerH + 18;
  int16_t x = nfcBox.x + padX;
  int16_t y = nfcBox.y + padY;

  gfx->setTextSize(1);
  int lineH = 16;

  // Step indicator
  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setCursor(x, y);
  gfx->print("Step 1: Identification");

  gfx->setCursor(x, y + lineH);
  gfx->print("Step 2: Replacement Confirm");

  // Current steps
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(x, y + lineH * 3);
  gfx->print("Status:");

  gfx->setCursor(x, y + lineH * 4);
  gfx->print(g_lastNFCStatus);

  // Citizen name from NFC tag
  if (g_hasFirstTag && g_firstTagText.length() > 0) {
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->setCursor(x, y + lineH * 6);
    gfx->print("Citizen:");

    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(x + 60, y + lineH * 6);
    gfx->print(g_firstTagText);
  }

  // DB update process infos
  if (g_dbUpdateAttempted) {
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->setCursor(x, y + lineH * 7);
    gfx->print("New Replacement:");

    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(x + 110, y + lineH * 7);
    if (g_dbLastRepNew > 0) {
      gfx->print(unixToMDY(g_dbLastRepNew));
    } else {
      gfx->print("-");
    }
  }

  gfx->setFont();
}

// GFX, render info boxes
// Maing box
// Left, Citizen info box
// Right, Battery info box
void renderGFXScreen() {
  basicLayout();

  gfx->fillScreen(COLOR_BG);

  // Box shadow, draw it once before the box
  gfx->fillRect(touchBox.x + 4, touchBox.y + 4, touchBox.w, touchBox.h, COLOR_BLACK);
  //Actual box
  gfx->fillRect(touchBox.x, touchBox.y, touchBox.w, touchBox.h, COLOR_PANEL_IN);
  gfx->drawRect(touchBox.x, touchBox.y, touchBox.w, touchBox.h, COLOR_WHITE);

  // Left, citizen
  drawMemberInfo(currentMember);

  // Right, battery
  drawNFCInfo();

  // Top, WIFI
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

// Debugging purpose
// Tried to use this for extra button on the top or side,
// but the orientation worked weird, so didn't improve more
void printTouchDebug(const TS_Point &p, int16_t sx, int16_t sy, bool inBox) {
  Serial.printf("RAW(%d,%d) -> SCREEN(%d,%d) inBox=%d | OBS X[%d..%d] Y[%d..%d]\n",
                p.x, p.y, sx, sy, inBox ? 1 : 0,
                obsMinX, obsMaxX, obsMinY, obsMaxY);
}


// Load name from DB
bool loadMemberByName(const String &name) {
  MemberInfo m;
  bool ok = firestoreGetMemberByName(name, m);
  if (!ok) return false;

  currentMember = m;
  return true;
}

// ---------------------------------------------
// ---------------------------------------------

// PN532, NFC read.write module

// PN532 SPI wiring
// Make sure to togle the physical switch on the module
// It controls I2C, SPI states
#define PN532_SCK   18
#define PN532_MISO  19
#define PN532_MOSI  23
// SS = CS on teh board
#define PN532_SS    21

// Switch gate (INPUT_PULLUP)
// Just followed the tutorial pin set up
#define NFC_ENABLE_PIN 35

// NTAG page block set up
// Before page 4, NFC stores basic infos
// which I should not touch
static const uint8_t START_PAGE = 4;
static const uint8_t END_PAGE   = 9;

// NFC string reading 'header' debugging
// Using header MK(from my name haha) to filter the string from NFC

static const uint8_t SIG0 = 'M';
static const uint8_t SIG1 = 'K';
// String g_writeText = String("Minkyu Kim");
String g_writeText = String("");

// tag hold and re attach delay
bool tagHeld = false;
uint8_t lastUid[7] = {0};
uint8_t lastUidLen = 0;

uint32_t lastSeenMs = 0;
const uint32_t TAG_RELEASE_MS = 600;

// PN532
Adafruit_PN532 nfc(PN532_SS);

// NFC utilities
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

// page read retry, for stability
bool readPageRetry(uint8_t page, uint8_t out4[4], int tries = 8) {
  for (int i = 0; i < tries; i++) {
    if (nfc.ntag2xx_ReadPage(page, out4)) return true;
    delay(12);
  }
  return false;
}
// page write retry, for stability
bool writePageRetry(uint8_t page, const uint8_t in4[4], int tries = 8) {
  uint8_t buf[4];
  memcpy(buf, in4, 4);

  for (int i = 0; i < tries; i++) {
    if (nfc.ntag2xx_WritePage(page, buf)) return true;
    delay(12);
  }
  return false;
}

// Confirming is the tag staying on the same spot
// check it 3 times straight to confirm
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
          // Increase the delay, if need more stability
          delay(80);
          return true;
        }
      }
    }
    delay(30);
  }
  return false;
}

// Deleting original NFC data
// [IMPORTANT]
// When write data on NFC, it just overwrite the data
// It means, if there's any leff-over strong after the written string, it's stil there
// For preventing it, delete(format) the previous data first
bool clearUserRegion() {
  Serial.println("[FORMAT] clear pages 4-9");
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

// NFC write
bool writeTextToTag(const String &text) {
  size_t len = text.length();
  if (len > 255) len = 255;

  uint16_t payloadCap = (uint16_t)((END_PAGE - START_PAGE) * 4); // header 제외 payload
  if (len > payloadCap) {
    Serial.print("[WRITE] Too long. cap=");
    Serial.println(payloadCap);
    return false;
  }

  // Make sure to clear/delete the previous data
  if (!clearUserRegion()) {
    Serial.println("[WRITE] clearUserRegion failed");
    return false;
  }

  // add header
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

// NFC read, with header
bool readTextFromTagStrict(String &outText) {
  outText = "";

  uint8_t header[4];
  if (!readPageRetry(START_PAGE, header, 12)) return false;

  // check header
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

// NFC read without header
// Purpose: writing data with header(M,K) requires a  proces through PN532
// But, for participants, it's easier to assign their name with NFC Tool on their phone
// This function allows participants to set up their name string on NFC tag easily
// After once it written by this board, doesn't requires it anymore
// If I add an additional NFC data write tool for participants, this function doesn't need anymore
bool readTextFromTagLoose(String &outText) {
  outText = "";
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

  // Remove 'Ten' string in front
  // With 'NFC Tool' from the phone, it always includes 'Ten' string
  // Remove it to read the correct string in NFC
  if (outText.startsWith("Ten")) {
    outText.remove(0, 3);
    outText.trim();
  }

  Serial.print("[READ LOOSE] text = ");
  Serial.println(outText);

  return (outText.length() > 0);
}

// NFC read, try both function for stability
bool readTextFromTagHybridOnce(String &outText) {
  // Try with header
  // -> better performance
  if (readTextFromTagStrict(outText)) {
    Serial.print("[READ] With header: ");
    Serial.println(outText);
    return (outText.length() > 0);
  }

  // If fail first, try with out header
  if (readTextFromTagLoose(outText)) {
    Serial.print("READ] Without header: ");
    Serial.println(outText);
    return true;
  }

  return false;
}

// NFC read, hybrid multiple attempt
bool readTextFromTagHybridRobust(String &outText) {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (readTextFromTagHybridOnce(outText)) {
      return true;
    }
    delay(60);
  }
  Serial.println("[READ] Failed");
  return false;
}

bool nfcEnabled() {
  return (digitalRead(NFC_ENABLE_PIN) == LOW);
}

//First NFC tag action
void handleFirstNfcTag(const String &text) {
  g_firstTagText = text;
  // Save the tag string for writing on the replaced NFC tag
  g_writeText  = g_firstTagText;
  g_hasFirstTag  = true;
  g_secondWriteDone = false;
  g_secondTagRemovedAt = 0;

  // Allow second tag after the replacement process
  // (touch screen)
  g_readyForSecondTag = false;

  // Reset DB update state
  g_dbUpdateAttempted = false;
  g_dbUpdateSuccess   = false;
  g_dbLastRepNew      = 0;

  // Load firestore date based on the name on NFC
  bool ok = loadMemberByName(g_firstTagText);

  if (ok) {
    g_lastNFCStatus = "First tag OK";
  } else {
    g_lastNFCStatus = "Name not found";
  }

  g_lastNFCText = g_firstTagText;

  Serial.print("[Replacement] First tag name = ");
  Serial.println(g_firstTagText);

  renderGFXScreen();
}

// Handling NFC after the first stage
void handleNFC() {
  if (!nfcEnabled()) {
    tagHeld = false;
    lastUidLen = 0;
    lastSeenMs = 0;
    delay(50);
    return;
  }

  // Check NFC presence once more
  uint8_t probeUid[7]; 
  uint8_t probeLen = 0;
  bool present = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, probeUid, &probeLen, 40);

  if (!present) {
    // Check is NFC still on teh spot
    if (tagHeld && lastSeenMs != 0 && (millis() - lastSeenMs) > TAG_RELEASE_MS) {
      tagHeld = false;
      lastUidLen = 0;

      if (g_secondWriteDone && g_secondTagRemovedAt == 0) {
        g_secondTagRemovedAt = millis();
        g_lastNFCStatus = "Second tag removed";
      } else {
        g_lastNFCStatus = "Tag removed";
      }
      g_lastNFCText = "";
      g_lastNFCUid  = "";
      renderGFXScreen();
    }
    delay(20);
    return;
  } else {
    lastSeenMs = millis();
  }

  // Check NFC's UID more
  uint8_t uid[7]; 
  uint8_t uidLen = 0;
  bool stable = waitStableTag(uid, uidLen, 1500);
  if (!stable) {
    delay(20);
    return;
  }

  lastSeenMs = millis();

  // Preventing reading same data from NFC multiple times
  if (tagHeld && uidEqual(uid, uidLen, lastUid, lastUidLen)) {
    return;
  }

  tagHeld = true;
  memcpy(lastUid, uid, uidLen);
  lastUidLen = uidLen;

  // UID string
  // For desbugging, and saved for just in case
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

  // NFC read process
  String text;
  if (!readTextFromTagHybridRobust(text)) {
    Serial.println("[Replacement READ] failed");
    g_lastNFCStatus = "Read failed";
    g_lastNFCText   = "";
    renderGFXScreen();
    delay(100);
    return;
  }

  Serial.print("[Replacement READ] text = ");
  Serial.println(text);

  // Process starts
  if (!g_hasFirstTag) {
    handleFirstNfcTag(text);
  }
  else if (!g_secondWriteDone) {
    if (!g_readyForSecondTag) {
      Serial.println("[Replacement] New first tag (screen not touched yet)");
      handleFirstNfcTag(text);
      return;
    }

    Serial.print("[Replacement SECOND] write name = ");
    Serial.println(g_firstTagText);

    if (writeTextToTag(g_firstTagText)) {
      String after;
      if (readTextFromTagHybridRobust(after)) {
        Serial.print("[Replacement VERIFY] text = ");
        Serial.println(after);

        g_lastNFCText     = after;
        g_secondWriteDone = true;
        g_readyForSecondTag = false;

        g_dbUpdateAttempted = false;
        g_dbUpdateSuccess   = false;
        g_dbLastRepNew      = 0;

        // Update Firestore DB
        // Update lastReplacementDate with current date
        if (currentMember.loaded && currentMember.docPath.length() > 0) {
          g_dbUpdateAttempted = true;
          g_lastNFCStatus     = "DB updating...";
          renderGFXScreen();

          if (ensureTimeSynced()) {
            time_t nowT = time(nullptr);
            if (nowT > 0) {
              int32_t lastRep = (int32_t)nowT;

              if (firestorePatchLastReplacementDate(currentMember.docPath, lastRep)) {
                Serial.println("[Replacement] Firestore lastReplacementDate updated");

                currentMember.lastBatteryUnix = lastRep;

                g_dbUpdateSuccess = true;
                g_dbLastRepNew    = lastRep;


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

    renderGFXScreen();
  }
  else {
    // resetting the cycle to accept the new NFC tag
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

    currentMember        = MemberInfo{};
    renderGFXScreen();
  }

  delay(150);
}

// ----------------------------------------------------
// ----------------------------------------------------
// ----------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // TFT + GFX
  gfx->begin(40000000);
  gfx->setRotation(1);

  // Touch
  hspi.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  ts.begin(hspi);

  pinMode(NFC_ENABLE_PIN, INPUT_PULLUP);
  SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

  // PN532 init
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

  // Emptying the member info
  currentMember = MemberInfo{};
  renderGFXScreen();

  // WIFI connect
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
      renderGFXScreen();
    } else {
      Serial.println("[AUTH] sign-in failed");
    }
  } else {
    Serial.println("[WiFi] connect timeout (15s)");
    drawWifiStatusBar(true);
  }
}

void loop() {
  // Update WIFI status bar
  drawWifiStatusBar();

  // WIFI reconnect attempt
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 5000) {
      lastTry = millis();
      connectWiFi();
      drawWifiStatusBar(true);
    }
  }

  // Center box touch action
  bool touched = ts.touched();
  if (touched && !lastTouched && (millis() - lastTouchMs) > debounceMs) {
    TS_Point p = ts.getPoint();
    int16_t sx, sy;
    mapTouchToScreen(p, sx, sy);

    bool inMainBox = pointInRect(sx, sy, touchBox);

    printTouchDebug(p, sx, sy, inMainBox);

    if (inMainBox) {
      // Send CMD serial to Arduino Nano
      sendToUnoCmd("REPLACEMENT_START");

      // Release the secondtag bool
      // So, can send 'SERVO_RESET' cmd serial
      g_readyForSecondTag = true;

      g_lastNFCStatus = "Screen touched: ready 2nd tag";
      renderGFXScreen();
    }

    lastTouchMs = millis();
  }
  lastTouched = touched;

  // PN532 handling
  handleNFC();

  // Count 3 sec after releasing the second tagging
  // For resetting the servo position
  if (g_secondWriteDone && g_secondTagRemovedAt != 0) {
    if (millis() - g_secondTagRemovedAt >= 3000) {
      sendToUnoCmd("SERVO_RESET");

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
      renderGFXScreen();
    }
  }

  delay(10);
}