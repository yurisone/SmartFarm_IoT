//온습도, 조도, (토양습도) 데이터 MQTT 서버로부터 수신 후 모터 제어 및 펌프 제어 / 버튼을 통한 모터, 네오픽셀 제어 로터리 엔코더 통해 환경설정

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ConfigPortal32.h>
#include <Adafruit_NeoPixel.h>

char*               ssid_pfix = (char*)"SmartFarmControl";
String              user_config_html = ""
    "<p><input type='text' name='broker' placeholder='MQTT Server'>";

char                mqttServer[100];
const int           mqttPort = 1883;
void msgCB(char* topic, byte* payload, unsigned int length); // message Callback

//neopixel 핀설정
#define LED_PIN 17
#define LED_NUM 8
Adafruit_NeoPixel pixels(LED_NUM, LED_PIN, NEO_GRB + NEO_KHZ800);

// Relay 핀 및 변수 설정
#define relaypin 2
bool relayState = false;

//모터 버튼 핀
#define SW1 21

//neopixel 버튼 핀
#define SW2 16
bool neoState = false;

WiFiClient wifiClient;
PubSubClient client(wifiClient);
WebServer server(80);

TFT_eSPI tft = TFT_eSPI();

char                buf_lux[50];
char                buf_temp[50];
char                buf_humi[50];
char                buf_soil[50];

float temperature = 0;
float humidity = 0;
float lux = 0;

float soil = 0;

//모터 끄기 켜기 인터럽트
volatile bool relayUpdateFlag = true;  // 모터 상태 강제 제어 플래그
volatile bool forceRelayState = false;  // 버튼으로 강제 제어 중인지 여부
volatile unsigned long relayTimer = 0;  // 강제 제어 시작 시간
const unsigned long relayForceDuration = 600000; // 10분 (600,000 ms)

// 모터 제어 인터럽트
IRAM_ATTR void setRelay() {
    //현재 상태 반전
    relayState = !relayState;

    if (relayState){
        digitalWrite(relaypin, HIGH);
        Serial.println("on");
    }
    else{
       digitalWrite(relaypin, LOW);
        Serial.println("off"); 
    }
    //강제 제어 시작
    forceRelayState = true;
    relayTimer = millis();
    relayUpdateFlag = true;
}

//neopixel 끄기 켜기 인터럽트
// 네오픽셀 상태 및 제어 변수
volatile bool neoUpdateFlag = true; // 네오픽셀 업데이트 플래그
volatile bool forceNeoState = false; // 버튼으로 강제 제어 중인지 여부
volatile unsigned long neoTimer = 0; // 강제 제어 시작 시간
const unsigned long neoForceDuration = 600000; // 강제 제어 지속 시간 (10분)

// 네오픽셀 버튼 인터럽트 처리
IRAM_ATTR void setNeo() {
    // 현재 상태 반전
    neoState = !neoState;

    // 강제 제어 시작
    forceNeoState = true;
    neoTimer = millis();
    neoUpdateFlag = true; // 플래그 설정
}

// 웹 페이지 (릴레이 버튼 및 온습도 값 표시)
void handleRoot() {
    char html[1024];
    char templateHtml[] =
        "<html><head><meta charset=\"utf-8\">"
        "<title>제어 및 모니터링</title></head>"
        "<body><center>"
        "<h1>릴레이 및 LED 제어</h1>"
        "<h2>온도: %.1f &deg;C</h2>"
        "<h2>습도: %.1f %%</h2>"
        "<h2>조도: %.2f</h2>"
        "<h3>릴레이 상태: %s</h3>"
        "<h3>LED 상태: %s</h3>"
        "<form action='/relay' method='GET'>"
        "<button name='state' value='on' type='submit' style='font-size:16px;'>릴레이 켜기</button>"
        "<button name='state' value='off' type='submit' style='font-size:16px;'>릴레이 끄기</button>"
        "</form>"
        "<form action='/led' method='GET'>"
        "<button name='state' value='on' type='submit' style='font-size:16px;'>LED 켜기</button>"
        "<button name='state' value='off' type='submit' style='font-size:16px;'>LED 끄기</button>"
        "</form>"
        "</center></body></html>";

    sprintf(html, templateHtml, temperature, humidity, lux, relayState ? "ON" : "OFF", neoState ? "ON" : "OFF");
    server.send(200, "text/html", html);
}

