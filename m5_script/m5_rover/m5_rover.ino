// m5_rover.ino
// M5StickC Plus2 + RoverC Pro 制御ファームウェア
// HTTP API でドライブ命令を受け付ける

#include <M5StickCPlus2.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "credentials.h"  // ssid1〜3, pass1〜3

// ===================== RoverC Pro I2C =====================
#define ROVER_ADDR   0x38
#define REG_MOTOR0   0x00  // front-left
#define REG_MOTOR1   0x01  // front-right
#define REG_MOTOR2   0x02  // rear-left
#define REG_MOTOR3   0x03  // rear-right
#define REG_BATTERY  0x20  // バッテリー ADC (uint16, 12bit, 3.3V ref, 2x divider)

// ===================== Web =====================
WebServer server(80);

// ===================== State =====================
String currentDriver = "";          // 現在のドライバー（"puchiteya" など、空なら空き）
unsigned long driverUntil   = 0;    // ドライブ終了予定時刻
bool isMoving = false;
int defaultSpeed = 60;              // デフォルト速度 0-100

// ===================== Motor control =====================

bool isMotorPowered() {
  Wire.beginTransmission(ROVER_ADDR);
  return Wire.endTransmission() == 0;
}

// バッテリー電圧を返す (V)。読み取り失敗時は -1
float getBatteryVoltage() {
  Wire.beginTransmission(ROVER_ADDR);
  Wire.write(REG_BATTERY);
  if (Wire.endTransmission() != 0) return -1;
  Wire.requestFrom(ROVER_ADDR, 2);
  if (Wire.available() < 2) return -1;
  uint16_t raw = Wire.read() | (Wire.read() << 8);
  return raw * 3.3f / 4096.0f * 2.0f;  // 12bit ADC, 3.3V ref, 2x 分圧
}

// 電圧→残量% (16340: 3.0V=0%, 4.2V=100%)
int getBatteryPercent(float v) {
  if (v < 0) return -1;
  int pct = (int)((v - 3.0f) / (4.2f - 3.0f) * 100.0f);
  return constrain(pct, 0, 100);
}

void setMotors(int8_t m0, int8_t m1, int8_t m2, int8_t m3) {
  Wire.beginTransmission(ROVER_ADDR);
  Wire.write(REG_MOTOR0); Wire.write((uint8_t)m0);
  Wire.endTransmission();
  Wire.beginTransmission(ROVER_ADDR);
  Wire.write(REG_MOTOR1); Wire.write((uint8_t)m1);
  Wire.endTransmission();
  Wire.beginTransmission(ROVER_ADDR);
  Wire.write(REG_MOTOR2); Wire.write((uint8_t)m2);
  Wire.endTransmission();
  Wire.beginTransmission(ROVER_ADDR);
  Wire.write(REG_MOTOR3); Wire.write((uint8_t)m3);
  Wire.endTransmission();
}

void stopMotors() {
  setMotors(0, 0, 0, 0);
  isMoving = false;
}

// メカナムホイール配置:
//   M0(前左) M1(前右)
//   M2(後左) M3(後右)
// 前進: 全部同じ方向
// 後退: 全部逆
// 右回転: 左側+, 右側-
// 左回転: 左側-, 右側+
// 右横移動(strafe): M0+, M1-, M2-, M3+
// 左横移動(strafe): M0-, M1+, M2+, M3-

void driveForward(int8_t spd) { setMotors( spd,  spd,  spd,  spd); }
void driveBackward(int8_t spd){ setMotors(-spd, -spd, -spd, -spd); }
void rotateRight(int8_t spd)  { setMotors( spd, -spd,  spd, -spd); }
void rotateLeft(int8_t spd)   { setMotors(-spd,  spd, -spd,  spd); }
void strafeRight(int8_t spd)  { setMotors( spd, -spd, -spd,  spd); }
void strafeLeft(int8_t spd)   { setMotors(-spd,  spd,  spd, -spd); }

// ===================== Display =====================

