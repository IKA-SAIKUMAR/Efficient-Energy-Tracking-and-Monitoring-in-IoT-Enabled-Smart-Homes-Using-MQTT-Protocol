#include "EmonLib.h"
#include <LiquidCrystal_I2C.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// MQTT Settings for HiveMQ
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* mqttUser = "123456789@1";
const char* mqttPassword = "123456789@1";

// MQTT Topics
const char* topic_gpio0 = "switch11";
const char* topic_gpio1 = "switch12";
const char* topic_gpio2 = "switch13";
const char* topic_gpio3 = "switch14";

// ThingSpeak MQTT Settings
const char* tsMqttServer = "mqtt3.thingspeak.com";
const int tsMqttPort = 1883;
const char* tsMqttUser = "Ni4yPBUcJxw1CRApJjMdOh0";
const char* tsMqttPassword = "4omwrgqhu/5wUJH7Fm3nYNRe";
const char* tsClientID = "Ni4yPBUcJxw1CRApJjMdOh0";
const char* tsPublishTopic = "channels/2813094/publish";

// Energy Monitor
EnergyMonitor emon;
const float vCalibration = 42.5;
const float currCalibration = 1.80;
float powerValue = 0.0;
float energyConsumed = 0.0; // Energy in Wh

// WiFi and MQTT Clients
WiFiClient espClient;
PubSubClient client(espClient);
WiFiClient tsClient;
PubSubClient tsMqttClient(tsClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// GPIO Definitions
const int GPIO0 = 13;
const int GPIO1 = 4;
const int GPIO2 = 5;
const int GPIO3 = 12;
const int Button0 = 14;
const int Button1 = 27;
const int Button2 = 26;
const int Button3 = 25;

bool gpioState0 = LOW;
bool gpioState1 = LOW;
bool gpioState2 = LOW;
bool gpioState3 = LOW;
bool lastButtonState0 = HIGH;
bool lastButtonState1 = HIGH;
bool lastButtonState2 = HIGH;
bool lastButtonState3 = HIGH;

// Manual Control Flag to prevent MQTT from overriding button presses
bool manualControlActive = false;

// Function Declarations
void gpioControlTask(void* pvParameters);
void mqttTask(void* pvParameters);
void energyMonitoringTask(void* pvParameters);
void thingSpeakTask(void* pvParameters);

// Callback for MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  // Only update GPIO state if manual control is not active
  if (!manualControlActive) {
    if (String(topic) == topic_gpio0) {
      gpioState0 = (message == "1");
      digitalWrite(GPIO0, gpioState0);
    } else if (String(topic) == topic_gpio1) {
      gpioState1 = (message == "1");
      digitalWrite(GPIO1, gpioState1);
    } else if (String(topic) == topic_gpio2) {
      gpioState2 = (message == "1");
      digitalWrite(GPIO2, gpioState2);
    } else if (String(topic) == topic_gpio3) {
      gpioState3 = (message == "1");
      digitalWrite(GPIO3, gpioState3);
    }
  }
}

// Reconnect to MQTT brokers
void reconnect(PubSubClient& mqttClient, const char* user, const char* password, const char* clientID = nullptr) {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(clientID ? clientID : "espClient", user, password)) {
      if (&mqttClient == &client) {
        mqttClient.subscribe(topic_gpio0);
        mqttClient.subscribe(topic_gpio1);
        mqttClient.subscribe(topic_gpio2);
        mqttClient.subscribe(topic_gpio3);
      }
    } else {
      delay(5000);
    }
  }
}

// LCD Update Function
void updateLCD(float Vrms, float Irms) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("V:%.2f I:%.2f", Vrms, Irms);
  lcd.setCursor(0, 1);
  lcd.printf("E:%.3fkWh", energyConsumed / 1000.0); // Energy in kWh
}

// Debounce and GPIO Control Task
unsigned long lastButtonPressTime0 = 0;
unsigned long lastButtonPressTime1 = 0;
unsigned long lastButtonPressTime2 = 0;
unsigned long lastButtonPressTime3 = 0;
const unsigned long debounceDelay = 200; // 200ms debounce delay

