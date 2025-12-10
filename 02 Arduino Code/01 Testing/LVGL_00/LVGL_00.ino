#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "secrets.h"

Preferences prefs;

// ESP32-32E 3.5" pin setting
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

// HSPI
SPIClass hspi(HSPI);

// GFX (ST7796 SPI)
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
static const uint16_t COLOR_RED   = 0xF800;
static const uint16_t COLOR_WHITE = 0xFFFF;
static const uint16_t COLOR_BLACK = 0x0000;
static const uint16_t COLOR_GRAY  = 0x2104;

// Wifit bar height(for debbuing)
static const int BAR_H = 24;

// UI box size
struct Rect { int16_t x, y, w, h; };
Rect touchBox;


// Firebase Auth / Firestore REST 
String g_idToken;
uint32_t g_tokenExpiryMs = 0;

// Memebers 
const char* MEMBER_DOCS[] = {"members/m1", "members/m2", "members/m3"};
const int MEMBER_COUNT = 3;
int currentMemberIndex = 0;

// member data 
// tendency: 0~10 정수 1개만 사용
struct MemberInfo {
  String docPath;     // members/m1
  String name;
  String country;

  int32_t birthDateInt = 0;  // YYYYMMDD (0이면 없음)
  bool hasVisa = false;
  String visaType;
  bool canFinancial = false;

  int tendency = 0;          // 0~10
  int32_t replacedDateInt = 0;
  bool loaded = false;
};
MemberInfo currentMember;

// Serial Read setup
static const char* CMD_PREFIX = "@CMD:";
void handleCommand(const String& cmd) {
  // cmd에는 "DO_ACTION" 같은 본문만 들어옴
  Serial.print("[ESP32] CMD = ");
  Serial.println(cmd);

  if (cmd == "ACTION_1") {
    // TODO: 여기에서 원하는 동작 실행
gfx->setFont(u8g2_font_6x10_tr);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(8, 32);
        gfx->print("[ESP32] Doing action!");
  gfx->setFont();

    Serial.println("[ESP32] Doing action!");
  } else if (cmd == "HELLO_ESP32") {
    Serial.println("[ESP32] Hello back!");
  } else {
    Serial.println("[ESP32] Unknown command");
  }
}
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
    // 너무 길어지는 것 방지
    if (buf.length() < 200) buf += c;
    else buf = "";
  }
  return false;
}
void processSerialCommands() {
  String line;
  if (!readLine(line)) return;

  line.trim();
  if (line.startsWith(CMD_PREFIX)) {
    String cmd = line.substring(strlen(CMD_PREFIX));
    cmd.trim();
    handleCommand(cmd);
  } else {
    // 필요하면 디버그 출력
    // Serial.print("[ESP32] DBG: "); Serial.println(line);
  }
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

String dateIntToPretty(int32_t yyyymmdd) {
  if (yyyymmdd <= 0) return "";
  int32_t y = yyyymmdd / 10000;
  int32_t m = (yyyymmdd / 100) % 100;
  int32_t d = yyyymmdd % 100;
  char buf[16];
  snprintf(buf, sizeof(buf), "%04ld-%02ld-%02ld", (long)y, (long)m, (long)d);
  return String(buf);
}

int32_t dateStrToInt(const String &s) {
  String digits;
  digits.reserve(8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') digits += c;
  }
  if (digits.length() < 8) return 0;
  return (int32_t)digits.substring(0, 8).toInt(); // YYYYMMDD
}


void sendToUnoCmd(const char* cmd) {
  Serial.print("@CMD:");
  Serial.println(cmd);
}

// WiFi 
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
    // + WiFi.localIP().toString()
    //  + " RSSI:" + String(WiFi.RSSI());
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

  gfx->fillRect(0, 0, gfx->width(), BAR_H, COLOR_BLACK);

  gfx->setFont(u8g2_font_6x10_tr);
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(4, 16);
  gfx->print(s);
  gfx->setFont();
}

// TLS (for debugging)
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

// Firestore raw GET 
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

//Firestore create & patch 
bool firestoreCreateDocument(const String &collection, const String &docId, const String &bodyJson) {
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
             + collection
             + "?documentId=" + docId;

  if (!https.begin(client, url)) return false;
  https.addHeader("Authorization", "Bearer " + g_idToken);
  https.addHeader("Content-Type", "application/json");

  int code = https.POST(bodyJson);
  String resp = https.getString();
  https.end();

  Serial.printf("[FS] CREATE %s/%s -> HTTP %d\n", collection.c_str(), docId.c_str(), code);
  if (code != 200) Serial.println(resp);

  return (code == 200);
}

