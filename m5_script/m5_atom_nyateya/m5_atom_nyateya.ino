// m5_atom_nyateya.ino — にゃてゃ (カナリアイエロー #fff262)
// AtomS3 + Atomic Echo Base + Atomic Battery Base
// Based on cubic9com/AtomNyanWithImpactDetection (MIT License)
//
// 遊び方:
//   軽くタップ      → 喜ぶ（にゃ～）
//   強く叩く        → 怒る（にゃ！）
//   傾ける          → 顔が傾く
//   逆さにする      → びっくり顔
//   大きな声をかける → 喜ぶ
//   3分放置         → 眠る（タップで起きる）

#include <M5Unified.h>
#include <Avatar.h>
#include "fft.hpp"
#include "sound_happy.h"
#include "sound_angry.h"
#include <cinttypes>

// ===== にゃてゃ の色 =====
#define COLOR_NEUTRAL_BG  ((uint16_t)0xFF8C)  // #fff262 カナリアイエロー
#define COLOR_NEUTRAL_FG  TFT_BLACK
#define COLOR_HAPPY_BG    TFT_YELLOW
#define COLOR_HAPPY_FG    TFT_BLACK
#define COLOR_ANGRY_BG    TFT_ORANGE
#define COLOR_ANGRY_FG    TFT_WHITE

// ===== 衝撃検知 =====
static const float    THR_HAPPY       = 0.25f;   // 0.5^2  軽いタップ
static const float    THR_ANGRY       = 121.0f;  // 11.0^2 強い衝撃
static const float    THR_WAKE        = 0.05f;   // 眠りから覚める最小タップ
static const uint32_t COOLDOWN        = 300;

// ===== 状態の持続時間 =====
static const uint32_t IMPACT_DURATION      = 1500;
static const uint32_t VOICE_HAPPY_DURATION = 1500;
static const uint32_t SLEEP_TIMEOUT        = 3UL * 60 * 1000;  // 3分
static const uint32_t VOICE_HAPPY_COOLDOWN = 5000;
static const float    VOICE_HAPPY_THR      = 0.75f;  // 声かけで喜ぶ音量閾値
static const float    UPSIDE_THR           = -0.5f;  // 逆さ判定(az)

// ===== リップシンク =====
static const float    LIPSYNC_MAX_INIT = 10.0f;
static const uint8_t  LIPSYNC_SHIFT   = 11;
static const size_t   WAVE_SIZE       = 256 * 2;
static const uint32_t RECORD_RATE     = 16000;
static const uint8_t  VOL             = 128;

// ===== 状態 =====
enum State {
  ST_NEUTRAL,
  ST_HAPPY_IMPACT,   // タップ → 喜び
  ST_ANGRY_IMPACT,   // 強打 → 怒り
  ST_VOICE_HAPPY,    // 声かけ → 喜び
  ST_UPSIDE,         // 逆さ → びっくり
  ST_SLEEPY          // 放置 → 眠り
};

using namespace m5avatar;
Avatar        avatar;
ColorPalette* cps[4];  // 0=neutral 1=happy 2=angry 3=muted(grey)

State    currentState       = ST_NEUTRAL;
uint32_t stateStartTime     = 0;
uint32_t lastActiveTime     = 0;
uint32_t lastVoiceHappyTime = 0;
uint32_t lastImpactTime     = 0;

bool     isMuted         = true;
float    prev_accel_sq    = 1.0f;
int16_t* rec_data         = nullptr;
fft_t    fft;
float    lipsync_max      = LIPSYNC_MAX_INIT;
uint32_t last_lipsync_max_ms = 0;
bool     imu_ok           = false;

// ===== ヘルパー: 音再生 =====
void playSound(const uint8_t* data, size_t size) {
  if (isMuted) return;  // スピーカーミュート中は鳴らさない
  M5.Speaker.end();
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(VOL);
  M5.Speaker.playRaw(data, size, 16000, false, 1, 0, true);
}

// ===== ヘルパー: 状態遷移 =====
void setState(State next, Expression expr, uint8_t paletteIdx) {
  currentState  = next;
  stateStartTime = lgfx::v1::millis();
  if (next != ST_SLEEPY && next != ST_UPSIDE) {
    lastActiveTime = lgfx::v1::millis();
  }
  // NEUTRAL時はミュート状態に応じてパレット切替
  uint8_t idx = (paletteIdx == 0 && isMuted) ? 3 : paletteIdx;
  avatar.setColorPalette(*cps[idx]);
  avatar.setExpression(expr);
}

