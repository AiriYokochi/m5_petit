#pragma once

#include <M5Unified.h>
#include <Avatar.h>
#include "character_config.h"

using namespace m5avatar;

// パレットインデックス定数
const uint8_t PALETTE_NEUTRAL = 0;  // 通常 (キャラ固有色)
const uint8_t PALETTE_HAPPY   = 1;  // 喜び
const uint8_t PALETTE_ANGRY   = 2;  // 怒り

// 外部変数宣言
extern uint32_t impact_detected_time;
extern uint32_t last_impact_detect_time;
extern bool     is_playing_sound;
extern float    prev_accel_sq;
extern uint8_t  palette_index;

// パレットの初期化（キャラクターの色で設定）
void initializeEmotionPalettes(ColorPalette** cps);

// 衝撃検知と表情変化を処理
void handleImpactDetection(Avatar& avatar, ColorPalette** cps);

// 表情の持続時間管理と通常状態への復帰
void handleExpressionDuration(Avatar& avatar, ColorPalette** cps);
