#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>

const char* STA_SSID = "moj";
const char* STA_PASS = "Amirali@";

const String BOT_TOKEN = "8955143221:AAG0KjH29aP5L21sYQ0te6njuRxZgEmZeuI";

const String TG_PROXY  = "https://tg-proxy.amirrezaadelfaraa.workers.dev";

const char MESSAGE[] = "hello world";
const String SHOW_TEXT = "data has been extracted";

const char* AP_SSID = "ESP32";
const char* AP_PASS = "12345678";

#define SS_PIN  5
#define RST_PIN 27

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
const byte BLOCK_NUM = 4;

WebServer server(80);

String lastText = "";
String lastUid  = "";
unsigned long scanCount = 0;

const char* CHAT_IDS[] = {
  "8419714778",
};
const int CHAT_COUNT = sizeof(CHAT_IDS) / sizeof(CHAT_IDS[0]);

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>RFID ESP32</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      min-height: 100vh;
      display: flex; align-items: center; justify-content: center;
      font-family: Tahoma, Arial, sans-serif;
      background: linear-gradient(135deg, #1e293b, #0f172a);
      color: #e2e8f0;
    }
    .card {
      width: min(90vw, 420px);
      padding: 40px 24px;
      text-align: center;
      background: #1e293b;
      border: 1px solid #334155;
      border-radius: 20px;
      box-shadow: 0 10px 40px rgba(0,0,0,.4);
    }
    .status { font-size: 14px; color: #94a3b8; margin-bottom: 20px; }
    .dot {
      display: inline-block; width: 10px; height: 10px;
      border-radius: 50%; background: #22c55e; margin-left: 6px;
    }
    .dot.off { background: #ef4444; }
    #msg { font-size: 64px; font-weight: bold; min-height: 90px; color: #38bdf8; }
    #msg.waiting { font-size: 22px; color: #64748b; line-height: 90px; font-weight: normal; }
    #msg.pop { animation: pop .5s ease; }
    @keyframes pop {
      0%   { transform: scale(.5); opacity: 0; }
      70%  { transform: scale(1.15); }
      100% { transform: scale(1); opacity: 1; }
    }
    .info { margin-top: 24px; font-size: 14px; color: #94a3b8; line-height: 2; }
    .info b { color: #e2e8f0; direction: ltr; unicode-bidi: embed; }
  </style>
</head>
<body>
  <div class="card">
    <div class="status"><span id="dot" class="dot off"></span><span id="conn">در حال اتصال...</span></div>
    <div id="msg" class="waiting">منتظر کارت...</div>
    <div class="info">
      تعداد اسکن: <b id="count">0</b><br>
      UID: <b id="uid">-</b>
    </div>
  </div>

  <script>
    let lastCount = 0;

    async function update() {
      try {
        const res = await fetch('/status', { cache: 'no-store' });
        const d = await res.json();

        document.getElementById('dot').className = 'dot';
        document.getElementById('conn').textContent = 'connect';

        if (d.count > 0) {
          const msg = document.getElementById('msg');
          msg.textContent = d.text;
          msg.classList.remove('waiting');
          document.getElementById('count').textContent = d.count;
          document.getElementById('uid').textContent = d.uid;

          if (d.count !== lastCount) {
            msg.classList.remove('pop');
            void msg.offsetWidth;
            msg.classList.add('pop');
            lastCount = d.count;
          }
        }
      } catch (e) {
        document.getElementById('dot').className = 'dot off';
        document.getElementById('conn').textContent = 'connection is lost';
      }
    }

    setInterval(update, 500);
    update();
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleStatus() {
  String json = "{\"text\":\"" + lastText + "\",\"uid\":\"" + lastUid +
                "\",\"count\":" + String(scanCount) + "}";
  server.send(200, "application/json; charset=utf-8", json);
}

String jsonEscape(const String& s) {
  String out = "";
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;
    }
  }
  return out;
}

String buildTelegramUrl(const String& method) {
  if (TG_PROXY.length() > 0) {
    String base = TG_PROXY;
    if (base.endsWith("/")) base.remove(base.length() - 1);
    return base + "/bot" + BOT_TOKEN + "/" + method;
  } else {
    return "https://api.telegram.org/bot" + BOT_TOKEN + "/" + method;
  }
}

void testTelegramConnection() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(8000);

  String url = buildTelegramUrl("getMe");
  Serial.println("Telegram connection test:");
  Serial.println("  " + url);

  http.begin(client, url);
  int code = http.GET();

  Serial.print("  Response code: ");
  Serial.println(code);

  if (code == 200) {
    Serial.println("  Connection to Telegram OK.");
    Serial.println("  " + http.getString());
  } else if (code > 0) {
    Serial.println("  Server responded but not 200:");
    Serial.println("  " + http.getString());
  } else {
    Serial.println("  Connection failed (DNS/TLS/filter).");
  }

  http.end();
}

void sendTelegramTo(const String& chatId, const String& text) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(8000);

  String url = buildTelegramUrl("sendMessage");
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  String body = "{\"chat_id\":\"" + jsonEscape(chatId) +
                "\",\"text\":\"" + jsonEscape(text) + "\"}";

  int code = http.POST(body);

  Serial.print("Telegram -> ");
  Serial.print(chatId);
  Serial.print(" : response code = ");
  Serial.println(code);

  if (code == 200) {
    Serial.println(http.getString());
  } else if (code > 0) {
    Serial.println("Server response:");
    Serial.println(http.getString());
  } else {
    Serial.println("Connection to server failed.");
  }

  http.end();
}

