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

## 導入手順（PlatformIO）

### 1. PlatformIO インストール

```bash
pip install platformio
# または VS Code の PlatformIO IDE 拡張をインストール
```

### 2. WiFi設定

`m5_script/credentials.example.h` をコピーして `m5_script/credentials.h` を作成：

```bash
cp m5_script/credentials.example.h m5_script/credentials.h
```

`credentials.h` を編集して実際の値を入れる：

```cpp
const char* ssid1 = "スマホテザリングのSSID";
const char* pass1 = "パスワード";
const char* ssid2 = "家のWiFiのSSID";
const char* pass2 = "パスワード";
```

ssid1（スマホ）に3回接続を試み、失敗したらssid2（家WiFi/DHCP）にフォールバック。切断時の再接続も同様。

固定IPはソース内の `local_IP` / `gateway` を環境に合わせて変更。

### 3. ビルド＆書き込み

2つの環境（`puchiko` / `puchiteya`）があり、それぞれ別の .ino をビルドする。
`select_source.py` が `m5_script/m5_petit_<env>.ino` を自動で `src/` にコピーする。

```bash
# ビルドのみ
pio run -e puchiko
pio run -e puchiteya

# ビルド＆書き込み
pio run -e puchiko -t upload
pio run -e puchiteya -t upload

# シリアルモニタ
pio device monitor
```

> ⚠️ 2つの環境を同時にビルド（`pio run`）するとメモリ不足で落ちることがあります。1つずつビルドしてください。

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

mDNS対応のため、IPアドレスの代わりにホスト名でもアクセス可能：

- ぷちこ: `http://puchiko.local/`
- ぷちてゃ: `http://puchiteya.local/`

> ホスト名は `MDNS_HOSTNAME` で設定。WebSocketも `ws://puchiko.local:8080` で接続可能。

---

## HTTP API

| エンドポイント | 説明 |
|---|---|
| `GET /help` | API一覧 |
| `GET /snapshot` | カメラ撮影（JPEG） |
| `GET /face_list` | 顔画像ファイル一覧（JSON） |
| `GET /face_play?name=xxx.jpg` | 顔画像を5秒表示 |
| `GET /face_draw_mode` | 描画モードへ切替 |
| `GET /face_play_mode` | スライドショーモードへ切替 |
| `GET /set_face_draw?eyeX=&eyeY=&mouth=` | 視線・口の制御（5秒後に戻る） |
| `GET /blink?left=true&right=false` | ウィンク |
| `GET /se_list` | 効果音ファイル一覧（JSON） |
| `GET /se_play?name=xxx.wav` | 効果音再生 |
| `GET /setvolume?value=0~100` | 音量変更 |
| `GET /getvolume` | 現在の音量取得（0〜100） |
| `GET /icon_list` | アイコン一覧 |
| `GET /icon_play?name=love\|cry` | アイコン表示（3秒） |
| `GET /set_color?color=RRGGBB` | 顔の色変更 |
| `GET /status` | 状態取得（is_sleeping, power_save） |
| `GET /setbrightness?value=0~100` | 画面輝度変更 |
| `GET /getbrightness` | 画面輝度取得 |
| `GET /sensors` | センサーデータ取得（IMU/照度/近接/バッテリー/RSSI） |
| `GET /powersave?value=true\|false` | 省電力モード切替（輝度制限+描画10fps） |
| `GET /getpowersave` | 省電力モード状態取得 |
| `GET /sleep` | スリープモード |
| `GET /wake` | スリープから復帰 |
| `POST /upload_wav` | WAVファイルをSDにアップロード（multipart/form-data） |
| `POST /upload_face` | 顔画像(JPG)をSDにアップロード（multipart/form-data） |

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
| `COLOR RRGGBB` | 顔の色変更 |
| `BRIGHTNESS value` | 画面輝度設定（0〜100） |
| `POWERSAVE ON` | 省電力モードON |
| `POWERSAVE OFF` | 省電力モードOFF |
| `SLEEP` | スリープモード（3回タッチで復帰） |
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
  "battery": 83.4,
  "voltage": 3.982,
  "rssi": -45
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

## 省電力モード

`/powersave?value=true` またはWSで `POWERSAVE ON` で有効化。

- 画面輝度を40以下に制限
- 顔の描画を30fps→10fpsに削減
- バッテリー持ち改善（おでかけ時に推奨）

## 低バッテリー自動スリープ

バッテリー残量が10%以下になると自動的にスリープモードに入る。完全放電を防止。

> バッテリー0%は充電中を意味するため、自動スリープの対象外。

## ファイルアップロード

SDカードを抜き差しせずにHTTPでファイルを追加できる。

```bash
# WAVファイルをアップロード
curl -F "file=@hello.wav" http://<IP>/upload_wav

# 顔画像をアップロード
curl -F "file=@smile.jpg" http://<IP>/upload_face
```

## タッチ反応

タッチすると脊髄反射で目をつぶる（まばたき）。タッチイベントはWSでも配信される。

## 注意事項

- **マイクとスピーカーは同時使用不可**（I2S/DMA競合）。再生中はマイク停止、録音中はスピーカー停止。
- **マイクは接続時に自動起動しない**。`MIC_START` コマンドで明示的に起動すること。

---

## テスト用HTML

`test.html` をブラウザで開くと、WS接続・マイク音声受信・WAV送信をブラウザから試せる。

![テスト画面](img/image2.png)