// 릴레이 모터제어
void handleRelay() {
    if (server.hasArg("state")) {
        String state = server.arg("state");
        if (state == "on") {
            digitalWrite(relaypin, HIGH);
            relayState = true;
        } else if (state == "off") {
            digitalWrite(relaypin, LOW);
            relayState = false;
        }
        //강제 제어 시작
        forceRelayState = true;
        relayTimer = millis();
        relayUpdateFlag = true;
    }
    // 상태 갱신 후 메인 페이지로 리디렉트
    server.sendHeader("Location", "/");
    server.send(303);
}

// LED 제어
void handleLED() {
    if (server.hasArg("state")) {
        String state = server.arg("state");
        if (state == "on") {
            neoState = true;
        } else if (state == "off") {
            neoState = false;
        }
        //강제 제어 시작
        forceNeoState = true;
        neoTimer = millis();
        neoUpdateFlag = true;
    }
    // 상태 갱신 후 메인 페이지로 리디렉트
    server.sendHeader("Location", "/");
    server.send(303);
}

// 404 처리
void handleNotFound() {
    server.send(404, "text/plain", "잘못된 요청입니다. 경로를 확인하세요.");
}

//셋업
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
    MDNS.begin("SmartFarmWeb");
    // 웹 서버 경로 설정
    server.on("/", handleRoot);         // 메인 페이지
    server.on("/relay", handleRelay);   // 릴레이 제어
    server.on("/led", handleLED);     // 펌프 제어
    server.onNotFound(handleNotFound);  // 404 에러 처리
    server.begin();
    Serial.println("HTTP server start");

    pixels.begin();
    pinMode(relaypin, OUTPUT);
    digitalWrite(relaypin, LOW);
    pinMode(SW1, INPUT_PULLUP);
    pinMode(SW2, INPUT_PULLUP);
    attachInterrupt(SW1, setRelay, FALLING);
    attachInterrupt(SW2, setNeo, FALLING);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Status", 80, 20, 4);
    tft.drawString("Temp : ", 10, 60, 2);
    tft.drawString("Humi : ", 10, 90, 2);
    tft.drawString("Lux  : ", 10, 120, 2);
    tft.drawString("soil : ", 10, 150, 2);
    tft.drawString("Motor : ", 140, 60, 2);
    tft.drawString("Light : ", 140, 90, 2);
    tft.drawLine(120, 60, 120, 170, TFT_WHITE);
    Serial.printf("\nIP address : "); Serial.println(WiFi.localIP());

    if (cfg.containsKey("broker")) {
            sprintf(mqttServer, (const char*)cfg["broker"]);
    }
    client.setServer(mqttServer, mqttPort);
    client.setCallback(msgCB);

    while (!client.connected()) {
        Serial.println("Connecting to MQTT...");
        if (client.connect("Smart Farm Control")) {
            Serial.println("connected");  
        } else {
            Serial.print("failed with state "); Serial.println(client.state());
            delay(2000);
        }
    }
    client.subscribe("id/SmartFarm/control/cmd/#");
    client.subscribe("id/SmartFarm/sensor/evt/#");
}

