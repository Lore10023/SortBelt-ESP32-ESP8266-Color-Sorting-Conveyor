#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include "web.h"

Servo myServo;

//DC Motor pins
#define IN1_PIN 26
#define IN2_PIN 25
#define ENA_PIN 33

//IR Sensor pin
#define IR_PIN 27
int SensorState = 0;
int LastSensorState = 0;

const int PWM_FREQ = 500;   // Hz
const int PWM_BITS = 8;      // 0-255
int speedValue = 130;        // 0 = stop, 255 = full speed (<=120 - the motor won't start, it will have a sound)  

// Same definition as on the ESP8266
typedef struct{
  uint8_t type;
  uint8_t color;
  uint32_t seq;
  uint32_t t_ms;
} Message;

//Green cube reversing
const unsigned long REVERSE_TIME_GREEN = 300;

//unknown cube resolving case
bool reversing = false;
unsigned long reverseStart = 0;
const unsigned long REVERSE_TIME = 2500;
uint32_t cntRejected = 0;   

bool wasStopped = false;

volatile bool cubeSensor = false;
volatile bool colorSensor = false;
unsigned long stopTime = 0;
bool beltStopped = false;
volatile bool cubeOnBelt = false;
unsigned long cubeOnBeltSince = 0;

enum { CUBE_DETECTED = 0, COLOR_RESULT = 1, HEARTBEAT = 2 };
enum { RED = 0, GREEN = 1, BLUE = 2, UNKNOWN = 3 };

uint32_t lastSeq = 0;
volatile uint8_t lastColor = UNKNOWN;

const char* colorName(uint8_t c) {
  switch (c) {
    case RED:     return "RED";
    case GREEN:   return "GREEN";
    case BLUE:    return "BLUE";
    case UNKNOWN: return "UNKNOWN";
    default:      return "?";
  }
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(Message)) {
    Serial.printf("Wrong size: %d\n", len);
    return;
  }
  lastHeartbeatMs = millis();

  Message m;
  memcpy(&m, data, sizeof(m));

  // Detect lost or duplicated messages
  int32_t diff = (int32_t)(m.seq - lastSeq);
  if (diff == 0) return;                       // duplicate
  if (diff < 0) Serial.println("Sender restarted, resyncing");
  else if (diff > 1 && lastSeq != 0) Serial.printf("Lost %ld message(s)\n", (long)(diff - 1));
  rxTotal++;
  if (diff > 1 && lastSeq != 0) rxLost += (diff - 1);
  lastSeq = m.seq;

  switch (m.type) {
    case HEARTBEAT:
      //Serial.printf("HEARTBEAT  seq=%lu\n", (unsigned long)m.seq);
      break;
    case CUBE_DETECTED:
      Serial.printf("CUBE_DETECTED  seq=%lu\n", (unsigned long)m.seq);
      cubeSensor = true;
      break;
    case COLOR_RESULT:
      Serial.printf("COLOR_RESULT  color=%s  seq=%lu\n", colorName(m.color), (unsigned long)m.seq);
      lastColor = m.color;
      colorSensor = true;
      break;
    default:
      Serial.printf("Unknown type %u\n", m.type);
  }
}

void startReverse() {
  ledcWrite(ENA_PIN, 0);
  delay(150);
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, HIGH);
  ledcWrite(ENA_PIN, speedValue);
  reversing = true;
  reverseStart = millis();
  cntRejected++;
  isReversing = true;
  Serial.println("UNKNOWN cube: reversing belt");
}

void endReverse() {
  ledcWrite(ENA_PIN, 0);
  delay(150);
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  ledcWrite(ENA_PIN, speedValue);
  reversing = false;
  isReversing = false;
}

void moveServo(int a) {
  myServo.write(a);
  lastServoAngle = a;
}

void setup() {
  Serial.begin(115200);

  //Servo Motor pin
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  myServo.setPeriodHertz(50);
  myServo.attach(18, 500, 2400);
  moveServo(90);

  //DC Motor pins
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  ledcAttach(ENA_PIN, PWM_FREQ, PWM_BITS);
  //start the DC Motor
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  ledcWrite(ENA_PIN, speedValue);

  //IR Sensor pin
  pinMode(IR_PIN, INPUT);
  lastIrChangeMs = millis();

  //Communication with the esp8266 setup
  //WiFi.mode(WIFI_STA);
  //esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);   // must match the ESP8266

  //Communication + web (set the channle to 1 and the web server)
  setupWeb();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onReceive);
  Serial.println("Receiver ready");

  //the first pass doesn’t see a false change
  LastSensorState = digitalRead(IR_PIN);
}

