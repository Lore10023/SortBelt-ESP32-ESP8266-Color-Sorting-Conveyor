# 🎨 SortBelt: ESP32 + ESP8266 Color-Sorting Conveyor

A wireless, two-microcontroller conveyor belt that detects colored cubes, classifies them (red / green / blue / unknown), and sorts them with a servo diverter. It comes with a built-in web dashboard for live statistics, system diagnostics, and an emergency stop, with no router or internet connection required.

<!-- Add a photo or GIF of the build here -->
<!-- ![Demo](docs/demo.gif) -->

## ✨ Features

- **Wireless sensor node**: an ESP8266 with a color sensor sends detections to the ESP32 controller over **ESP-NOW** (low-latency, connectionless).
- **Robust color classification**: median filtering of repeated pulse readings, classification by the **R/C (red / clear) ratio** so results don't depend on brightness, and a **majority vote** across multiple readings before a color is accepted.
- **Reliable messaging**: every packet carries a sequence number, so the receiver detects **lost, duplicated, and restarted** messages. A 1 Hz **heartbeat** tells the controller whether the sensor node is alive.
- **Automatic sorting**: a servo diverter routes cubes by color; an IR sensor at the end of the belt confirms that the cube arrived before the sorting sequence runs.
- **Unknown-cube handling**: if a cube can't be classified or no color result arrives within 3 s, the belt reverses for 2.5 s to reject it.
- **Fault detection**: flags a "cube lost between sensors" fault if a cube doesn't reach the IR sensor within 10 s, plus an IR-stuck check and an indirect color-sensor health estimate.
- **Self-hosted web dashboard**: the ESP32 creates its own Wi-Fi access point and serves a responsive (dark-mode aware) page with:
  - total and per-color cube counters
  - response time (IR trigger → servo command): last / average / worst
  - system diagnostics (sensor node, radio link, color sensor, IR sensor, belt/motor, servo)
  - **Emergency Stop** and **Reset** buttons

## 🧠 How it works

```
┌─────────────────────────┐   ESP-NOW (channel 1)   ┌──────────────────────────────┐
│  ESP8266 – Sensor node  │ ──────────────────────► │  ESP32 – Controller          │
│                         │  CUBE_DETECTED          │                              │
│  Color sensor (TCS3200) │  COLOR_RESULT           │  DC motor (belt) via H-bridge│
│  Reads + classifies     │  HEARTBEAT              │  Servo diverter              │
│  cube color             │                         │  IR sensor (end of belt)     │
└─────────────────────────┘                         │  Web server + dashboard      │
                                                    └──────────────┬───────────────┘
                                                                   │ Wi-Fi AP "SorterBot"
                                                                   ▼
                                                        Phone / laptop browser
                                                        (192.168.4.1)
```

**Sorting flow**

1. The sensor node sees a cube under the color sensor (clear-channel reading in range) and sends `CUBE_DETECTED`.
2. The ESP32 **stops the belt** so the cube sits still under the sensor.
3. The sensor node takes several readings, classifies the color, and sends `COLOR_RESULT`.
4. The ESP32 positions the servo according to the color and **restarts the belt**.
5. When the IR sensor at the end of the belt detects the cube, the belt stops and the servo runs the sorting sequence for that color. Response time is measured here.
6. **Unknown** cubes (or a timeout waiting for the result) trigger a timed belt reversal to reject the cube.

## 🔧 Hardware

| Part | Qty | Notes |
|---|---|---|
| ESP32 dev board | 1 | Controller, motor, servo, IR, web server |
| ESP8266 dev board (NodeMCU / Wemos D1 mini) | 1 | Wireless sensor node |
| TCS3200 / TCS230 color sensor | 1 | Frequency output, read with `pulseIn` |
| DC gear motor + conveyor belt | 1 | Belt drive |
| H-bridge motor driver (e.g. L298N) | 1 | `IN1`, `IN2`, `ENA` (PWM speed) |
| Servo motor (SG90 or similar) | 1 | Cube diverter |
| IR obstacle sensor | 1 | Detects the cube at the end of the belt |
| Colored cubes, power supply, wiring | | |

