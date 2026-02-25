#include "M5CoreS3.h"
#include "esp_camera.h"

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <WebSocketsServer.h>

const char* ssid = "<SSID>";
const char* pass = "<PASSWORD>";

// ===================== Files / SD =====================
File faceDir;
File iterFile;

// ===================== Web =====================
WebServer server(80);
WebSocketsServer webSocket(8080);

// ===================== UI / State =====================
unsigned long bootTime = 0;
bool showIP = true;
String ipString;

unsigned long lastWifiCheck = 0;
unsigned long lastReconnectTry = 0;
bool wifiConnected = false;

// ===================== Face slideshow =====================
unsigned long lastFaceChange = 0;
const unsigned long FACE_INTERVAL_MS = 3000;

bool faceOverride = false;
unsigned long faceOverrideUntil = 0;

// ===================== Mic / WS =====================
bool wsClientConnected = false;
uint8_t wsClientNum = 0;

bool micActive = false;              // Mic.begin() 済みか
volatile bool capturing = false;     // snapshot中か

static const int MIC_BUF = 512;
static int16_t micBuffer[MIC_BUF];
static unsigned long lastMicSend = 0;

static constexpr const size_t record_number     = 256;
static constexpr const size_t record_length     = 320;
static constexpr const size_t record_size       = record_number * record_length;
static constexpr const size_t record_samplerate = 17000;
static int16_t prev_y[record_length];
static int16_t prev_h[record_length];
static size_t rec_record_idx  = 2;
static size_t draw_record_idx = 0;
static int16_t *rec_data;

volatile bool receivingAudio = false;
volatile bool requestMicStart = false;
volatile bool requestMicStop = false;
volatile bool requestAudioEnd = false;


// ===== リングバッファ =====
#define AUDIO_BUFFER_SIZE 8192
int16_t audioBuffer[AUDIO_BUFFER_SIZE];
volatile size_t audioWriteIndex = 0;
volatile size_t audioReadIndex  = 0;
bool speakerActive = false;
static unsigned long lastAudioDataTime = 0;
volatile unsigned long micStartDelayTime = 0;

// ===================== Helpers =====================
void drawWifiStatus();
void drawIPIfNeeded();
void showNextFaceImage();

void micStartIfNeeded() {

  if (micActive) return;

  Serial.println("[MIC] starting...");

  delay(10);   // ← これ重要（WiFiタスク安定待ち）

  CoreS3.Speaker.end();   // 競合防止
  speakerActive = false;

  delay(5);

  CoreS3.Mic.begin();
  micActive = true;

  // // フラッシュは軽く1回だけ
  // int16_t dummy[128];
  // CoreS3.Mic.record(dummy, 128, 16000);

  Serial.println("[MIC] started");
}
void micStopIfNeeded() {
  if (!micActive) return;

  CoreS3.Mic.end();
  micActive = false;

  Serial.println("[MIC] end");
}

void playWavFromSD(const char* path) {

  // MICを必ず止める
  micStopIfNeeded();

  CoreS3.Speaker.begin();
  CoreS3.Speaker.setVolume(255);

  File wav = SD.open(path);
  if (!wav) {
    Serial.printf("WAV open failed: %s\n", path);
    CoreS3.Speaker.end();
    speakerActive = false;
    return;
  }

  size_t size = wav.size();
  if (size == 0) {
    wav.close();
    CoreS3.Speaker.end();
    speakerActive = false;
    return;
  }

  uint8_t* buffer = (uint8_t*)malloc(size);
  if (!buffer) {
    wav.close();
    CoreS3.Speaker.end();
    speakerActive = false;
    return;
  }

  wav.read(buffer, size);
  wav.close();

  CoreS3.Speaker.playWav(buffer, size);

  free(buffer);

  while (CoreS3.Speaker.isPlaying()) {
    delay(1);
  }

  CoreS3.Speaker.end();
  speakerActive = false;

  // WS接続中ならマイク復帰
  if (wsClientConnected && !capturing) {
    micStartIfNeeded();
  }
}