void wakeFromSleep() {
  M5.Speaker.end();
  M5.Mic.begin();
  setState(ST_NEUTRAL, Expression::Neutral, 0);
}

// ===== 衝撃検知 =====
void handleImpact() {
  if (!imu_ok) return;

  M5.Imu.update();
  auto d = M5.Imu.getImuData();
  float asq = d.accel.x*d.accel.x + d.accel.y*d.accel.y + d.accel.z*d.accel.z;
  float delta = asq - prev_accel_sq;
  float dsq = delta * delta;
  prev_accel_sq = asq;

  uint32_t now = lgfx::v1::millis();
  if ((now - lastImpactTime) <= COOLDOWN) return;

  // 眠っているとき: 小さいタップでも起きる
  if (currentState == ST_SLEEPY) {
    if (dsq >= THR_WAKE) {
      lastImpactTime = now;
      wakeFromSleep();
      setState(ST_HAPPY_IMPACT, Expression::Happy, 1);
      avatar.setMouthOpenRatio(0.5f);
      playSound(sound_happy, sizeof(sound_happy));
    }
    return;
  }

  // 逆さのとき: 衝撃無視（az で別途管理）
  if (currentState == ST_UPSIDE) return;

  if (dsq >= THR_ANGRY) {
    lastImpactTime = now;
    setState(ST_ANGRY_IMPACT, Expression::Angry, 2);
    avatar.setMouthOpenRatio(1.0f);
    playSound(sound_angry, sizeof(sound_angry));

  } else if (dsq >= THR_HAPPY) {
    lastImpactTime = now;
    setState(ST_HAPPY_IMPACT, Expression::Happy, 1);
    avatar.setMouthOpenRatio(0.8f);
    playSound(sound_happy, sizeof(sound_happy));
  }
}

// ===== 逆さ検知 =====
void handleUpside() {
  if (!imu_ok) return;
  if (currentState == ST_HAPPY_IMPACT || currentState == ST_ANGRY_IMPACT) return;

  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);

  if (az < UPSIDE_THR && currentState != ST_UPSIDE) {
    setState(ST_UPSIDE, Expression::Doubt, 0);

  } else if (az > 0.2f && currentState == ST_UPSIDE) {
    setState(ST_NEUTRAL, Expression::Neutral, 0);
  }
}

// ===== 状態タイムアウト管理 =====
void handleStateTimeout() {
  uint32_t now = lgfx::v1::millis();

  // 表情の持続時間が過ぎたら通常に戻す
  if (currentState == ST_HAPPY_IMPACT || currentState == ST_ANGRY_IMPACT ||
      currentState == ST_VOICE_HAPPY) {
    if ((now - stateStartTime) > IMPACT_DURATION) {
      M5.Speaker.end();
      M5.Mic.begin();
      setState(ST_NEUTRAL, Expression::Neutral, 0);
    }
    return;
  }

  // 放置検知 → 眠る
  if (currentState == ST_NEUTRAL) {
    if ((now - lastActiveTime) > SLEEP_TIMEOUT) {
      M5.Speaker.end();
      setState(ST_SLEEPY, Expression::Sleepy, 0);
      avatar.setMouthOpenRatio(0.0f);
    }
  }
}

// ===== リップシンク & 傾き & 声かけ検知 =====
void lipsync() {
  // 音再生中・眠り中はスキップ（でも逆さは通す）
  if (currentState == ST_HAPPY_IMPACT || currentState == ST_ANGRY_IMPACT) return;

  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);

  // --- 傾き → 回転 ---
  float tilt_rotation = ax * 25.0f;  // ±25度

  // --- 眠り中: 回転のみ更新して終了 ---
  if (currentState == ST_SLEEPY || currentState == ST_UPSIDE) {
    avatar.setRotation(tilt_rotation);
    return;
  }

  // --- マイク録音 ---
  if (!M5.Mic.record(rec_data, WAVE_SIZE, RECORD_RATE)) {
    avatar.setRotation(tilt_rotation);
    return;
  }

  fft.exec(rec_data);
  uint64_t level = 0;
  for (size_t bx = 5; bx <= 60; bx++) level += (uint64_t)abs((int32_t)fft.get(bx));

  float ratio = (float)(level >> LIPSYNC_SHIFT) / lipsync_max;

  if (ratio <= 0.01f) {
    ratio = 0.0f;
    if ((lgfx::v1::millis() - last_lipsync_max_ms) > 500) {
      last_lipsync_max_ms = lgfx::v1::millis();
      lipsync_max = LIPSYNC_MAX_INIT;
    }
  } else {
    if (ratio > 1.5f) lipsync_max += 10.0f;
    if (ratio > 1.3f) ratio = 1.3f;
    last_lipsync_max_ms = lgfx::v1::millis();
    lastActiveTime = lgfx::v1::millis();  // 声があれば放置リセット

    // 大きな声 → 喜ぶ
    uint32_t now = lgfx::v1::millis();
    if (ratio > VOICE_HAPPY_THR && currentState == ST_NEUTRAL &&
        (now - lastVoiceHappyTime) > VOICE_HAPPY_COOLDOWN) {
      lastVoiceHappyTime = now;
      setState(ST_VOICE_HAPPY, Expression::Happy, 1);
      avatar.setMouthOpenRatio(0.7f);
      playSound(sound_happy, sizeof(sound_happy));
      return;
    }
  }

  // 傾き + 話し中のゆらぎを合成して回転
  float wobble = (ratio > 0.05f) ? (float)random(-2, 2) * 5.0f * ratio : 0.0f;
  avatar.setRotation(tilt_rotation + wobble);
  avatar.setMouthOpenRatio(ratio);
}