bool firestorePatchDocument(const String &docPath, const String &bodyJson) {
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

  if (!https.begin(client, url)) return false;
  https.addHeader("Authorization", "Bearer " + g_idToken);
  https.addHeader("Content-Type", "application/json");

  int code = https.sendRequest("PATCH", (uint8_t*)bodyJson.c_str(), bodyJson.length());
  String resp = https.getString();
  https.end();

  Serial.printf("[FS] PATCH %s -> HTTP %d\n", docPath.c_str(), code);
  if (code != 200) Serial.println(resp);

  return (code == 200);
}

//(For seeding) Firestore JSON type generate
// birthDate/tendency를 integerValue로 저장
String buildMemberDocBody(
  const char* name,
  int32_t birthDateYYYYMMDD,
  const char* country,
  bool hasVisa,
  const char* visaType,
  bool canFinancialTransactions,
  int tendency0to10,
  int32_t replacedDateYYYYMMDD
) {
  StaticJsonDocument<1024> root;
  JsonObject fields = root.createNestedObject("fields");

  fields["name"]["stringValue"] = name;
  fields["birthDate"]["integerValue"] = String(birthDateYYYYMMDD);
  fields["country"]["stringValue"] = country;

  JsonObject visa = fields.createNestedObject("visa");
  JsonObject visaMap = visa.createNestedObject("mapValue");
  JsonObject visaFields = visaMap.createNestedObject("fields");
  visaFields["hasVisa"]["booleanValue"] = hasVisa;
  visaFields["type"]["stringValue"] = visaType;

  fields["canFinancialTransactions"]["booleanValue"] = canFinancialTransactions;

  tendency0to10 = clampInt(tendency0to10, 0, 10);
  fields["tendency"]["integerValue"] = String(tendency0to10);
  fields["replacedDate"]["integerValue"] = String(replacedDateYYYYMMDD);
 
  String out;
  serializeJson(root, out);
  return out;
}

void seedMembersOnce() {
  String bodyM1 = buildMemberDocBody("Minkyu Kim", 19971025, "South Korea", true,  "F-1",  true,  9, 20251126);
  String bodyM2 = buildMemberDocBody("Max Hahn",     20000315, "USA",     false, "none", false, 5, 20210512);
  String bodyM3 = buildMemberDocBody("Augie Fesh", 19940623, "Canada",     true,  "H-1B", true,  2, 19970823);

  if (!firestoreCreateDocument("members", "m1", bodyM1)) firestorePatchDocument("members/m1", bodyM1);
  if (!firestoreCreateDocument("members", "m2", bodyM2)) firestorePatchDocument("members/m2", bodyM2);
  if (!firestoreCreateDocument("members", "m3", bodyM3)) firestorePatchDocument("members/m3", bodyM3);
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

  // birthDate: integerValue > stringValue/timestampValue fallback
  out.birthDateInt = 0;
  if (!fields["birthDate"].isNull()) {
    if (!fields["birthDate"]["integerValue"].isNull()) {
      String v = fields["birthDate"]["integerValue"].as<String>();
      out.birthDateInt = (int32_t)v.toInt();
    } else {
      String s = fields["birthDate"]["stringValue"] | fields["birthDate"]["timestampValue"] | "";
      out.birthDateInt = dateStrToInt(s);
    }
  }

  out.hasVisa = false;
  out.visaType = "";
  JsonObject visaFields = fields["visa"]["mapValue"]["fields"];
  if (!visaFields.isNull()) {
    out.hasVisa = visaFields["hasVisa"]["booleanValue"] | false;
    out.visaType = visaFields["type"]["stringValue"] | "";
  }

  out.canFinancial = fields["canFinancialTransactions"]["booleanValue"] | false;

  // tendency: integerValue (0~10)
  out.tendency = 0;
  if (!fields["tendency"].isNull() && !fields["tendency"]["integerValue"].isNull()) {
    String v = fields["tendency"]["integerValue"].as<String>();
    out.tendency = clampInt(v.toInt(), 0, 10);
  } else if (!fields["tendencies"].isNull()) {
    
    JsonArray vals = fields["tendencies"]["arrayValue"]["values"].as<JsonArray>();
    if (!vals.isNull() && vals.size() > 0) {
      String s0 = vals[0]["stringValue"] | "";
      int n = s0.toInt();
      out.tendency = clampInt(n, 0, 10);
    }
  }

  out.replacedDateInt = 0;
  if (!fields["replacedDate"].isNull()) {
    if (!fields["replacedDate"]["integerValue"].isNull()) {
      String v = fields["replacedDate"]["integerValue"].as<String>();
      out.replacedDateInt = (int32_t)v.toInt();
    } else {
      String s = fields["replacedDate"]["stringValue"] | fields["replacedDate"]["timestampValue"] | "";
      out.replacedDateInt = dateStrToInt(s);
    }
  }

  out.loaded = true;
  return true;
}