//main loop
void loop() {
    client.loop();
    server.handleClient();

    sprintf(buf_lux, "%.2f", lux);
    sprintf(buf_temp, "%.1f", temperature);
    sprintf(buf_humi, "%.1f", humidity);
    sprintf(buf_soil, "%.1f", soil); 
    tft.fillRect(60, 60, 40, 20, TFT_BLACK);
    tft.drawString(buf_temp, 60, 60, 2);
    tft.fillRect(60, 90, 40, 20, TFT_BLACK);
    tft.drawString(buf_humi, 60, 90, 2);
    tft.fillRect(60, 120, 60, 40, TFT_BLACK);
    tft.drawString(buf_lux, 60, 120, 2);
    tft.fillRect(60, 150, 60, 40, TFT_BLACK);
    tft.drawString(buf_soil, 60, 150, 2);

    if (relayState == false){
        tft.fillRect(200, 60, 40, 20, TFT_BLACK);
        tft.drawString("Off", 200, 60, 2);
    } else{
        tft.fillRect(200, 60, 40, 20, TFT_BLACK);
        tft.drawString("On", 200, 60, 2); 
    }

    if (neoState == false){
        tft.fillRect(200, 90, 40, 20, TFT_BLACK);
        tft.drawString("Off", 200, 90, 2);
    } else{
        tft.fillRect(200, 90, 40, 20, TFT_BLACK);
        tft.drawString("On", 200, 90, 2); 
    }

    // relay 강제 제어 상태 유지
    if (forceRelayState){
       if (millis() - relayTimer >= relayForceDuration) {
            // 강제 제어 종료
            forceRelayState = false;
            relayUpdateFlag = true; // MQTT 값에 따라 업데이트
        } 
    }

    // 네오픽셀 강제 제어 상태 유지
    if (forceNeoState) {
        if (millis() - neoTimer >= neoForceDuration) {
            // 강제 제어 종료
            forceNeoState = false;
            neoUpdateFlag = true; // MQTT 값에 따라 업데이트
        }
    }

    // 네오픽셀 제어
    if (neoUpdateFlag) {
        if (forceNeoState) {
            if (neoState) {
                for (int i = 0; i < pixels.numPixels(); i++) {
                    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
                }
                pixels.setBrightness(255);
            } else {
                pixels.clear();
            }
            pixels.show();
        } else {
            if (lux > 150) {
                pixels.clear();
                pixels.show();
                neoState = false;
            } else if (lux > 100 && lux <= 150) {
                for (int i = 0; i < pixels.numPixels(); i++) {
                    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
                }
                pixels.setBrightness(32);
                pixels.show();
                neoState = true;
            } else if (lux > 50 && lux <= 100) {
                for (int i = 0; i < pixels.numPixels(); i++) {
                    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
                }
                pixels.setBrightness(128);
                pixels.show();
                neoState = true;
            } else if (lux > 0 && lux <= 50) {
                for (int i = 0; i < pixels.numPixels(); i++) {
                    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
                }
                pixels.setBrightness(255);
                pixels.show();
                neoState = true;
            }
        }
    }
    delay(100);
}

// subscribe

// MQTT 명령에 따른 모터 제어
void msgCB(char* topic, byte* payload, unsigned int length) {
    char msgBuffer[20];
    int i;
    for (i = 0; i < (int)length; i++) {
        msgBuffer[i] = payload[i];
    }
    msgBuffer[i] = '\0';
    Serial.printf("\n%s -> %s", topic, msgBuffer);

    // 온도, 습도, 조도에 따른 제어 (기존과 동일)
    if (!strcmp(topic, "id/SmartFarm/sensor/evt/temperature")) {
        temperature = atoi(msgBuffer);
    } else if (!strcmp(topic, "id/SmartFarm/sensor/evt/humidity")) {
        humidity = atoi(msgBuffer);
    } else if (!strcmp(topic, "id/SmartFarm/sensor/evt/lux")) {
        lux = atoi(msgBuffer);
    } else if (!strcmp(topic, "id/SmartFarm/sensor/evt/soil")) {
        soil = atoi(msgBuffer); 
    }
    // 모터 제어 명령
    else if (!strcmp(topic, "id/SmartFarm/control/cmd/motor")) {
        if (relayUpdateFlag){
            if (forceRelayState == false){
                if (!strcmp(msgBuffer, "on")) {
                    digitalWrite(relaypin, HIGH);
                    Serial.println("\nmotor on");
                    relayState = true;
                } else if (!strcmp(msgBuffer, "off")) {
                    digitalWrite(relaypin, LOW);
                    Serial.println("\nmotor off");
                    relayState = false;
                }
            }
        }
    }
}
