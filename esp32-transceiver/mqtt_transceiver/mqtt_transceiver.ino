#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "secrets.h"

// Подставляем данные из secrets.h
const char* ssid          = SECRET_WIFI_SSID;
const char* password      = SECRET_WIFI_PASS;
const char* mqtt_broker   = SECRET_MQTT_BROKER;
const int   mqtt_port     = SECRET_MQTT_PORT;
const char* mqtt_username = SECRET_MQTT_USER;
const char* mqtt_password = SECRET_MQTT_PASS;
const char* mqtt_topic_cmd= SECRET_MQTT_TOPIC1;
const char* mqtt_topic_synch= SECRET_MQTT_TOPIC2;

// uart
#define STM32_RX_PIN 21 // прием от STM32 (подключать к PA9)
#define STM32_TX_PIN 22 // передача в STM32 (подключать к PA10)

WiFiClientSecure espClient;
PubSubClient client(espClient);

unsigned long lastReconnectAttempt = 0;

// Функция обработки входящих сообщений от MQTT брокера
void mqttCallback(char* topic, byte* payload, unsigned int length) 
{
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) 
  {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';

  // Выводим в Serial монитор компьютера для отладки
  Serial.print("[MQTT] Получена команда в топик [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // отправка команды по uart в stm32
  Serial2.println(message);
}

// Подключение к Wi-Fi
void setupWiFi() {
  delay(10);
  Serial.println();
  Serial.print("[WiFi] Подключение к ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("[WiFi] Успешно подключено!");
  Serial.print("[WiFi] IP-адрес: ");
  Serial.println(WiFi.localIP());
}

// Неблокирующее подключение к MQTT
boolean reconnect() {
  Serial.print("[MQTT] Попытка подключения к EMQX Cloud...");
  
  // Генерируем уникальный Client ID
  String clientId = "ESP32_AudioPlayer_" + String(random(0xffff), HEX);

  if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) 
  {
    Serial.println(" УСПЕШНО!");
    // Подписываемся на топик управления
    client.subscribe(mqtt_topic_cmd);
    Serial.print("[MQTT] Подписались на топик: ");
    Serial.println(mqtt_topic_cmd);
  } 
  else 
  {
    Serial.print(" ОШИБКА, код: ");
    Serial.println(client.state());
  }
  return client.connected();
}

void setup() {
  // USB порт для вывода логов на компьютер
  Serial.begin(115200);

  // Аппаратный UART2 для связи со STM32 на скорости 115200
  Serial2.begin(115200, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);

  setupWiFi();

  // Отключаем строгую проверку корневого сертификата (для простоты запуска)
  espClient.setInsecure();

  // Настройка MQTT клиента
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  // Проверка статуса Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }
  // Проверка статуса MQTT и неблокирующее переподключение
  if (!client.connected()) 
  {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) 
    {
      lastReconnectAttempt = now;
      if (reconnect()) 
      {
        lastReconnectAttempt = 0;
      }
    }
  } 
  else 
  {
    client.loop(); // Обработка событий сети и вызов callback
  }

  // Проброс ответов от STM32 обратно в отладочный Serial ESP32 
  while (Serial2.available()) {
    char c = Serial2.read();
    Serial.write(c);
  }
}