void updateDisplay() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 0);

  bool powered = isMotorPowered();
  float bv   = powered ? getBatteryVoltage() : -1;
  int   bpct = getBatteryPercent(bv);

  // 電源 & バッテリーインジケーター
  M5.Lcd.setTextSize(1);
  if (powered) {
    uint16_t bcolor = bpct > 50 ? GREEN : (bpct > 20 ? YELLOW : RED);
    M5.Lcd.setTextColor(bcolor);
    M5.Lcd.setCursor(170, 0);
    M5.Lcd.printf("BAT:%3d%%", bpct);
  } else {
    M5.Lcd.setTextColor(RED);
    M5.Lcd.setCursor(200, 0);
    M5.Lcd.print("PWR:OFF");
  }

  M5.Lcd.setTextSize(2);
  if (currentDriver.isEmpty()) {
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.setTextColor(DARKGREY);
    M5.Lcd.println("rover");
    M5.Lcd.println("standby");
  } else {
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.println(currentDriver);
    if (isMoving) {
      M5.Lcd.setTextColor(GREEN);
      M5.Lcd.println("moving");
    } else {
      M5.Lcd.setTextColor(YELLOW);
      M5.Lcd.println("idle");
    }
  }

  // IP表示
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(DARKGREY);
  M5.Lcd.setCursor(0, 100);
  M5.Lcd.println(WiFi.localIP().toString());
}

// ===================== HTTP handlers =====================

// GET /status
void handleStatus() {
  bool powered = isMotorPowered();
  float bv  = powered ? getBatteryVoltage() : -1;
  int   bpct = getBatteryPercent(bv);
  String json = "{\"driver\":\"" + currentDriver
    + "\",\"moving\":"       + (isMoving ? "true" : "false")
    + ",\"motor_power\":"    + (powered  ? "true" : "false")
    + ",\"battery_v\":"      + (bv  >= 0 ? String(bv,  2) : "null")
    + ",\"battery_pct\":"    + (bpct >= 0 ? String(bpct)  : "null")
    + "}";
  server.send(200, "application/json", json);
}

// GET /acquire?driver=puchiteya
// ドライバーを取得（他が使用中なら 409）
void handleAcquire() {
  if (!server.hasArg("driver")) { server.send(400, "text/plain", "missing driver"); return; }
  String driver = server.arg("driver");
  if (!currentDriver.isEmpty() && currentDriver != driver && millis() < driverUntil) {
    server.send(409, "text/plain", "busy:" + currentDriver);
    return;
  }
  currentDriver = driver;
  driverUntil   = millis() + 30000;  // 30秒タイムアウト
  updateDisplay();
  server.send(200, "text/plain", "ok");
}

// GET /release?driver=puchiteya
void handleRelease() {
  String driver = server.hasArg("driver") ? server.arg("driver") : "";
  if (driver == currentDriver || currentDriver.isEmpty()) {
    stopMotors();
    currentDriver = "";
    updateDisplay();
  }
  server.send(200, "text/plain", "ok");
}

// GET /move?dir=forward&dist=30&speed=60
// dist: cm (時間換算, 1cm≒25ms at speed=100)
// speed: 0-100 (デフォルト60)
void handleMove() {
  if (!isMotorPowered()) { server.send(503, "text/plain", "motor power off"); return; }
  if (!server.hasArg("dir")) { server.send(400, "text/plain", "missing dir"); return; }
  String dir  = server.arg("dir");
  int dist_cm = server.hasArg("dist")  ? server.arg("dist").toInt()  : 20;
  int speed   = server.hasArg("speed") ? server.arg("speed").toInt() : defaultSpeed;
  speed = constrain(speed, 0, 100);
  int8_t spd  = (int8_t)map(speed, 0, 100, 0, 110);  // 0-110 (余裕を持たせる)
  unsigned long duration_ms = (unsigned long)(dist_cm * 25);  // 要キャリブレーション

  if      (dir == "forward")  driveForward(spd);
  else if (dir == "backward") driveBackward(spd);
  else if (dir == "right")    strafeRight(spd);
  else if (dir == "left")     strafeLeft(spd);
  else { server.send(400, "text/plain", "invalid dir"); return; }

  isMoving = true;
  driverUntil = millis() + 30000;
  updateDisplay();
  server.send(200, "text/plain", "ok");

  delay(duration_ms);
  stopMotors();
  updateDisplay();
}