void showFaceFile(const String& filename) {
  String path = "/face/" + filename;
  File f = SD.open(path);
  if (!f) {
    Serial.printf("Face open failed: %s\n", path.c_str());
    return;
  }

  size_t size = f.size();
  if (size == 0) { f.close(); return; }

  uint8_t* buffer = (uint8_t*)malloc(size);
  if (!buffer) {
    f.close();
    Serial.printf("JPG malloc failed (%u): %s\n", (unsigned)size, path.c_str());
    return;
  }

  f.read(buffer, size);
  f.close();

  // 背景のチカチカを抑える：黒塗りしない（drawJpgが上書き）
  CoreS3.Display.drawJpg(buffer, size, 0, 0);
  free(buffer);

  drawWifiStatus();
  drawIPIfNeeded();
}

// ===================== API: list =====================
void handleFaceList() {
  File dir = SD.open("/face/");
  if (!dir) {
    server.send(500, "application/json", "{\"error\":\"no face dir\"}");
    return;
  }

  String json = "[";
  bool first = true;

  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String n = String(f.name());
      String lower = n; lower.toLowerCase();
      if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
        if (!first) json += ",";
        json += "\"" + n + "\"";
        first = false;
      }
    }
    f = dir.openNextFile();
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleSeList() {
  File dir = SD.open("/wav/");
  if (!dir) {
    server.send(500, "application/json", "{\"error\":\"no wav dir\"}");
    return;
  }

  String json = "[";
  bool first = true;

  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String n = String(f.name());
      String lower = n; lower.toLowerCase();
      if (lower.endsWith(".wav")) {
        if (!first) json += ",";
        json += "\"" + n + "\"";
        first = false;
      }
    }
    f = dir.openNextFile();
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ===================== API: play =====================
void handleFacePlay() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "missing name");
    return;
  }
  String name = server.arg("name");
  showFaceFile(name);

  faceOverride = true;
  faceOverrideUntil = millis() + 5000;

  server.send(200, "text/plain", "ok");
}

void handleSePlay() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "missing name");
    return;
  }
  String name = server.arg("name");
  String path = "/wav/" + name;

  playWavFromSD(path.c_str());
  server.send(200, "text/plain", "ok");
}

// ===================== API: snapshot =====================
void handleSnapshot() {
  capturing = true;

  // カメラ中はマイクを完全停止（I2S/DMA競合対策）
  micStopIfNeeded();

  // 最新化（捨てフレーム）
  CoreS3.Camera.get(); CoreS3.Camera.free(); delay(5);
  CoreS3.Camera.get(); CoreS3.Camera.free(); delay(5);

  if (!CoreS3.Camera.get()) {
    capturing = false;
    server.send(500, "text/plain", "Camera capture failed");
    // 戻す
    if (wsClientConnected) {
      micStartIfNeeded();      
    }
    return;
  }

  uint8_t* out_jpg = nullptr;
  size_t out_len = 0;

  if (!frame2jpg(CoreS3.Camera.fb, 60, &out_jpg, &out_len)) {
    CoreS3.Camera.free();
    capturing = false;
    server.send(500, "text/plain", "JPEG conversion failed");
    if (wsClientConnected) {
      micStartIfNeeded();      
    }
    return;
  }

  playWavFromSD("/wav/camera.wav");

  server.setContentLength(out_len);
  server.send(200, "image/jpeg", "");
  WiFiClient client = server.client();
  client.write(out_jpg, out_len);
  client.flush();

  free(out_jpg);
  CoreS3.Camera.free();

  capturing = false;

  // WSが繋がってるならマイク再開
  if (wsClientConnected) {
    micStartIfNeeded();    
  }
}

// ===================== WiFi =====================
void updateWifiState() {
  if (millis() - lastWifiCheck < 500) return;
  lastWifiCheck = millis();

  bool now = (WiFi.status() == WL_CONNECTED);

  if (now != wifiConnected) {
    wifiConnected = now;
    if (wifiConnected) {
      Serial.println("WiFi reconnected");
      ipString = WiFi.localIP().toString();
      playWavFromSD("/wav/success.wav");
    } else {
      Serial.println("WiFi disconnected");
      playWavFromSD("/wav/failed.wav");
    }
    drawWifiStatus();
  }

  if (!wifiConnected && (millis() - lastReconnectTry > 1500)) {
    lastReconnectTry = millis();
    WiFi.reconnect();
  }
}

