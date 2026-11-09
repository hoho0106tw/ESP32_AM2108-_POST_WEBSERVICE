/*
 * ESP32 + AM2108 -> POST /api/receive，每分鐘傳送一次。
 * 將本檔全部內容複製到 Arduino IDE 的 .ino 草稿後上傳。
 *
 * Arduino IDE 函式庫管理員：安裝 Adafruit AHTX0，並安裝其相依套件
 * Adafruit Unified Sensor、Adafruit BusIO。
 * 沿用原程式已使用的 Adafruit_AHTX0 感測器介面。
 *
 * 一般 ESP32 接線（其他板型請自行修改下方腳位）：
 * AM2108 VCC -> 3.3V、GND -> GND、SDA -> GPIO21、SCL -> GPIO22。
 * 序列埠監控視窗：115200 baud。
 * 填入下方 WIFI_SSID、WIFI_PASSWORD；確認 ESP32 能連到 API 的內網。
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <time.h>
#include <math.h>

const char* WIFI_SSID =""; 
const char* WIFI_PASSWORD =""; 
const char* WEB_SERVICE_URL = "http://10.80.223.38:889/api/receive";

const int SDA_PIN = 21;
const int SCL_PIN = 22;
const unsigned long SEND_INTERVAL_MS = 60000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 15000;
const int32_t CONNECT_TIMEOUT_MS = 5000;
const uint16_t READ_TIMEOUT_MS = 5000;

Adafruit_AHTX0 aht;
unsigned long lastSendAt = 0;
unsigned long lastWiFiRetryAt = 0;
bool firstSend = true;
bool wasWiFiConnected = false;

String getFormattedTime() {
  struct tm timeInfo;
  // NTP 無法同步時不阻塞上傳；API 仍會回傳伺服器時間。
  if (!getLocalTime(&timeInfo, 100)) {
    return "Time_Not_Synchronized";
  }
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
  return String(buffer);
}

void sendSensorData() {
  sensors_event_t humidity, temperature;
  if (!aht.getEvent(&humidity, &temperature)) {
    Serial.println("[錯誤] 感測器讀取失敗，本次不傳送。");
    return;
  }

  float t = temperature.temperature;
  float h = humidity.relative_humidity;
  if (!isfinite(t) || !isfinite(h) || h < 0 || h > 100) {
    Serial.println("[錯誤] 感測器數值無效，本次不傳送。");
    return;
  }

  String deviceTime = getFormattedTime();
  String jsonBody = "{\"temp\":" + String(t, 2) +
                    ",\"humid\":" + String(h, 2) +
                    ",\"time\":\"" + deviceTime + "\"}";

  Serial.println("\n--- Sending Data to WebService ---");
  Serial.printf("WiFi RSSI: %ld dBm\n", (long)WiFi.RSSI());
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("ESP32 WiFi 實體位址 (MAC): ");
  Serial.println(WiFi.macAddress());
  Serial.printf("Temp: %.2f C, Humid: %.2f %%\n", t, h);
  Serial.print("Device Time: ");
  Serial.println(deviceTime);
  Serial.print("POST JSON: ");
  Serial.println(jsonBody);

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, WEB_SERVICE_URL)) {
    Serial.println("[錯誤] HTTP 初始化失敗，請檢查網址。");
    http.end();
    return;
  }

  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(READ_TIMEOUT_MS);
  // 本專案 API 回應使用 Connection: close，每次建立獨立連線。
  http.setReuse(false);
  http.addHeader("Content-Type", "application/json");

  unsigned long startedAt = millis();
  int code = http.POST(jsonBody);
  Serial.printf("POST 耗時: %lu ms\n", millis() - startedAt);

  if (code > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(code);
    Serial.println("Server Response:");
    Serial.println(http.getString());
    if (code >= 200 && code < 300) {
      Serial.println("[成功] 伺服器已回應本次上傳。");
    } else {
      Serial.println("[失敗] 伺服器回傳非 2xx 狀態，請查看上方內容。");
    }
  } else {
    Serial.print("[失敗] HTTP Error code: ");
    Serial.println(code);
    Serial.print("Error message: ");
    Serial.println(HTTPClient::errorToString(code));
    Serial.printf("WiFi status: %d\n", (int)WiFi.status());
    if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
      Serial.println("TCP 連線建立失敗；請檢查網路及伺服器 889 埠，RSSI 正常也可能發生。");
    }
    // 不自動重送 POST，避免回應遺失時造成重複資料。
    Serial.println("本筆未確認成功；下一分鐘讀取新資料再傳送。本程式不保存失敗資料。");
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!aht.begin(&Wire)) {
    Serial.println("[錯誤] 找不到 AM2108，請檢查電源、I2C 接線與感測器相容性。");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("AM2108 感測器初始化成功。");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // 尚未連線也先印出 STA 網卡實體位址，方便申請網路放行。
  Serial.print("ESP32 WiFi 實體位址 (MAC): ");
  Serial.println(WiFi.macAddress());
  lastWiFiRetryAt = millis();
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (wasWiFiConnected) {
      Serial.println("[警告] WiFi 已斷線，等待重新連線。");
      wasWiFiConnected = false;
    }
    if (now - lastWiFiRetryAt >= WIFI_RETRY_INTERVAL_MS) {
      lastWiFiRetryAt = now;
      Serial.println("[WiFi] 嘗試重新連線，請確認 SSID、密碼及基地台。");
      WiFi.reconnect();
    }
    delay(10);
    return;
  }

  if (!wasWiFiConnected) {
    wasWiFiConnected = true;
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("ESP32 WiFi 實體位址 (MAC): ");
    Serial.println(WiFi.macAddress());
    // 台灣時間 UTC+8；背景同步，最多先等待 10 秒。
    configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
    struct tm timeInfo;
    if (getLocalTime(&timeInfo, 10000)) {
      Serial.println("NTP 時間同步成功。");
    } else {
      Serial.println("[提示] NTP 尚未同步，先上傳並使用 Time_Not_Synchronized 標記。");
    }
  }

  now = millis();
  // 首次連線立即傳送，其後每 60 秒啟動一次；使用差值支援 millis 溢位。
  if (firstSend || now - lastSendAt >= SEND_INTERVAL_MS) {
    firstSend = false;
    lastSendAt = now;
    sendSensorData();
  }
  delay(10);
}
