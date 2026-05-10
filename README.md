# 🚗 Ultra-Low Power BLE OBD2 Dashboard (RAK4631)

This project turns a **RAK4631 (nRF52840)** into a standalone, ultra-efficient wireless dashboard that connects to a BLE ELM327 OBD2 adapter. It automatically fetches and displays your vehicle's engine coolant temperature on a 128x32 OLED screen. Designed specifically for **solar-powered / energy-harvesting** setups in vehicles, it achieves a deep sleep consumption of just **~280µA**.

![Cover Image Placeholder](/readme_assets/thumbnail.jpg)

## ✨ Main Features

* **Energy Harvesting Ready (~280µA Sleep):** Achieves extreme low power by physically putting the OLED and I2C bus to sleep, and aggressively hard-killing the unused SX1262 LoRa chip, TCXO, and RF antenna switch via direct GPIO manipulation.
* **Silent Probing (Smart Engine Detection):** Wakes up every 30 seconds to connect to the OBD2 adapter, but keeps the screen OFF. It requests engine RPMs (PID `01 0C`); if the engine is off (0 RPM), it instantly disconnects and sleeps, avoiding battery drain.
* **Zombie Connection Watchdogs:** Includes multiple software safeguards to auto-heal the MCU. Features a 45-second data timeout, I2C bus rescue, and RAM overflow protection to survive ELM327 hangups or corrupted packets without needing physical resets.
* **Custom BLE UART Parsing:** Bypasses standard GATT limitations to read generic ELM327 BLE adapters that use custom TX/RX UUIDs (`0xFFF0` service).

---

## 🛒 Required Materials

To build this project exactly as configured, you will need the following hardware:

1. **Microcontroller (MCU):** [WisBlock Core RAK4631](https://store.rakwireless.com/products/rak4631-lpwan-node) (nRF52840 + SX1262 LoRa).
2. **Base Board:** [WisBlock Base Board 2nd Gen (RAK19007)](https://store.rakwireless.com/products/rak19007-wisblock-base-board-2nd-gen) or any compatible WisBlock base.
3. **Display:** [0.91" I2C OLED Display 128x32 SSD1306](https://amzn.to/49n3jL8).
4. **OBD2 Adapter:** [ELM327 BLE OBD2 Adapter](https://amzn.to/4uIwNeU) *(Crucial: It MUST be a Bluetooth Low Energy (BLE) model, usually advertised as iOS/Apple compatible. Standard Bluetooth Classic models will not work with the nRF52840).*
5. **Power Source:** A standard [3.7V LiPo Battery (e.g., 820mAh)](https://amzn.to/4u937aW) and an optional 5V Solar Panel for continuous energy harvesting.

---

## 📁 Repository Structure

* `/ble_temp_monitor_fw` - Contains the main `.ino` sketch for the RAK4631.
* `/power_profile` - Power consumption graphs (Otii Ace Pro profiles).
* `/3D_enclosure` - 3D files for printing the device enclosure.
* `README.md` - This documentation file.

---

## 🛠️ Software Prerequisites

Before compiling, ensure you have the following installed in your Arduino IDE:

1. **Board Support Package (BSP):** [RAKwireless nRF Boards](https://github.com/RAKWireless/RAKwireless-Arduino-BSP-Index) installed via the Boards Manager.
2. **Libraries:**
   * `Adafruit Bluefruit nRF52` (For BLE Central operations).
   * `U8g2` by olikraus (For driving the SSD1306 OLED display).

---

## 🚀 Installation & Setup Guide

**Step 1:** Assemble your hardware. Connect the OLED screen to the I2C pins of your WisBlock Base board (SDA and SCL). 

**Step 2:** Clone this repository and open the sketch in the Arduino IDE.

**Step 3:** Compile and flash the code to your RAK4631. 
* *Note: The code defaults to searching for a BLE device exposing the `0xFFF0` service UUID. If your specific ELM327 uses different UUIDs for its UART service, update the `obdService`, `obdNotifyChar`, and `obdWriteChar` variables at the top of the sketch.*

**Step 4:** Plug the ELM327 BLE adapter into your car's OBD2 port. The monitor will automatically connect with the ELM327 and start showing the temperature.

![Monitor working](/readme_assets/monitor.jpg)

---

### ⚠️ IMPORTANT: Solar Panel Voltage Limit
If you are connecting a solar panel directly to the WisBlock Base Board, **DO NOT exceed 5.5V**. While the TP4054 charging chip supports up to 6.5V, the board utilizes a 5.6V Zener diode for over-voltage protection. Connecting a panel with a higher output (e.g., 6V+) will fry the Zener diode instantly.

---

## 🔋 Power States Architecture & Energy Consumption

The code is architected around strict "Duty Cycles" to maximize battery life. Based on precision profiling using the Qoitech Otii Ace Pro, the system exhibits the following power footprint:

* **Active Mode (14.2 mA):** When the engine is ON (RPM > 0). The OLED is powered, displaying real-time data, and the BLE radio requests updates every 10 seconds.
* **Scanning / Silent Probing (4.47 mA for 2.3s):** Wakes up every 30 seconds when disconnected. It briefly turns on the BLE scanner for 2.3 seconds to check for the ELM327. If the car is found but the engine is OFF (0 RPM), it rapid-fires 3 RPM requests and aborts.
* **Deep Sleep Mode (286 µA):** The baseline consumption between the 30-second scans. Bluetooth radio is stopped, OLED is placed in `PowerSave(1)`, I2C peripheral (TWI) is disabled, and SDA/SCL pins are set to high impedance. The MCU sleeps using FreeRTOS `System ON Idle`.

### Real-World Battery Estimation

Using a standard **820mAh LiPo battery** without any solar panel input (complete darkness), the battery life is estimated based on a typical daily commute profile:

* **2 hours of driving** (Active Mode @ 14.2 mA)
* **22 hours parked** (Deep Sleep @ 286 µA + periodic Scanning bursts @ 4.47 mA)

Under these conditions, the dashboard will run autonomously for **23.7 days** before fully depleting the battery. With the addition of a 5V solar panel catching daily sunlight, the system replenishes the consumed energy, achieving a theoretically infinite energy-harvesting loop.

![Power Profile](/power_profile/sleeping-current.png)