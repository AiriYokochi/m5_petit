# m5_petit
M5CoreS3 を使った
📷 カメラ + 🎤 音声ストリーミング + 🖼 顔表示 + 🔊 SE再生
を行うプロジェクトです。

---

### 📦 できること
- 📸 HTTP snapshot配信
- 🖼 SDカード顔スライド表示
- 🔊 WAV再生
- 🎤 マイク音声をWebSocketでリアルタイム配信
- 📡 WiFi接続監視
- 🖥 PCブラウザで音声再生

---

### 🧰 必要なハードウェア
- M5CoreS3
- microSDカード(FAT32)
- WiFi環境（テザリング可）
- PC（Chrome推奨）
⚠️ iPhoneテザリングではWebSocket通信が失敗することがあります

### 🔧 導入手順（Arduino IDE）

①ボード追加
Arduino IDE →
ファイル > 環境設定

追加のボードマネージャURLに以下を追加：
```
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```
その後：

ツール > ボード > ボードマネージャ
→ M5Stack をインストール

---

② 使用ボード
M5CoreS3

ボード設定は以下：
![alt text](img/image.png)

---
③ライブラリインストール

- M5CoreS3(1.0.1)
- M5Stack(0.4.6)
- M5Unified(0.2.13)
- SD(1.3.0)
  
[WebSockets](https://github.com/Links2004/arduinoWebSockets)
以下からZIPダウンロード：
https://github.com/Links2004/arduinoWebSockets

---

④ WiFi設定

m5_script/m5_petit.ino を開き、以下を書き換える：
```cpp
const char* ssid = "あなたのSSID";
const char* pass = "パスワード";
```

---

⑤IDE書き込み

---

### 💾 SDカード準備
`./sd.zip`を解凍して、SDカード(FAT32フォーマット)のルートに入れる

```
/
    face/~~~~.png
    wav/~~~.wav
```

WAV形式は
16bit PCM、Mono、16000Hz
聞こえない場合はAudacityでMonoralに変換すること

---

🌐 IPアドレス確認

起動後、

画面右下

またはシリアルモニタ（115200）

でIPを確認。

---

### 📡 HTTP API


ブラウザでアクセス
- `http://<IPアドレス>/help`
  - ヘルプ
- `http://<IPアドレス>/snapshot`
  - jpgがみれる
- `http://<IPアドレス>/se_list`
  - 現在のSEリストがJSONで帰ってくる
- `http://<IPアドレス>/face_list`
  - 現在の顔画像がJSONで帰ってくる
- `http://<IPアドレス>/se_play?name=XXXX.wav`
  - SEを鳴らす
- `http://<IPアドレス>/face_play?name=XXXX.jpg`
  - 顔画像を5秒間表示させる
- `http://<IPアドレス>/setvolume?value=XX`
  - 0~100で音量をセットする
- `http://<IPアドレス>/getvolume`
  - 現在音量を取得
- `http://<IPアドレス>/face_draw_mode`
   - 自由に顔が動くモード
- `http://<IPアドレス>/face_play_mode`
  - スライドショーモード
- `http://<IPアドレス>/set_face_draw?eyeX=-100~100&eyeY=-100~100`
  - 視線を移動させる
  
### 🖥 PC側テストHTML
`ws://<IPアドレス>:8080`

保存してブラウザで開く

```html
<!DOCTYPE html>
<html>
<body>

<button onclick="start()">Start</button>
<br><br>
<input type="file" id="wavFile" accept=".wav">
<button onclick="sendWav()">Send WAV to CoreS3</button>

<script>
let ws;
let audioCtx;
let processor;

let ringBuffer = new Float32Array(48000);
let writePos = 0;
let readPos = 0;

function start() {

  audioCtx = new (window.AudioContext || window.webkitAudioContext)({
    sampleRate: 16000
  });

  processor = audioCtx.createScriptProcessor(1024, 1, 1);

  processor.onaudioprocess = function(e) {

    const output = e.outputBuffer.getChannelData(0);

    for (let i = 0; i < output.length; i++) {

      if (readPos !== writePos) {
        output[i] = ringBuffer[readPos];
        readPos = (readPos + 1) % ringBuffer.length;
      } else {
        output[i] = 0;
      }
    }
  };

  processor.connect(audioCtx.destination);

  ws = new WebSocket("ws://<IPアドレス>:8080");
  ws.binaryType = "arraybuffer";

  ws.onopen = () => {
    console.log("WebSocket connected");
  };

  ws.onmessage = (event) => {

    const view = new DataView(event.data);
    const length = event.data.byteLength / 2;

    for (let i = 0; i < length; i++) {

      const sample = view.getInt16(i * 2, true) / 32768.0;

      ringBuffer[writePos] = sample;
      writePos = (writePos + 1) % ringBuffer.length;

      if (writePos === readPos) {
        readPos = (readPos + 1024) % ringBuffer.length;
      }
    }
  };
}

// 🔊 WAV送信
function sendWav() {

  if (!ws || ws.readyState !== WebSocket.OPEN) {
    alert("WebSocket未接続");
    return;
  }

  const fileInput = document.getElementById("wavFile");
  const file = fileInput.files[0];

  if (!file) {
    alert("WAVを選択してくださいね");
    return;
  }

  const reader = new FileReader();

  reader.onload = function(e) {

    const arrayBuffer = e.target.result;

    // WAVヘッダ除去（44バイト）
    const pcmData = arrayBuffer.slice(44);

    const chunkSize = 2048;
    let offset = 0;

    function sendChunk() {

      if (offset >= pcmData.byteLength) {
        ws.send("END");
        console.log("PCM send complete");
        return;
      }

      const slice = pcmData.slice(offset, offset + chunkSize);
      ws.send(slice);

      offset += chunkSize;

      setTimeout(sendChunk, 15);
    }

    sendChunk();
  };

  reader.readAsArrayBuffer(file);
}

</script>

</body>
</html>
```
※ <IP ADDRESS> を自分のIPに変更

---

### 🔊 WAVファイルをCoreS3に送信して再生する

PCブラウザからWAVファイルを選択し、
WebSocket経由でCoreS3に送信してスピーカー再生できます。
終わりにENDとテキストを送る必要があります。
![alt text](img/image2.png)

---

### 👆 タッチイベント送信
M5CoreS3のタッチパネルが押されたときに、
タッチ座標（x, y）
タッチイベント
を WebSocket 経由でPCへ通知します。

```json
{
  "event": "touch",
  "x": 120,
  "y": 200
}
```

### センサー送信
250ms周期で送信

```json
{
  "event": "sensors",
  "ambient": 342,
  "proximity": 120,
  "ax": 0.01,
  "ay": -0.98,
  "az": 0.12,
  "gx": 0.00,
  "gy": 0.02,
  "gz": -0.01,
  "battery": 83.4
}
```

---

### ⚠️ 重要注意事項
#### マイクとスピーカーは同時使用不可
必ず：
- 再生前に Mic.end()
- 録音前に Speaker.end()

#### iPhoneテザリングはWS通信不可

