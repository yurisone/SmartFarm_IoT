// 온습도, 조도 , (토양습도) 데이터 MQTT 서버에 5초 간격으로 Publish
//ESP32 사용

#include <Arduino.h>
#include <ConfigPortal32.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <Wire.h>
#include <BH1750.h>

char*               ssid_pfix = (char*)"Smart Farm Sensor";
String              user_config_html = ""
    "<p><input type='text' name='broker' placeholder='MQTT Server'>";

char                mqttServer[100];
const int           mqttPort = 1883;
unsigned long       pubInterval = 5000;   //5초 마다 갱신
unsigned long       lastPublished = - pubInterval;

#define             SDA_PIN 21  // Define your custom SDA pin
#define             SCL_PIN 22  // Define your custom SCL pin

BH1750              lightMeter;
float               lux = 0;
char                buf_lux[50];

#define             DHTPIN 15
DHTesp              dht;
int                 dht_interval = 2000;
unsigned long       lastDHTReadMillis = 0;
float               humidity = 0;
float               temperature = 0;
char                buf_temp[50];
char                buf_humi[50];

#define SENPIN 33
float soilMoistureValue = 0;
char                buf_soil[50];

WiFiClient wifiClient;
PubSubClient client(wifiClient);
void pubStatus();

void readDHT22() {
    unsigned long currentMillis = millis();

    if(millis() > lastDHTReadMillis + dht_interval) {
        lastDHTReadMillis = currentMillis;

        humidity = dht.getHumidity();
        temperature = dht.getTemperature();
    }
}

void setup() {
    Serial.begin(115200);
    loadConfig();
    // *** If no "config" is found or "config" is not "done", run configDevice ***
    if(!cfg.containsKey("config") || strcmp((const char*)cfg["config"], "done")) {
        configDevice();
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin((const char*)cfg["ssid"], (const char*)cfg["w_pw"]);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    // main setup
    dht.setup(DHTPIN, DHTesp::DHT22);   // Connect DHT Sensr to GPIO 18
    Wire.begin(SDA_PIN, SCL_PIN);  // Initialize I²C with custom pins
    lightMeter.begin();
    pinMode(SENPIN, INPUT);
    if (cfg.containsKey("broker")) {
            sprintf(mqttServer, (const char*)cfg["broker"]);
    }
    client.setServer(mqttServer, mqttPort);

    while (!client.connected()) {
        Serial.println("Connecting to MQTT...");
        if (client.connect("MQTT_Smart_Farm_Sensor")) {
            Serial.println("connected");  
        } else {
            Serial.print("failed with state "); Serial.println(client.state());
            delay(2000);
        }
    }
}

void loop() {
    client.loop();

    unsigned long currentMillis = millis();
    if(currentMillis - lastPublished >= pubInterval) {
        lastPublished = currentMillis;
        readDHT22();
        lux = lightMeter.readLightLevel();
        soilMoistureValue = analogRead(SENPIN);
        sprintf(buf_lux, "%.2f", lux);
        sprintf(buf_temp, "%.1f", temperature);
        sprintf(buf_humi, "%.1f", humidity);
        sprintf(buf_soil, "%.1f", soilMoistureValue);
        pubStatus();
    }
}

void pubStatus() {  //온도, 습도, 조도 publish
    client.publish("id/SmartFarm/sensor/evt/temperature", buf_temp);
    client.publish("id/SmartFarm/sensor/evt/humidity", buf_humi);
    client.publish("id/SmartFarm/sensor/evt/lux", buf_lux);
    client.publish("id/SmartFarm/sensor/evt/soil", buf_soil); 
}