// display layout, render
void computeLayout() {
  int16_t W = gfx->width();
  int16_t H = gfx->height();

  int16_t top = BAR_H + 8;
  int16_t availH = H - top;

  touchBox.x = W / 12;
  touchBox.y = top + availH / 12;
  touchBox.w = W - 2 * touchBox.x;
  touchBox.h = availH - 2 * (availH / 12);
}

void drawMemberInfo(const MemberInfo &m) {
  const int pad = 14;
  int16_t x = touchBox.x + pad;
  int16_t y = touchBox.y + pad;

  gfx->setFont(u8g2_font_6x10_tr);

  int size = 2;
  gfx->setTextSize(size);
  gfx->setTextColor(COLOR_WHITE);

  if (!m.loaded) {
    gfx->setCursor(x, y + 12*size);
    gfx->print("Loading...");
    gfx->setFont();
    return;
  }

  gfx->setCursor(x, y + 12*size);
  gfx->print(m.docPath);

  gfx->setCursor(x, y + 30*size);
  gfx->print("Name: "); gfx->print(m.name);

  gfx->setCursor(x, y + 46*size);
  gfx->print("Country: "); gfx->print(m.country);

  gfx->setCursor(x, y + 62*size);
  gfx->print("Birth: ");
  if (m.birthDateInt > 0) gfx->print(dateIntToPretty(m.birthDateInt));
  else gfx->print("-");

  gfx->setCursor(x, y + 78*size);
  gfx->print("Visa: ");
  gfx->print(m.hasVisa ? "YES" : "NO");
  if (m.hasVisa && m.visaType.length()) {
    gfx->print(" ("); gfx->print(m.visaType); gfx->print(")");
  }

  gfx->setCursor(x, y + 94*size);
  gfx->print("Finance: "); gfx->print(m.canFinancial ? "YES" : "NO");

  gfx->setCursor(x, y + 110*size);
  gfx->print("Tendency: ");
  gfx->print(m.tendency);
  gfx->print("/10");

  gfx->setCursor(x, y + 126*size);
  gfx->print("Last Replaced Date: ");
  if (m.replacedDateInt > 0) gfx->print(dateIntToPretty(m.replacedDateInt));
  else gfx->print("-");

  gfx->setFont();
}

void renderMemberScreen() {
  computeLayout();

  gfx->fillScreen(COLOR_BLACK);

  gfx->fillRect(touchBox.x + 6, touchBox.y + 6, touchBox.w, touchBox.h, COLOR_GRAY);
  gfx->fillRect(touchBox.x, touchBox.y, touchBox.w, touchBox.h, COLOR_RED);
  gfx->drawRect(touchBox.x, touchBox.y, touchBox.w, touchBox.h, COLOR_WHITE);

  drawMemberInfo(currentMember);
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

// member load and transition
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

void nextMember() {
  currentMemberIndex = (currentMemberIndex + 1) % MEMBER_COUNT;
  loadMemberByIndex(currentMemberIndex);
  renderMemberScreen();
}


void setup() {
  Serial.begin(115200);
  // Serial.begin(9600);
  delay(200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  gfx->begin(40000000);
  gfx->setRotation(1);

  hspi.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  ts.begin(hspi);

  currentMember.loaded = false;
  renderMemberScreen();

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
      bool existM1 = false;
      {
        int code; String resp;
        existM1 = firestoreGetDocumentRaw("members/m1", code, resp);
      }

      prefs.begin("app", false);
      bool seeded = prefs.getBool("seeded", false);

      if (!seeded || !existM1) {
        seedMembersOnce();
        prefs.putBool("seeded", true);
        Serial.println("[SEED] done (or repaired)");
      } else {
        seedMembersOnce();
        Serial.println("[SEED] already");
      }
      prefs.end();

      loadMemberByIndex(currentMemberIndex);
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
  drawWifiStatusBar();
  processSerialCommands();

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 5000) {
      lastTry = millis();
      connectWiFi();
      drawWifiStatusBar(true);
    }
  }

  bool touched = ts.touched();
  if (touched && !lastTouched && (millis() - lastTouchMs) > debounceMs) {
    TS_Point p = ts.getPoint();
    int16_t sx, sy;
    mapTouchToScreen(p, sx, sy);

    bool inBox = pointInRect(sx, sy, touchBox);
    printTouchDebug(p, sx, sy, inBox);

    nextMember();

    lastTouchMs = millis();
    sendToUnoCmd("DO_ACTION");
  }

  lastTouched = touched;
  delay(10);
}