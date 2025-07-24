
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ESP32Servo.h>
#include <SPIFFS.h>

// === WiFi Default ===
const char* default_ssid = "Nama_WiFi_Awal";
const char* default_pass = "Password_WiFi_Awal";

// === Telegram ===
const char* botToken = "7297079456:AAFkc4N7eCOqIIEcGz3xtwDiv4DG1BJE0pM";
const String ADMIN_ID = "1268342014";
WiFiClientSecure secured_client;
UniversalTelegramBot bot(botToken, secured_client);

// === PIN CONFIG ===
#define FLAME_PIN      4
#define MQ2_ANALOG_PIN 34
#define LED_SYS_PIN    2
#define LED_WIFI_PIN   23
#define BUZZER_PIN     13
#define MOTOR_PIN      27
#define SERVO_PIN      14

// === SYSTEM STATE ===
const int gasThreshold = 600;
bool gasLeakDetected = false;
bool apiNotified = false;
bool gasNormalSent = true;
unsigned long motorStopTime = 0;

// === CHAT ID STORAGE ===
#define MAX_CHAT_ID 10
String chatIDs[MAX_CHAT_ID];
String userNames[MAX_CHAT_ID];
int chatCount = 0;

// Servo
Servo jendela;

// === ID Management ===
bool isRegistered(const String &id) {
  for (int i = 0; i < chatCount; i++)
    if (chatIDs[i] == id) return true;
  return false;
}

void registerChatID(const String &id, const String &name) {
  if (chatCount < MAX_CHAT_ID && !isRegistered(id)) {
    chatIDs[chatCount] = id;
    userNames[chatCount] = name;
    chatCount++;
    saveChatIDs();
  }
}

void unregisterChatID(const String &id) {
  for (int i = 0; i < chatCount; i++) {
    if (chatIDs[i] == id) {
      for (int j = i; j < chatCount - 1; j++) {
        chatIDs[j] = chatIDs[j + 1];
        userNames[j] = userNames[j + 1];
      }
      chatCount--;
      saveChatIDs();
      return;
    }
  }
}

String getChatIDList() {
  if (chatCount == 0) return "⚠️ Tidak ada pengguna terdaftar.";
  String s = "📋 Pengguna terdaftar:
";
  for (int i = 0; i < chatCount; i++)
    s += String(i + 1) + ". " + userNames[i] + " (" + chatIDs[i] + ")
";
  return s;
}

bool isNumeric(const String &s) {
  if (s.length() == 0) return false;
  for (char c : s)
    if (!isDigit(c)) return false;
  return true;
}

void saveChatIDs() {
  File f = SPIFFS.open("/chatids.txt", "w");
  if (!f) return;
  for (int i = 0; i < chatCount; i++)
    f.println(chatIDs[i] + "," + userNames[i]);
  f.close();
}

void loadChatIDs() {
  if (!SPIFFS.begin(true)) return;
  File f = SPIFFS.open("/chatids.txt", "r");
  if (!f) return;
  chatCount = 0;
  while (f.available() && chatCount < MAX_CHAT_ID) {
    String line = f.readStringUntil('\n');
    line.trim();
    int sep = line.indexOf(',');
    if (sep > 0) {
      String id = line.substring(0, sep);
      String name = line.substring(sep + 1);
      if (!isRegistered(id)) {
        chatIDs[chatCount] = id;
        userNames[chatCount] = name;
        chatCount++;
      }
    }
  }
  f.close();
}

void broadcastMessage(const String &msg) {
  for (int i = 0; i < chatCount; i++)
    bot.sendMessage(chatIDs[i], msg, "");
}