void gpioControlTask(void* pvParameters) {
  while (true) {
    unsigned long currentMillis = millis();

    // Handle Button 0
    bool buttonState0 = digitalRead(Button0);
    if (buttonState0 == LOW && (currentMillis - lastButtonPressTime0) > debounceDelay) {
      gpioState0 = !gpioState0;
      digitalWrite(GPIO0, gpioState0);
      if (client.connected()) {
        client.publish(topic_gpio0, gpioState0 ? "1" : "0");
      }
      lastButtonPressTime0 = currentMillis;
      manualControlActive = true; // Set manual control active
    }

    // Handle Button 1
    bool buttonState1 = digitalRead(Button1);
    if (buttonState1 == LOW && (currentMillis - lastButtonPressTime1) > debounceDelay) {
      gpioState1 = !gpioState1;
      digitalWrite(GPIO1, gpioState1);
      if (client.connected()) {
        client.publish(topic_gpio1, gpioState1 ? "1" : "0");
      }
      lastButtonPressTime1 = currentMillis;
      manualControlActive = true; // Set manual control active
    }

    // Handle Button 2
    bool buttonState2 = digitalRead(Button2);
    if (buttonState2 == LOW && (currentMillis - lastButtonPressTime2) > debounceDelay) {
      gpioState2 = !gpioState2;
      digitalWrite(GPIO2, gpioState2);
      if (client.connected()) {
        client.publish(topic_gpio2, gpioState2 ? "1" : "0");
      }
      lastButtonPressTime2 = currentMillis;
      manualControlActive = true; // Set manual control active
    }

    // Handle Button 3
    bool buttonState3 = digitalRead(Button3);
    if (buttonState3 == LOW && (currentMillis - lastButtonPressTime3) > debounceDelay) {
      gpioState3 = !gpioState3;
      digitalWrite(GPIO3, gpioState3);
      if (client.connected()) {
        client.publish(topic_gpio3, gpioState3 ? "1" : "0");
      }
      lastButtonPressTime3 = currentMillis;
      manualControlActive = true; // Set manual control active
    }

    delay(10); // Small delay for responsiveness
  }
}

// MQTT Task
void mqttTask(void* pvParameters) {
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin("");
    }
    if (!client.connected()) {
      reconnect(client, mqttUser, mqttPassword);
    }
    client.loop();  // Handle incoming MQTT messages when they arrive
    delay(10);
  }
}

// Energy Monitoring Task
void energyMonitoringTask(void* pvParameters) {
  while (true) {
    emon.calcVI(20, 2000);
    float Vrms = emon.Vrms;
    float Irms = emon.Irms;

    if (Vrms >= 150.0 && Irms >= 0.01) {
      powerValue = Vrms * Irms; // Calculate power in W
      energyConsumed += (powerValue / 3600.0); // Add energy in Wh
    } else {
      powerValue = 0.0; // Reset power to 0
      Vrms = 0.0;      // Reset voltage to 0
      Irms = 0.0;      // Reset current to 0
    }

    updateLCD(Vrms, Irms); // Update the LCD display with current values
    delay(1000); // Update every second
  }
}

// ThingSpeak Task
void thingSpeakTask(void* pvParameters) {
  while (true) {
    if (!tsMqttClient.connected()) {
      reconnect(tsMqttClient, tsMqttUser, tsMqttPassword, tsClientID);
    }
    tsMqttClient.loop();

    char payload[100];
    snprintf(payload, sizeof(payload), "field1=%.2f&field2=%d&field3=%d&field4=%d&field5=%d", 
         powerValue, gpioState0, gpioState1, gpioState2, gpioState3);

    tsMqttClient.publish(tsPublishTopic, payload);

    delay(20000);  // Send data to ThingSpeak every 20 seconds
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(GPIO0, OUTPUT);
  pinMode(GPIO1, OUTPUT);
  pinMode(GPIO2, OUTPUT);
  pinMode(GPIO3, OUTPUT);
  pinMode(Button0, INPUT);
  pinMode(Button1, INPUT);
  pinMode(Button2, INPUT);
  pinMode(Button3, INPUT);

  WiFiManager wifiManager;
  wifiManager.autoConnect("ESP32_AP");

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);  // Set the MQTT callback to handle remote GPIO control

  tsMqttClient.setServer(tsMqttServer, tsMqttPort);

  lcd.init();
  lcd.backlight();

  emon.voltage(35, vCalibration, 1.8);
  emon.current(34, currCalibration);

  xTaskCreate(gpioControlTask, "GPIO Control", 8192, NULL, 3, NULL);
  xTaskCreate(mqttTask, "MQTT Task", 8192, NULL, 2, NULL);
  xTaskCreate(energyMonitoringTask, "Energy Monitoring", 8192, NULL, 1, NULL);
  xTaskCreate(thingSpeakTask, "ThingSpeak Task", 8192, NULL, 1, NULL);
}

void loop() {
  // Empty loop as FreeRTOS tasks handle everything
}
