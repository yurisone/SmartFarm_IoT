import sys
import requests
import paho.mqtt.client as mqtt

topic1 = "id/SmartFarm/sensor/evt/#"
server = "localhost"
token="IO3TjNzNuMSXSz0LmzTRer1_7u6q2MHgVzkRL_ndc18RTpqKY7FtPaaFWtvcgetmz7WEfU4duOYnz8rZ1CcMDg=="
headers={
    "Authorization" : f"Token {token}"
}
temp_low = 20; temp_high = 30
humi_low = 30; humi_high = 70
soil_low = 700; soil_high = 3000

def on_connect(client, userdata, flags, rc):
    print("Connected with RC : " + str(rc))
    client.subscribe(topic1)

URL = f"http://127.0.0.1:8086/write?db=bucket01"

def on_message(client, userdata, msg):
    global temp_low, temp_high, humi_low, humi_high  # 전역 변수 선언
    value = float(msg.payload.decode('utf-8'))
    key = msg.topic.split('/')[4]
    if msg.topic.split('/')[2] == "sensor":
        d = f"ambient,location=room3 {key}={value}"
        r = requests.post(url=URL, data=d, headers=headers)
        print(f'rc : {r} for {msg.topic:38} {value: >5.1f}')
        if key == "temperature":
            if value > temp_high:
                client.publish("id/SmartFarm/control/cmd/motor", "on")
            elif value < temp_low:
                client.publish("id/SmartFarm/control/cmd/motor", "off")
        elif key == "humidity":
            if value > humi_high:
                client.publish("id/SmartFarm/control/cmd/motor", "on")
            elif value < humi_low:
                client.publish("id/SmartFarm/control/cmd/motor", "off")
        elif key == "soil":
            if value > soil_high:
                client.publish("id/SmartFarm/control/cmd/pump", "on")
            elif value < soil_low:
                client.publish("id/SmartFarm/control/cmd/pump", "off") 


client = mqtt.Client()
client.connect(server, 1883, 60)
client.on_connect = on_connect
client.on_message = on_message

client.loop_forever()
