#include <ESP8266WiFi.h>
#include <espnow.h>
#include <user_interface.h>

const int S3 = 14;
const int S2 = 12;
const int OUT = 13;

const int nr_channel = 9;
const unsigned long time_out = 30000;
uint32_t seqContor = 0;
unsigned long lastHeartbeat = 0;

enum { RED = 0,
       GREEN = 1,
       BLUE = 2,
       UNKNOWN = 3,
       NOT_READY = 4,
       EMPTY = 5 };
enum { CUBE_DETECTED = 0,
       COLOR_RESULT = 1,
       HEARTBEAT = 2 };

uint8_t receiverMAC[] = { 0x58, 0x2A, 0xBD, 0x80, 0x82, 0xE8 };

typedef struct {
  uint8_t type;
  uint8_t color;
  uint32_t seq;
  uint32_t t_ms;
} Message;

Message esp8266Data;

//Read 8 pulses per channel so the output will be more stable and returns the average (average)
unsigned long readChannel_average(bool s3, bool s2) {
  int valid = 0;
  unsigned long sum = 0;

  digitalWrite(S3, s3);
  digitalWrite(S2, s2);
  delay(10);

  for (int i = 1; i <= nr_channel; i++) {
    unsigned long p = pulseIn(OUT, LOW, time_out);
    if (p > 0) {
      sum = sum + p;
      valid++;
    }
  }

  return valid ? sum / valid : 0;
}

unsigned long insertionSortAndGetMedian(unsigned long v[], int n) {
  for (int i = 1; i < n; ++i) {
    unsigned long key = v[i];
    int j = i - 1;

    while (j >= 0 && v[j] > key) {
      v[j + 1] = v[j];
      j = j - 1;
    }
    v[j + 1] = key;
  }
  return v[n / 2];
}

//Read 9 pulses per channel in an array and than it sorts them an return the middle one so the output will be more stable (median)
//More robust than average (ignores the extreme values)
unsigned long readChannel_median(bool s3, bool s2) {
  digitalWrite(S3, s3);
  digitalWrite(S2, s2);
  delay(10);

  unsigned long v[nr_channel];
  int n = 0;
  for (int i = 0; i < nr_channel; i++) {
    unsigned long p = pulseIn(OUT, LOW, time_out);
    if (p > 0) {
      v[n++] = p;
    }
  }

  if (n < nr_channel / 2) return 0;

  return insertionSortAndGetMedian(v, n);
}

//clasify the cubes colors by measuring the pulses based on the value from r/c because it is a more stable number
int clasify_colors(unsigned long R, unsigned long C) {
  float ratio = (float)R / C;
  Serial.println(ratio);
  if (1.9 < ratio && ratio < 2.6) {
    return BLUE;
  } else if (1.0 < ratio && ratio < 1.3) {
    return GREEN;
  } else if (0.4 < ratio && ratio < 0.6) {
    return RED;
  } else {
    return UNKNOWN;
  }
}

// this function is an agreement of 2-3 readings, it fixes the issue as a single bad reading being accepted as a color
int stablecolor() {
  delay(150);

  int votes[4] = { 0, 0, 0, 0 };
  int empty = 0;

  for (int i = 0; i < 3; i++) {
    unsigned long r = readChannel_median(LOW, LOW);
    unsigned long g = readChannel_median(HIGH, HIGH);
    unsigned long b = readChannel_median(HIGH, LOW);
    unsigned long c = readChannel_median(LOW, HIGH);
    //Serial.println(c);

    if (r && g && b && c) {
      if (c > 400) {
        empty++;
      } else {
        votes[clasify_colors(r, c)]++;
      }
    } else {
      Serial.println("NO SIGNAL");
    }
    delay(30);
  }

  if (empty >= 2) return EMPTY;
  for (int i = 0; i < 4; i++) {
    if (votes[i] >= 2) return i;
  }
  return NOT_READY;
}

void sendMessage(uint8_t type, uint8_t color) {
  esp8266Data.type = type;
  esp8266Data.color = color;
  esp8266Data.seq = ++seqContor;
  esp8266Data.t_ms = millis();
  esp_now_send(receiverMAC, (uint8_t *)&esp8266Data, sizeof(esp8266Data));
}

void sendHeartBeat() {
  if (millis() - lastHeartbeat >= 1000) {
    lastHeartbeat = millis();
    sendMessage(HEARTBEAT, 0);
  }
}

void onSent(uint8_t *mac, uint8_t status) {
  //if(status != 0){
  //Serial.println("Send Failed");
  //}
  Serial.println(status == 0 ? "Sent OK" : "Send Failed");
}

void setup() {
  Serial.begin(115200);
  wifi_set_channel(1);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(OUT, INPUT);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != 0) {
    Serial.println("ESP NOW INIT FAILED");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onSent);
  esp_now_add_peer(receiverMAC, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
}

void loop() {
  sendHeartBeat();

  unsigned long clear = readChannel_median(LOW, HIGH);
  //Serial.print("Clear: "); Serial.println(clear);
  if (clear == 0 || clear > 400) {
    return;
  }

  sendMessage(CUBE_DETECTED, 0);

  int result = NOT_READY;

  for (int i = 0; i < 3 && result == NOT_READY; i++) {
    result = stablecolor();
  }
  if (result == EMPTY) {
    //Serial.println("EMPTY BAND");
    return;
  } else if (result == NOT_READY) {
    result = UNKNOWN;
    Serial.println("UNKNOWN (no agreement)");
  } else if (result == RED) {
    Serial.println("RED");
  } else if (result == BLUE) {
    Serial.println("BLUE");
  } else if (result == GREEN) {
    Serial.println("GREEN");
  } else if (result == UNKNOWN) {
    Serial.println("UNKNOWN");
  }

  sendMessage(COLOR_RESULT, result);
  //delay(2000);

  unsigned long t = millis();
  while (millis() - t < 2000) {
    sendHeartBeat();
    delay(10);
  }
}
