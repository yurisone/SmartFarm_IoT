// 토양습도센서 data get 후 펌프 제어

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ConfigPortal32.h>

char*               ssid_pfix = (char*)"SmartFarmPump";
String              user_config_html = ""
    "<p><input type='text' name='broker' placeholder='MQTT Server'>";

char                mqttServer[100];
const int           mqttPort = 1883;
void msgCB(char* topic, byte* payload, unsigned int length); // message Callback


// 펌프 버튼 핀
#define SW3 44

WiFiClient wifiClient;
PubSubClient client(wifiClient);
WebServer server(80);

TFT_eSPI tft = TFT_eSPI();

char                buf_soil[50];

float soil = 0;

#define pumppin 43   // 워터펌프 릴레이
bool pumpState = false;

//펌프 끄기 켜기 인터럽트
volatile bool pumpUpdateFlag = true;  // 펌프 상태 강제 제어 플래그
volatile bool forcePumpState = false;  // 버튼으로 강제 제어 중인지 여부
volatile unsigned long pumpTimer = 0;  // 강제 제어 시작 시간
const unsigned long pumpForceDuration = 10000; // 10초 (10000 ms)

// 펌프 제어 인터럽트
IRAM_ATTR void setPump() {
    //현재 상태 반전
    pumpState = !pumpState;

    if (pumpState){
        digitalWrite(pumppin, HIGH);
        Serial.println("pump on");
    }
    else{
        digitalWrite(pumppin, LOW);
        Serial.println("pump off"); 
    }
    //강제 제어 시작
    forcePumpState = true;
    pumpTimer = millis();
    pumpUpdateFlag = true;
}

// 웹 페이지 표시
void handleRoot() {
    char html[1024];
    char templateHtml[] =
        "<html><head><meta charset=\"utf-8\">"
        "<title>제어 및 모니터링</title></head>"
        "<body><center>"
        "<h1>워터펌프 제어</h1>"
        "<h2>토양 수분 값: %d</h2>"
        "<h3>워터펌프 상태: %s</h3>"
        "<form action='/pump' method='GET'>"
        "<button name='state' value='on' type='submit' style='font-size:16px;'>펌프 켜기</button>"
        "<button name='state' value='off' type='submit' style='font-size:16px;'>펌프 끄기</button>"
        "</form>"
        "</center></body></html>";

    sprintf(html, templateHtml, soil, pumpState ? "ON" : "OFF");
    server.send(200, "text/html", html);
}


// 릴레이 펌프제어
void handlePump() {
    if (server.hasArg("state")) {
        String state = server.arg("state");
        if (state == "on") {
            digitalWrite(pumppin, HIGH);
            pumpState = true;
        } else if (state == "off") {
            digitalWrite(pumppin, LOW);
            pumpState = false;
        }
        //강제 제어 시작
        forcePumpState = true;
        pumpTimer = millis();
        pumpUpdateFlag = true;
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
    MDNS.begin("SmartFarmPumpWeb");
    // 웹 서버 경로 설정
    server.on("/", handleRoot);         // 메인 페이지
    server.on("/pump", handlePump);     // 펌프 제어
    server.onNotFound(handleNotFound);  // 404 에러 처리
    server.begin();
    Serial.println("HTTP server start");

    pinMode(SW3, INPUT_PULLUP);
    pinMode(pumppin, OUTPUT);
    digitalWrite(pumppin, LOW);
    attachInterrupt(SW3, setPump, FALLING);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Status", 80, 20, 4);
    tft.drawString("soil : ", 10, 150, 2);
    tft.drawString("Pump : ", 140, 120, 2);
    tft.drawLine(120, 60, 120, 170, TFT_WHITE);
    Serial.printf("\nIP address : "); Serial.println(WiFi.localIP());

    if (cfg.containsKey("broker")) {
            sprintf(mqttServer, (const char*)cfg["broker"]);
    }
    client.setServer(mqttServer, mqttPort);
    client.setCallback(msgCB);

    while (!client.connected()) {
        Serial.println("Connecting to MQTT...");
        if (client.connect("Smart Farm Pump")) {
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

    sprintf(buf_soil, "%.1f", soil); 
    tft.fillRect(60, 150, 60, 40, TFT_BLACK);
    tft.drawString(buf_soil, 60, 150, 2);

    
    if (pumpState == false){
        tft.fillRect(200, 120, 40, 20, TFT_BLACK);
        tft.drawString("Off", 200, 120, 2);
    } else{
        tft.fillRect(200, 120, 40, 20, TFT_BLACK);
        tft.drawString("On", 200, 120, 2); 
    } 

    
    // pump 강제 제어 상태 유지
    if (forcePumpState){
       if (millis() - pumpTimer >= pumpForceDuration) {
            // 강제 제어 종료
            forcePumpState = false;
            pumpUpdateFlag = true; // MQTT 값에 따라 업데이트
        } 
    }
    delay(100);
}

// subscribe

// MQTT 명령에 따른 펌프 제어
void msgCB(char* topic, byte* payload, unsigned int length) {
    char msgBuffer[20];
    int i;
    for (i = 0; i < (int)length; i++) {
        msgBuffer[i] = payload[i];
    }
    msgBuffer[i] = '\0';
    Serial.printf("\n%s -> %s", topic, msgBuffer);

    // 토양 습도를 통한 펌프 제어
    if (!strcmp(topic, "id/SmartFarm/sensor/evt/soil")) {
        soil = atoi(msgBuffer); 
    }
    
    // 펌프 제어 명령
    else if (!strcmp(topic, "id/SmartFarm/control/cmd/pump")) {
        if (pumpUpdateFlag){
            if (forcePumpState == false){
                if (!strcmp(msgBuffer, "on")) {
                    digitalWrite(pumppin, HIGH);
                    Serial.println("\npump on");
                    pumpState = true;
                } else if (!strcmp(msgBuffer, "off")) {
                    digitalWrite(pumppin, LOW);
                    Serial.println("\npump off");
                    pumpState = false;
                }
            }
        }
    } 
}