// ===================== UI =====================
void drawWifiStatus() {
  // 右上を白で確保（黒くしない）
  const int x = 200;
  const int y = 0;
  CoreS3.Display.fillRect(x, y, 120, 18, TFT_WHITE);

  CoreS3.Display.setTextSize(1);
  if (!wifiConnected) {
    CoreS3.Display.setTextColor(TFT_RED, TFT_WHITE);
    CoreS3.Display.setCursor(x, y);
    CoreS3.Display.print("WiFi ERROR");
  }
}

void drawIPIfNeeded() {
  if (!showIP) return;

  if (millis() - bootTime < 30000) {
    // 右下を白で上書き（黒帯にしない）
    CoreS3.Display.fillRect(0, 220, 320, 20, TFT_WHITE);
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setTextColor(TFT_GREEN, TFT_WHITE);
    CoreS3.Display.setCursor(200, 220);
    CoreS3.Display.print(ipString);
  } else {
    showIP = false;
    CoreS3.Display.fillRect(0, 220, 320, 20, TFT_WHITE);
  }
}

// ===================== Face slideshow =====================
void showNextFaceImage() {
  if (!faceDir) return;

  iterFile = faceDir.openNextFile();
  if (!iterFile) {
    faceDir.rewindDirectory();
    iterFile = faceDir.openNextFile();
  }
  if (!iterFile) return;

  if (iterFile.isDirectory()) { iterFile.close(); return; }

  String name = String(iterFile.name());
  String lower = name; lower.toLowerCase();
  if (!(lower.endsWith(".jpg") || lower.endsWith(".jpeg"))) {
    iterFile.close();
    return;
  }

  size_t size = iterFile.size();
  if (size == 0) { iterFile.close(); return; }

  uint8_t* buffer = (uint8_t*)malloc(size);
  if (!buffer) {
    Serial.printf("JPG malloc failed (%u): %s\n", (unsigned)size, name.c_str());
    iterFile.close();
    return;
  }

  iterFile.read(buffer, size);
  iterFile.close();

  CoreS3.Display.drawJpg(buffer, size, 0, 0);
  free(buffer);

  drawWifiStatus();
  drawIPIfNeeded();
}

// ===================== WebSocket =====================
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {

  switch (type) {

    case WStype_CONNECTED:
      wsClientConnected = true;
      wsClientNum = num;
      requestMicStart = true;
      micStartDelayTime = millis();
      break;

    case WStype_DISCONNECTED:
      wsClientConnected = false;
      requestMicStop = true;
      break;

    case WStype_BIN: {
      receivingAudio = true;

      if (length == 0) return;

      size_t bufferSize = AUDIO_BUFFER_SIZE;

      int samples = length / 2;

      if (samples > 1024) {
        samples = 1024;  // 最大制限
      }

      int16_t* pcm = (int16_t*)payload;

      for (int i = 0; i < samples; i++) {

        size_t next = (audioWriteIndex + 1) % bufferSize;

        if (next == audioReadIndex) {
          audioReadIndex = (audioReadIndex + 256) % bufferSize;
        }

        audioBuffer[audioWriteIndex] = pcm[i];
        audioWriteIndex = next;
      }

      break;
    }

    case WStype_TEXT:

      if (strcmp((char*)payload, "END") == 0) {
          receivingAudio = false;
          requestAudioEnd = true;
      }
      break;

    default:
      break;
    }
}

