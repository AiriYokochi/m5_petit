# m5_petit

M5CoreS3 を使ったロボット顔デバイス。

📷 カメラ / 🎤 マイク / 🖼 顔表示 / 🔊 音声再生 / 📡 センサー / 👆 タッチ

---

## できること

- HTTP でカメラ画像を取得
- WebSocket でコマンド送信（視線・表情・音声・スリープ等）
- WebSocket でマイク音声をリアルタイム配信（MIC_STARTで開始）
- WebSocket でセンサー・タッチイベントを受信
- SDカードの顔画像スライドショー
- SDカードのWAVファイル再生

---

## 必要なハードウェア

- M5CoreS3
- microSDカード（FAT32）
- WiFi環境（テザリング可）

> ⚠️ iPhoneテザリングでは WebSocket 通信が失敗することがあります

---

## 導入手順（Arduino IDE）

### 1. ボード追加

ファイル > 環境設定 > 追加のボードマネージャURL に追加：

```
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

ツール > ボード > ボードマネージャ → `M5Stack` をインストール

ボード設定：

![ボード設定](img/image.png)

### 2. ライブラリインストール

- M5CoreS3 (1.0.1)
- M5Stack (0.4.6)
- M5Unified (0.2.13)
- SD (1.3.0)
- [WebSockets by Links2004](https://github.com/Links2004/arduinoWebSockets)（ZIPダウンロードして追加）

### 3. WiFiと固定IP設定

`m5_script/m5_petit.ino` を開いて書き換える：

```cpp
const char* ssid = "あなたのSSID";
const char* pass = "パスワード";

// 固定IP（環境に合わせて変更）
IPAddress local_IP(10, 42, 138, 100);
IPAddress gateway(10, 42, 138, 1);
```

### 4. 書き込み

Arduino IDE で M5CoreS3 に書き込む。

---

## SDカード準備

`./sd.zip` を解凍してSDカード（FAT32）のルートに配置：

```
/
├── face/   ← 顔画像 (.jpg)
└── wav/    ← 効果音 (.wav) ※ 16bit PCM, Mono, 16000Hz
```

WAVが聞こえない場合は Audacity でモノラル変換する。

---

## IPアドレス確認

起動後、画面右下またはシリアルモニタ（115200baud）で確認。

---

## HTTP API

| エンドポイント | 説明 |
|---|---|
| `GET /help` | API一覧 |
| `GET /snapshot` | カメラ撮影（JPEG） |
| `GET /face_list` | 顔画像ファイル一覧（JSON） |
| `GET /face_play?name=xxx.jpg` | 顔画像を5秒表示 |
| `GET /se_list` | 効果音ファイル一覧（JSON） |
| `GET /getvolume` | 現在の音量取得（0〜100） |

---

## WebSocket API

接続先：`ws://<IPアドレス>:8080`

### クライアント → M5（コマンド）

テキストメッセージで送信。

| コマンド | 説明 |
|---|---|
| `LOOK x y` | 視線移動。x/y: -100〜100。5秒後に正面へ戻る |
| `LOOK x y mouth` | 視線＋口の開き（mouth: 0〜100） |
| `BLINK l r` | ウィンク。l/r: 0か1。0.8秒後に戻る |
| `MODE draw` | 描画モード（目・口をリアルタイム描画） |
| `MODE jpeg` | スライドショーモード（SDのJPEGを3秒ごとに表示） |
| `PLAY filename.wav` | WAVファイルを再生 |
| `VOL value` | 音量設定（0〜100） |
| `ICON love` | ハートアイコンを3秒表示 |
| `ICON cry` | 涙アイコンを3秒表示 |
| `MIC_START` | マイクをオンにして音声ストリーム開始 |
| `MIC_STOP` | マイクをオフ |
| `SLEEP` | スリープモード（3回タッチか明るさで復帰） |
| `WAKE` | スリープから復帰 |

音声を送る場合は PCM バイナリ（int16, Mono, 16000Hz）を送り、最後に `END` を送る。

### M5 → クライアント（イベント）

#### センサーデータ（250ms周期）

```json
{
  "event": "sensors",
  "ambient": 342,
  "proximity": 120,
  "ax": 0.01, "ay": -0.98, "az": 0.12,
  "gx": 0.00, "gy": 0.02, "gz": -0.01,
  "battery": 83.4
}
```

#### タッチイベント

```json
{
  "event": "touch",
  "x": 120,
  "y": 200
}
```

#### マイク音声（MIC_START後）

バイナリ（int16, Mono, 16000Hz, 約30ms毎）

---

## 注意事項

- **マイクとスピーカーは同時使用不可**（I2S/DMA競合）。再生中はマイク停止、録音中はスピーカー停止。
- **マイクは接続時に自動起動しない**。`MIC_START` コマンドで明示的に起動すること。

---

## テスト用HTML

`test.html` をブラウザで開くと、WS接続・マイク音声受信・WAV送信をブラウザから試せる。

![テスト画面](img/image2.png)