void sendTelegram(const String& text) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram: ESP32 not connected to internet");
    return;
  }

  for (int i = 0; i < CHAT_COUNT; i++) {
    sendTelegramTo(String(CHAT_IDS[i]), text);
    delay(300);
  }
}

String uidToString() {
  String s = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) s += "0";
    s += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) s += ":";
  }
  s.toUpperCase();
  return s;
}

void endCard() {
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  SPI.begin();
  rfid.PCD_Init();

  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setAutoReconnect(true);
  WiFi.begin(STA_SSID, STA_PASS);

  Serial.print("Connecting to internet");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Internet connected. IP: ");
    Serial.println(WiFi.localIP());
    testTelegramConnection();
  } else {
    Serial.println("Internet connection failed (check WiFi SSID/password)");
  }

  Serial.print("Web page: connect to WiFi ");
  Serial.print(AP_SSID);
  Serial.print(" and open http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.begin();

  Serial.println("Waiting for card...");
}

void loop() {
  server.handleClient();

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  MFRC522::StatusCode status = rfid.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A, BLOCK_NUM, &key, &(rfid.uid));

  if (status != MFRC522::STATUS_OK) {
    Serial.print("Auth error: ");
    Serial.println(rfid.GetStatusCodeName(status));
    endCard();
    return;
  }

  byte buffer[18];
  byte size = sizeof(buffer);
  status = rfid.MIFARE_Read(BLOCK_NUM, buffer, &size);

  if (status != MFRC522::STATUS_OK) {
    Serial.print("Read error: ");
    Serial.println(rfid.GetStatusCodeName(status));
    endCard();
    return;
  }

  if (memcmp(buffer, MESSAGE, strlen(MESSAGE)) == 0) {
    Serial.println(SHOW_TEXT);
    lastText = SHOW_TEXT;
    lastUid  = uidToString();
    scanCount++;

    endCard();
    sendTelegram(SHOW_TEXT + " UID: " + lastUid);
    return;
  } else {
    byte data[16] = {0};
    memcpy(data, MESSAGE, strlen(MESSAGE));

    status = rfid.MIFARE_Write(BLOCK_NUM, data, 16);
    if (status == MFRC522::STATUS_OK) {
      Serial.println("Text written to card; next scan will show message.");
    } else {
      Serial.print("Write error: ");
      Serial.println(rfid.GetStatusCodeName(status));
    }
  }

  endCard();
}


