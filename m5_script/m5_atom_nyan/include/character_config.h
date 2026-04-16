#pragma once

// キャラクターごとの色設定
// RGB565フォーマット: ((r>>3)<<11)|((g>>2)<<5)|(b>>3)

#if defined(PUCHITEYA)
  // ぷちてゃ: カナリアイエロー #fff262 (R=255,G=242,B=98)
  #define CHAR_NAME         "puchiteya"
  #define COLOR_CHAR        ((uint16_t)0xFF8C)  // #fff262
  #define COLOR_CHAR_HAPPY  TFT_YELLOW           // 明るい黄色
  #define COLOR_CHAR_ANGRY  TFT_ORANGE           // オレンジ
  #define COLOR_CHAR_DARK   ((uint16_t)0x3160)   // 深い黄緑 (#303003)
  #define FIRST_PALETTE     0

#elif defined(PUCHIKO)
  // ぷちこ: ラベンダー #cab8d9 (R=202,G=184,B=217)
  #define CHAR_NAME         "puchiko"
  #define COLOR_CHAR        ((uint16_t)0xCDDB)  // #cab8d9
  #define COLOR_CHAR_HAPPY  ((uint16_t)0xFFDF)  // ほんのりピンク白 #FFFCFF
  #define COLOR_CHAR_ANGRY  ((uint16_t)0x8010)  // 深い紫 #800080
  #define COLOR_CHAR_DARK   ((uint16_t)0x2009)  // 濃い紫 #200049
  #define FIRST_PALETTE     0

#elif defined(PUCHIRU)
  // ぷちる: ターコイズ #00afcc (R=0,G=175,B=204)
  #define CHAR_NAME         "puchiru"
  #define COLOR_CHAR        ((uint16_t)0x0579)  // #00afcc
  #define COLOR_CHAR_HAPPY  TFT_CYAN             // 明るい水色
  #define COLOR_CHAR_ANGRY  ((uint16_t)0x0410)  // 深いティール #004080
  #define COLOR_CHAR_DARK   ((uint16_t)0x0009)  // 濃い紺 #000049
  #define FIRST_PALETTE     0

#else
  #error "キャラクターを指定してください: -D PUCHITEYA / PUCHIKO / PUCHIRU"
#endif