// GET /rotate?dir=right&angle=90&speed=50
// angle: 度 (1度≒8ms at speed=100)
void handleRotate() {
  if (!isMotorPowered()) { server.send(503, "text/plain", "motor power off"); return; }
  if (!server.hasArg("dir")) { server.send(400, "text/plain", "missing dir"); return; }
  String dir    = server.arg("dir");
  int angle_deg = server.hasArg("angle") ? server.arg("angle").toInt() : 90;
  int speed     = server.hasArg("speed") ? server.arg("speed").toInt() : defaultSpeed;
  speed = constrain(speed, 0, 100);
  int8_t spd    = (int8_t)map(speed, 0, 100, 0, 110);
  unsigned long duration_ms = (unsigned long)(angle_deg * 8);  // 要キャリブレーション

  if      (dir == "right") rotateRight(spd);
  else if (dir == "left")  rotateLeft(spd);
  else { server.send(400, "text/plain", "invalid dir"); return; }

  isMoving = true;
  driverUntil = millis() + 30000;
  updateDisplay();
  server.send(200, "text/plain", "ok");

  delay(duration_ms);
  stopMotors();
  updateDisplay();
}

// GET /beep?freq=440&dur=300
// freq: Hz（デフォルト440）, dur: ms（デフォルト300、最大3000）
void handleBeep() {
  int freq = server.hasArg("freq") ? server.arg("freq").toInt() : 440;
  int dur  = server.hasArg("dur")  ? server.arg("dur").toInt()  : 300;
  int vol  = server.hasArg("vol")  ? server.arg("vol").toInt()  : 255;
  freq = constrain(freq, 20, 20000);
  dur  = constrain(dur,  1,  3000);
  vol  = constrain(vol,  0,  255);
  M5.Speaker.setVolume(vol);
  M5.Speaker.tone(freq, dur);
  delay(dur);
  server.send(200, "text/plain", "ok");
}

// POST /sequence  body: JSON配列 [{action,dir,dist,angle,freq,dur,speed,wait_ms}, ...]
// action: "move" | "rotate" | "beep" | "stop" | "wait"
void handleSequence() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "missing body"); return; }
  if (!isMotorPowered()) { server.send(503, "text/plain", "motor power off"); return; }

  String body = server.arg("plain");
  server.send(200, "text/plain", "ok");  // 先にレスポンスを返す（ブロッキング回避）

  // 簡易JSONパース（ArduinoJsonなしで配列要素を1個ずつ処理）
  int idx = 0;
  while (true) {
    int start = body.indexOf('{', idx);
    if (start < 0) break;
    int end = body.indexOf('}', start);
    if (end < 0) break;
    String item = body.substring(start, end + 1);
    idx = end + 1;

    // 各フィールドを抽出するラムダ的なヘルパー
    auto getStr = [&](const String& key) -> String {
      int p = item.indexOf("\"" + key + "\"");
      if (p < 0) return "";
      int q = item.indexOf(':', p);
      int s = item.indexOf('"', q);
      int e = item.indexOf('"', s + 1);
      if (s < 0 || e < 0) return "";
      return item.substring(s + 1, e);
    };
    auto getInt = [&](const String& key, int def) -> int {
      int p = item.indexOf("\"" + key + "\"");
      if (p < 0) return def;
      int q = item.indexOf(':', p);
      if (q < 0) return def;
      return item.substring(q + 1).toInt();
    };

    String action = getStr("action");

    if (action == "move") {
      String dir = getStr("dir");
      int dist   = getInt("dist",  20);
      int speed  = getInt("speed", defaultSpeed);
      speed = constrain(speed, 0, 100);
      int8_t spd = (int8_t)map(speed, 0, 100, 0, 110);
      unsigned long dur_ms = (unsigned long)(dist * 25);
      if      (dir == "forward")  driveForward(spd);
      else if (dir == "backward") driveBackward(spd);
      else if (dir == "right")    strafeRight(spd);
      else if (dir == "left")     strafeLeft(spd);
      isMoving = true; updateDisplay();
      delay(dur_ms);
      stopMotors(); updateDisplay();

    } else if (action == "rotate") {
      String dir  = getStr("dir");
      int angle   = getInt("angle", 90);
      int speed   = getInt("speed", defaultSpeed);
      speed = constrain(speed, 0, 100);
      int8_t spd  = (int8_t)map(speed, 0, 100, 0, 110);
      unsigned long dur_ms = (unsigned long)(angle * 8);
      if      (dir == "right") rotateRight(spd);
      else if (dir == "left")  rotateLeft(spd);
      isMoving = true; updateDisplay();
      delay(dur_ms);
      stopMotors(); updateDisplay();

    } else if (action == "beep") {
      int freq = getInt("freq", 440);
      int dur  = getInt("dur",  300);
      int vol  = getInt("vol",  255);
      M5.Speaker.setVolume(constrain(vol, 0, 255));
      M5.Speaker.tone(freq, dur);
      delay(dur);

    } else if (action == "stop") {
      stopMotors(); updateDisplay();

    } else if (action == "wait") {
      int ms = getInt("ms", 500);
      delay(constrain(ms, 0, 5000));
    }

    server.handleClient();  // 次のリクエストも受け付ける
  }
}