// ===================== Setup / Loop =====================
void setup() {
  auto cfg = M5.config();
  CoreS3.begin(cfg);
  Serial.begin(115200);



  // Display
  CoreS3.Display.fillScreen(TFT_WHITE);
  CoreS3.Display.setTextColor(TFT_CYAN, TFT_WHITE);
  CoreS3.Display.setCursor(0, 0);
  CoreS3.Display.println("Booting...");

  // SD
  if (!SD.begin(GPIO_NUM_4)) {
    Serial.println("SD Init Failed");
  } else {
    Serial.println("SD Init OK");
  }

  faceDir = SD.open("/face/");
  if (!faceDir) {
    Serial.println("Face dir open failed: /face/");
  }

  // Camera
  if (!CoreS3.Camera.begin()) {
    Serial.println("Camera Init Fail");
    CoreS3.Display.fillScreen(TFT_WHITE);
    CoreS3.Display.setTextColor(TFT_RED, TFT_WHITE);
    CoreS3.Display.setCursor(0, 0);
    CoreS3.Display.println("Camera Init Fail");
    while (1) delay(1000);
  }
  Serial.println("Camera Init Success");
  CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_QVGA);


  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    ipString = WiFi.localIP().toString();
    Serial.printf("WiFi connected: %s\n", ipString.c_str());
  } else {
    ipString = "0.0.0.0";
    Serial.println("WiFi NOT connected (will retry)");
  }

  // HTTP routes
  server.on("/snapshot", HTTP_GET, handleSnapshot);
  server.on("/face_list", HTTP_GET, handleFaceList);
  server.on("/se_list", HTTP_GET, handleSeList);
  server.on("/face_play", HTTP_GET, handleFacePlay);
  server.on("/se_play", HTTP_GET, handleSePlay);
  server.begin();

  // WebSocket
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  bootTime = millis();
  showIP = true;

  // first face
  showNextFaceImage();
  drawWifiStatus();
  drawIPIfNeeded();
}

void loop() {
  server.handleClient();
  webSocket.loop();

  updateWifiState();

  // Face slideshow (override中は止める)
  if (!faceOverride) {
    if (millis() - lastFaceChange > FACE_INTERVAL_MS) {
      lastFaceChange = millis();
      showNextFaceImage();
    }
  } else {
    if (millis() > faceOverrideUntil) {
      faceOverride = false;
    }
  }

  // Mic streaming (WS接続時のみ。camera中はOFF)
  if (requestAudioEnd) {

    requestAudioEnd = false;
    CoreS3.Speaker.end();
    speakerActive = false;
    Serial.println("Playback finished");
    if (wsClientConnected && !capturing) {
      micStartIfNeeded();
    }
  }

  if (requestMicStart &&
      millis() - micStartDelayTime > 200) {
      requestMicStart = false;
      if (!capturing) {
          micStartIfNeeded();
      }
  }
  if (requestMicStop) {
    requestMicStop = false;
    if (!speakerActive) {
      micStopIfNeeded();
    }
  }

  if (wsClientConnected && micActive && !capturing && !speakerActive) {
    if (millis() - lastMicSend > 30) {
      lastMicSend = millis();
      if (CoreS3.Mic.record(micBuffer, MIC_BUF, 16000)) {
        webSocket.sendBIN(wsClientNum,
                          (uint8_t*)micBuffer,
                          MIC_BUF * sizeof(int16_t));
      }
    }
  }

if (audioReadIndex != audioWriteIndex) {
    if (!speakerActive) {
        micStopIfNeeded();
        CoreS3.Speaker.begin();
        CoreS3.Speaker.setVolume(255);
        speakerActive = true;
    }
    static int16_t chunk[1024];
    int count = 0;
    while (audioReadIndex != audioWriteIndex && count < 1024) {
        chunk[count++] = audioBuffer[audioReadIndex];
        audioReadIndex = (audioReadIndex + 1) % AUDIO_BUFFER_SIZE;
    }
    CoreS3.Speaker.playRaw(chunk, count, 16000, false, 1, 0);
    lastAudioDataTime = millis();
}

  if (speakerActive &&
      audioReadIndex == audioWriteIndex &&
      millis() - lastAudioDataTime > 100) {
      CoreS3.Speaker.end();
      speakerActive = false;
      Serial.println("Playback finished");
      if (wsClientConnected && !capturing) {
        micStartIfNeeded();
      }
  }

  drawIPIfNeeded();

}