// === Telegram Command Handler ===
void handleNewMessages() {
  int newMsg = bot.getUpdates(bot.last_message_received + 1);
  while (newMsg) {
    for (int i = 0; i < newMsg; i++) {
      String chat_id = bot.messages[i].chat_id;
      String user = bot.messages[i].from_name;
      String text = bot.messages[i].text;

      if (text == "/start") {
        if (!isRegistered(chat_id)) {
          registerChatID(chat_id, user);
          bot.sendMessage(chat_id, "✅ Kamu terdaftar!");
          bot.sendMessage(ADMIN_ID, "👤 Pengguna baru:
" + user + " (" + chat_id + ")");
        } else {
          bot.sendMessage(chat_id, "ℹ️ Kamu sudah terdaftar.");
        }
      } else if (text == "/stop") {
        if (isRegistered(chat_id)) {
          unregisterChatID(chat_id);
          bot.sendMessage(chat_id, "❌ Kamu keluar.");
          bot.sendMessage(ADMIN_ID, "👋 Pengguna keluar:
" + user + " (" + chat_id + ")");
        } else {
          bot.sendMessage(chat_id, "⚠️ Kamu belum terdaftar.");
        }
      } else if (text == "/list") {
        if (chat_id == ADMIN_ID)
          bot.sendMessage(chat_id, getChatIDList());
        else
          bot.sendMessage(chat_id, "🚫 Hanya admin yang bisa menggunakan perintah ini.");
      } else if (text.startsWith("/hapus ")) {
        if (chat_id == ADMIN_ID) {
          String idToDelete = text.substring(7);
          idToDelete.trim();
          if (!isNumeric(idToDelete)) {
            bot.sendMessage(chat_id, "⚠️ Gunakan format: /hapus <chat_id>");
          } else if (isRegistered(idToDelete)) {
            unregisterChatID(idToDelete);
            bot.sendMessage(chat_id, "🗑️ ID dihapus: " + idToDelete);
            bot.sendMessage(idToDelete, "❌ Kamu telah dihapus oleh admin.");
          } else {
            bot.sendMessage(chat_id, "⚠️ ID tidak ditemukan.");
          }
        } else {
          bot.sendMessage(chat_id, "🚫 Hanya admin yang dapat menghapus.");
        }
      } else {
        bot.sendMessage(chat_id,
          "📌 Perintah tersedia:\n"
          "/start - Daftar\n"
          "/stop - Berhenti\n"
          "/list - Daftar pengguna (admin)\n"
          "/hapus <chat_id> - Hapus pengguna (admin)");
      }
    }
    newMsg = bot.getUpdates(bot.last_message_received + 1);
  }
}

// === SETUP ===
void setup() {
  Serial.begin(115200);

  pinMode(MQ2_ANALOG_PIN, INPUT);
  pinMode(FLAME_PIN, INPUT);
  pinMode(LED_SYS_PIN, OUTPUT);
  pinMode(LED_WIFI_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);

  digitalWrite(LED_SYS_PIN, HIGH);
  digitalWrite(LED_WIFI_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(MOTOR_PIN, LOW);

  WiFi.begin(default_ssid, default_pass);
  Serial.print("🔌 Menghubungkan ke WiFi default...");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 5) {
    delay(500); Serial.print(".");
    tries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️ Gagal konek, masuk WiFiManager...");
    WiFiManager wm;
    if (!wm.autoConnect("ESP32_Config", "12345678")) {
      Serial.println("❌ Gagal konek lewat WiFiManager. Restart...");
      ESP.restart();
    }
  } else {
    Serial.println("\n✅ Tersambung ke WiFi default.");
  }

  secured_client.setInsecure();
  digitalWrite(LED_WIFI_PIN, LOW);

  bot.sendMessage(ADMIN_ID, "🤖 ESP32 berhasil terhubung ke bot.");
  loadChatIDs();
  registerChatID(ADMIN_ID, "Admin");

  jendela.setPeriodHertz(50);
  jendela.attach(SERVO_PIN);
  jendela.write(0);
}

// === LOOP ===
void loop() {
  handleNewMessages();

  int gasValue = analogRead(MQ2_ANALOG_PIN);
  Serial.print("Gas: "); Serial.println(gasValue);

  if (gasValue > gasThreshold) {
    if (!gasLeakDetected) {
      gasLeakDetected = true;
      gasNormalSent = false;
      jendela.write(90);
      digitalWrite(MOTOR_PIN, HIGH);
      digitalWrite(LED_SYS_PIN, LOW);
      broadcastMessage("🚨 Kebocoran gas terdeteksi!");
    }
  } else if (gasLeakDetected && gasValue <= gasThreshold - 50) {
    gasLeakDetected = false;
    if (!gasNormalSent) {
      gasNormalSent = true;
      digitalWrite(MOTOR_PIN, LOW);
      digitalWrite(LED_SYS_PIN, HIGH);
      motorStopTime = millis();
      broadcastMessage("✅ Gas kembali normal.");
    }
  }

  if (motorStopTime != 0 && millis() - motorStopTime >= 3000) {
    jendela.write(0);
    motorStopTime = 0;
  }

  bool apiDetected = digitalRead(FLAME_PIN) == LOW;
  if (apiDetected) {
    digitalWrite(BUZZER_PIN, HIGH);
    if (!apiNotified) {
      apiNotified = true;
      broadcastMessage("🔥 Api terdeteksi!");
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    apiNotified = false;
  }

  delay(300);
}