void loop() {

  if (emergencyStop) {
    ledcWrite(ENA_PIN, 0);
    cubeSensor = false;
    colorSensor = false;
    cubeOnBelt = false;
    reversing = false;
    beltStopped = false;
    wasStopped = true;
    return;
  }

  if (wasStopped) {
    wasStopped = false;
    digitalWrite(IN1_PIN, HIGH);
    digitalWrite(IN2_PIN, LOW);
    ledcWrite(ENA_PIN, speedValue);
    moveServo(90);
    LastSensorState = digitalRead(IR_PIN);
    Serial.println("Reset: belt restarted");
    beltFault = false;
  }

  if (reversing) {
    cubeSensor = false;
    colorSensor = false;
    if (millis() - reverseStart > REVERSE_TIME) endReverse();
    return;
  }

  if (cubeOnBelt && millis() - cubeOnBeltSince > 10000) {
    beltFault = true;      // cube never reached the IR
    cubeOnBelt = false;
  }

  //stop the belt when the cube is under the color sensor, and start the belt when esp32 receive the color of the cube
  if(cubeSensor){
    cubeSensor = false;
    ledcWrite(ENA_PIN, 0); 
    beltStopped = true;
    stopTime = millis();
  }
  if(colorSensor){
    if(lastColor == RED){
      moveServo(0);
      delay(1000);
      cubeOnBelt = true;
      cubeOnBeltSince = millis();
      ledcWrite(ENA_PIN, speedValue);
      cntRed++;
      totalCubes++;
    }else if(lastColor == GREEN){
      moveServo(180);
      delay(1000);
      cubeOnBelt = true;
      cubeOnBeltSince = millis();
      ledcWrite(ENA_PIN, speedValue);
      cntGreen++;
      totalCubes++;
    }else if(lastColor == UNKNOWN){
      moveServo(90);
      startReverse();
      cntUnknown++;
      totalCubes++;
    }else{
      moveServo(0);
      ledcWrite(ENA_PIN, speedValue);
      cntBlue++;
      totalCubes++;
    }
    colorSensor = false; 
    beltStopped = false;
  }
  if (beltStopped && millis() - stopTime > 3000) {   // timeout
    //ledcWrite(ENA_PIN, speedValue);
    beltStopped = false;
    moveServo(90);
    cntUnknown++;
    totalCubes++;
    startReverse();
  }

  //stop the belt when the cube is in front of the IR sensor
  SensorState = digitalRead(IR_PIN);
  if (SensorState != LastSensorState) {
    lastIrChangeMs = millis();
    irLevel = SensorState;
  }
  if(cubeOnBelt && SensorState != LastSensorState && SensorState == LOW){
    Serial.println("IR DETECTED A CUBE");
    unsigned long t0 = micros();
    ledcWrite(ENA_PIN, 0); 
    cubeOnBelt = false;
    beltFault = false;
    if(lastColor == RED){
      moveServo(45);
      delay(500);
      moveServo(90);
      delay(500);
      moveServo(120);
      delay(500);
      moveServo(180);
      delay(1000);
      moveServo(0);
      delay(1000);
    }else if (lastColor == GREEN){
      ledcWrite(ENA_PIN, 0);
      delay(150);
      digitalWrite(IN1_PIN, LOW);
      digitalWrite(IN2_PIN, HIGH);
      ledcWrite(ENA_PIN, speedValue);
      isReversing = true;
      delay(REVERSE_TIME_GREEN);

      ledcWrite(ENA_PIN, 0);
      delay(150);
      digitalWrite(IN1_PIN, HIGH);
      digitalWrite(IN2_PIN, LOW);
      isReversing = false;

      moveServo(120);
      delay(500);
      moveServo(90);
      delay(500);
      moveServo(45);
      delay(500);
      moveServo(0);
      delay(1000);
      moveServo(180);
      delay(1000);
    }

    float resp = (micros() - t0) / 1000.0;
    lastResp = resp;
    sumResp += resp;
    respCount++;
    if(resp>maxResp){
      maxResp = resp;
    }

    ledcWrite(ENA_PIN, speedValue);
  }
  LastSensorState = SensorState;
}
