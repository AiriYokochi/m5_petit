// WiFi設定（優先順位順：ssid1→ssid2→ssid3 にフォールバック）
// このファイルを credentials.h にコピーして実際の値を入れてください
const char* ssid1 = "<PHONE_SSID>";           // スマホテザリング
const char* pass1 = "<PHONE_PASSWORD>";
const char* ssid2 = "<HOME_SSID>";             // 家のWiFiルーター
const char* pass2 = "<HOME_PASSWORD>";
const char* ssid3 = "<TRAVEL_ROUTER_SSID>";   // GL.iNET等の旅行用ルーター（任意）
const char* pass3 = "<TRAVEL_ROUTER_PASSWORD>";

// WireGuard設定（PC側のPublicKeyとエンドポイント）
// wg genkey | tee privatekey | wg pubkey > publickey で生成
#define WG_SERVER_PUBLIC_KEY  "<PC_WG_PUBLIC_KEY>"
#define WG_SERVER_IP          "<PC_TAILSCALE_OR_PUBLIC_IP>"
#define WG_SERVER_PORT        51820

// WireGuard クライアント設定（キャラクターごと）
#define WG_PUCHIRU_PRIVATE_KEY  "<PUCHIRU_WG_PRIVATE_KEY>"
#define WG_PUCHIRU_IP           "10.10.0.2"
#define WG_PUCHIKO_PRIVATE_KEY  "<PUCHIKO_WG_PRIVATE_KEY>"
#define WG_PUCHIKO_IP           "10.10.0.3"
#define WG_PUCHITEYA_PRIVATE_KEY "<PUCHITEYA_WG_PRIVATE_KEY>"
#define WG_PUCHITEYA_IP          "10.10.0.4"
