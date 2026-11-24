# ESP32 Smart Room Monitoring & Gas Detection System

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v4.4.7-blue.svg)](https://github.com/espressif/esp-idf)
[![Web Flasher](https://img.shields.io/badge/Web%20Flasher-Live%20Online-brightgreen.svg)](https://redihat.github.io/esp32-smart-room-monitoring/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

A complete IoT environmental monitoring and gas safety detection system built on the **ESP32 DevKit V1 (ESP-WROOM-32)** using native **ESP-IDF v4.4.7 in C**.

---

## ⚡ Instant Browser Web Flasher

You can flash this firmware directly to your ESP32 in **Google Chrome** or **Microsoft Edge** without installing Python, Git, or drivers:

👉 **[Launch Web Flasher (GitHub Pages)](https://redihat.github.io/esp32-smart-room-monitoring/)**

---

## 🛠️ Hardware Requirements

- **ESP32 DevKit V1** (ESP-WROOM-32)
- **DHT11** Temperature & Humidity Sensor
- **MQ-2** Gas Sensor Module (Analog Output `AO`)
- **22 kΩ** Resistor ($R_1$)
- **10 kΩ** Resistor ($R_2$)
- Breadboard & Jumper wires

---

## 🔌 Circuit & Pinout Diagram

| Component | Pin | ESP32 Pin | Wiring Notes |
| :--- | :---: | :---: | :--- |
| **DHT11** | VCC | **3V3** | 3.3V Power |
| **DHT11** | DATA | **D4 (GPIO 4)** | 1-Wire bidirectional digital data |
| **DHT11** | GND | **GND** | Common ground |
| **MQ-2** | VCC | **VIN (5V)** | Heating element requires 5V |
| **MQ-2** | GND | **GND** | Common ground |
| **MQ-2** | AO | **Junction** | Connects to 22 kΩ resistor |
| **Junction** | — | **D34 (GPIO 34)** | ADC1 Channel 6 |
| **Junction** | — | **GND** | Connects to 10 kΩ pulldown resistor |
| **MQ-2** | DO | — | Not connected |

### Why the Voltage Divider is Mandatory
The ESP32 ADC pin cannot tolerate 5V. The $22\text{ k}\Omega + 10\text{ k}\Omega$ voltage divider scales the 0–5V MQ-2 output down to a safe $0\text{ V} - 1.56\text{ V}$:
$$V_{\text{junction}} = V_{\text{AO}} \times \frac{10}{22 + 10} = V_{\text{AO}} \times 0.3125$$
In the firmware, the sensor AO voltage is reconstructed by multiplying by $3.2\times$.

---

## 🌐 Local Web Dashboard

The ESP32 runs its own embedded HTTP web server on port 80:
- Access via: `http://<ESP32-LOCAL-IP>/`
- Responsive cards for **Temperature (°C)**, **Humidity (%)**, **MQ-2 Raw ADC**, and **AO Voltage (mV)**.
- Live animated status banner: **NORMAL** (Green) or **GAS WARNING** (Pulsing Red).
- Real-time auto-refresh every 2.5 seconds using non-blocking AJAX (`/api/data`).

---

## 📱 Blynk IoT Cloud Integration

Syncs live sensor telemetry to the **Blynk mobile app** every 10 seconds:

| Virtual Pin | Datastream Name | Data Type | Range |
| :---: | :--- | :---: | :---: |
| **V0** | Temperature | Double | `-10` to `60 °C` |
| **V1** | Humidity | Double | `0` to `100 %` |
| **V2** | Gas Raw ADC | Integer | `0` to `4095` |
| **V3** | Gas Alert Status | Integer | `0` (Normal) or `1` (Alert) |
| **V4** | Sensor AO Voltage | Integer | `0` to `5000 mV` |

---

## 💻 Building and Flashing from Source

### Option 1: Native VS Code (ESP-IDF Extension)
1. Open [`smart_home_monitoring.code-workspace`](smart_home_monitoring.code-workspace) in VS Code.
2. Press `Ctrl + Shift + B` to build, flash, and open the serial monitor automatically!

### Option 2: 1-Click Batch Script
Double-click [`Flash_And_Run_ESP32.bat`](Flash_And_Run_ESP32.bat) to flash `smart_home_monitoring_merged_0x0.bin` at offset `0x0` and launch the serial terminal.

---

## 📜 License
MIT License. Created by [@REDIHAT](https://github.com/REDIHAT).