### Pinout

**ESP32 (controller)**

| Function | GPIO |
|---|---|
| Motor `IN1` | 26 |
| Motor `IN2` | 25 |
| Motor `ENA` (PWM) | 33 |
| IR sensor | 27 |
| Servo signal | 18 |

**ESP8266 (sensor node)**

| Function | GPIO |
|---|---|
| Color sensor `S2` | 12 |
| Color sensor `S3` | 14 |
| Color sensor `OUT` | 13 |

## 📁 Repository structure

```
.
├── esp32-controller/
│   ├── esp32-controller.ino     # belt, servo, IR logic, ESP-NOW receiver
│   └── web.h                    # Wi-Fi AP, web server, dashboard (HTML/JS), REST API
├── esp8266-sensor-node/
│   └── esp8266-sensor-node.ino  # color sensing, classification, ESP-NOW sender
├── docs/                        # photos, wiring diagram
└── README.md
```

## 🚀 Getting started

### 1. Libraries (Arduino IDE or PlatformIO)

**ESP32 controller**
- ESP32 Arduino core (3.x. The code uses the new `ledcAttach` API and `esp_now_recv_info_t` callback signature)
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo)
- [AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)

**ESP8266 sensor node**
- ESP8266 Arduino core (includes `ESP8266WiFi` and `espnow`)

### 2. Set the receiver MAC address

Find the ESP32's MAC address (e.g. `WiFi.macAddress()`) and put it into the sensor node sketch:

```cpp
uint8_t receiverMAC[] = { 0x58, 0x2A, 0xBD, 0x80, 0x82, 0xE8 };  // <- your ESP32's MAC
```

### 3. Flash both boards

- Flash `esp32-controller` to the ESP32.
- Flash `esp8266-sensor-node` to the ESP8266.

> Both boards must use the **same Wi-Fi channel (1)**. The ESP32 sets it when it starts its access point, and the ESP8266 sets it in `setup()`.

### 4. Open the dashboard

1. Power the system.
2. Connect your phone or laptop to the Wi-Fi network **`SorterBot`** (change the default password in `web.h` before real use).
3. Open **http://192.168.4.1**.

## 🌐 REST API

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/stats` | Counters, response times, e-stop state, node status |
| `GET` | `/api/diag` | Per-subsystem diagnostics (status code + text) |
| `POST` | `/api/estop` | Emergency stop: halts the belt and clears pending events |
| `POST` | `/api/reset` | Clears the stop and restarts the belt |

## ⚙️ Tuning & calibration

- **Color thresholds**: in `clasify_colors()`, colors are decided by the R/C ratio (blue ≈ 1.9–2.6, green ≈ 1.0–1.3, red ≈ 0.4–0.6). Re-measure these for your sensor, cube material, and lighting.
- **Cube-present threshold**: the clear-channel pulse width (`> 400` means an empty belt) depends on sensor distance.
- **Belt speed**: `speedValue` in the controller sketch. Low values may not overcome motor stiction.
- **Timing**: `REVERSE_TIME`, `REVERSE_TIME_GREEN`, and the 3 s color-result timeout can be adjusted to your belt length and speed.

## 🗺️ Possible improvements

- Non-blocking servo and reverse sequences (replace `delay()` with a state machine) for better responsiveness
- Auto-calibration routine for color thresholds
- Persistent statistics and CSV export from the dashboard
- Authenticated / secured control endpoints
- Servo position feedback and encoder-based belt tracking

## 🛠️ Tech stack

`C++` · `Arduino framework` · `ESP32` · `ESP8266` · `ESP-NOW` · `AsyncWebServer` · `HTML/CSS/JavaScript` · `REST API` · `PWM motor control` · `TCS3200`

## 📄 License

MIT. See [LICENSE](LICENSE).