// ===== setup =====
void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = false;
  cfg.external_speaker.atomic_echo = true;
  M5.begin(cfg);

  M5.Log.setLogLevel(m5::log_target_display, ESP_LOG_NONE);
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_INFO);
  M5.Log.setEnableColor(m5::log_target_serial, false);
  M5_LOGI("start: nyateya");

  auto mic_cfg = M5.Mic.config();
  mic_cfg.sample_rate = 16000;
  mic_cfg.pin_ws      = 1;
  mic_cfg.pin_data_in = 2;
  M5.Mic.config(mic_cfg);

  rec_data = (int16_t*)heap_caps_malloc(WAVE_SIZE * sizeof(int16_t), MALLOC_CAP_8BIT);
  memset(rec_data, 0, WAVE_SIZE * sizeof(int16_t));
  M5.Mic.begin();
  M5.Speaker.end();  // デフォルトスピーカーミュート

  imu_ok = M5.Imu.isEnabled();
  M5_LOGI("IMU: %d", imu_ok);

  M5.Display.setRotation(0);
  avatar.setScale(0.47f);
  avatar.setPosition(-45, -95);
  avatar.init(1);

  for (int i = 0; i < 3; i++) cps[i] = new ColorPalette();
  cps[0]->set(COLOR_PRIMARY,    COLOR_NEUTRAL_FG);
  cps[0]->set(COLOR_BACKGROUND, COLOR_NEUTRAL_BG);
  cps[1]->set(COLOR_PRIMARY,    COLOR_HAPPY_FG);
  cps[1]->set(COLOR_BACKGROUND, COLOR_HAPPY_BG);
  cps[2]->set(COLOR_PRIMARY,    COLOR_ANGRY_FG);
  cps[2]->set(COLOR_BACKGROUND, COLOR_ANGRY_BG);

  cps[3] = new ColorPalette();
  cps[3]->set(COLOR_PRIMARY,    TFT_DARKGREY);
  cps[3]->set(COLOR_BACKGROUND, TFT_LIGHTGREY);

  // ミュート状態で起動
  avatar.setColorPalette(*cps[3]);
  lastActiveTime = lgfx::v1::millis();
}

// ===== loop =====
void loop() {
  M5.update();

  handleImpact();
  handleUpside();
  handleStateTimeout();
  lipsync();

  // ボタンA: 長押し=ミュート切替 / 短押し=起き上がり
  static uint32_t btnPressStart = 0;
  static bool btnLongHandled = false;
  if (M5.BtnA.isPressed()) {
    if (btnPressStart == 0) btnPressStart = lgfx::v1::millis();
    if (!btnLongHandled && (lgfx::v1::millis() - btnPressStart) > 700) {
      isMuted = !isMuted;
      // NEUTRAL中なら即座にパレット切替
      if (currentState == ST_NEUTRAL || currentState == ST_SLEEPY) {
        avatar.setColorPalette(*cps[isMuted ? 3 : 0]);
      }
      lastActiveTime = lgfx::v1::millis();
      btnLongHandled = true;
    }
  } else {
    if (btnPressStart > 0 && !btnLongHandled) {
      if (currentState == ST_SLEEPY) wakeFromSleep();
      lastActiveTime = lgfx::v1::millis();
    }
    btnPressStart = 0;
    btnLongHandled = false;
  }

  if (M5.BtnPWR.wasClicked()) esp_restart();



  lgfx::v1::delay(1);
}