// GET /speed          → 現在のデフォルト速度を返す
// GET /speed?value=70 → デフォルト速度を変更
void handleSpeed() {
  if (server.hasArg("value")) {
    int v = server.arg("value").toInt();
    defaultSpeed = constrain(v, 0, 100);
  }
  server.send(200, "application/json", "{\"default_speed\":" + String(defaultSpeed) + "}");
}

// GET /stop
void handleStop() {
  stopMotors();
  updateDisplay();
  server.send(200, "text/plain", "ok");
}

// GET /help
void handleHelp() {
  server.send(200, "text/plain",
    "m5_rover API\n"
    "  GET /status\n"
    "  GET /acquire?driver=<name>\n"
    "  GET /release?driver=<name>\n"
    "  GET /move?dir=forward|backward|left|right&dist=<cm>&speed=<0-100>\n"
    "  GET /rotate?dir=right|left&angle=<deg>&speed=<0-100>\n"
    "  GET /stop\n"
    "  GET /speed             -> 現在のデフォルト速度\n"
    "  GET /speed?value=<0-100> -> デフォルト速度を変更\n"
  );
}

// ===================== Setup / Loop =====================

void setup() {
  M5.begin();
  M5.Speaker.setVolume(255);  // ブザー最大音量
  Wire.begin(0, 26);  // M5StickC Plus2 の Grove I2C ピン

  M5.Lcd.setRotation(3);  // 横向き
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("connecting...");

  // WiFi候補を順番に試す（DHCP→ゲートウェイ取得→.99で再接続）
  const char* ssids[] = { ssid1, ssid2, ssid3 };
  const char* passes[] = { pass1, pass2, pass3 };
  bool connected = false;
  for (int i = 0; i < 3 && !connected; i++) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.printf("WiFi %d/%d\n%s", i + 1, 3, ssids[i]);

    // 1) DHCP で接続してゲートウェイを取得
    WiFi.begin(ssids[i], passes[i]);
    for (int t = 0; t < 20; t++) {
      if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
      delay(500);
    }
    if (!connected) { WiFi.disconnect(); delay(200); continue; }

    // 2) ゲートウェイの最終オクテットを99に変えて固定IP設定
    IPAddress gw      = WiFi.gatewayIP();
    IPAddress subnet  = WiFi.subnetMask();
    IPAddress staticIP(gw[0], gw[1], gw[2], 99);
    WiFi.disconnect();
    delay(200);

    WiFi.config(staticIP, gw, subnet);
    WiFi.begin(ssids[i], passes[i]);
    connected = false;
    for (int t = 0; t < 20; t++) {
      if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
      delay(500);
    }
    if (!connected) { WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); WiFi.disconnect(); delay(200); }
  }
  if (!connected) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(RED);
    M5.Lcd.println("WiFi FAILED");
    while (true) { delay(1000); }  // 再起動待ち
  }

  MDNS.begin("rover");

  server.on("/status",  HTTP_GET, handleStatus);
  server.on("/acquire", HTTP_GET, handleAcquire);
  server.on("/release", HTTP_GET, handleRelease);
  server.on("/move",    HTTP_GET, handleMove);
  server.on("/rotate",  HTTP_GET, handleRotate);
  server.on("/stop",    HTTP_GET, handleStop);
  server.on("/beep",     HTTP_GET,  handleBeep);
  server.on("/sequence", HTTP_POST, handleSequence);
  server.on("/speed",   HTTP_GET, handleSpeed);
  server.on("/help",    HTTP_GET, handleHelp);
  server.begin();

  stopMotors();
  updateDisplay();
}

void loop() {
  server.handleClient();
  M5.update();

  // ドライバータイムアウト（30秒操作なし → 解放）
  if (!currentDriver.isEmpty() && millis() > driverUntil) {
    stopMotors();
    currentDriver = "";
    updateDisplay();
  }

  // ボタンA: 緊急停止
  if (M5.BtnA.wasPressed()) {
    stopMotors();
    currentDriver = "";
    updateDisplay();
  }

  // ボタンB: 表示更新（バッテリー残量など）
  if (M5.BtnB.wasPressed()) {
    updateDisplay();
  }
}
