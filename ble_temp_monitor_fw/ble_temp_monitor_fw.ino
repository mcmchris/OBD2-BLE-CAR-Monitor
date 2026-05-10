#include <bluefruit.h>
#include <U8g2lib.h>
#include <Wire.h>

BLEClientService        obdService(0xFFF0);
BLEClientCharacteristic obdNotifyChar(0xFFF1);
BLEClientCharacteristic obdWriteChar(0xFFF2);

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0);

String rxBuffer = "";
uint32_t lastRequestTime = 0;
bool requestingTemp = false; 
int currentRpm = 0;
char displayStr[32] = {0}; 
uint16_t connection_handle = BLE_CONN_HANDLE_INVALID;

// Power management and watchdog variables
bool isScreenOn = false;
int silentCheckCount = 0; 
uint32_t lastDataReceivedTime = 0; 

void bt_init() {
  Bluefruit.autoConnLed(false);
  Bluefruit.begin(0, 1);
  Bluefruit.setName("RAK_Dash");
  obdService.begin();
  obdNotifyChar.setNotifyCallback(obd_notify_callback);
  obdNotifyChar.begin();
  obdWriteChar.begin();

  Bluefruit.Central.setDisconnectCallback(disconnect_callback);
  Bluefruit.Central.setConnectCallback(connect_callback);
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.filterUuid(obdService.uuid);
  Bluefruit.Scanner.setInterval(3200, 200); 
  Bluefruit.Scanner.start(0); 
}

void setup(void) {

  // === Hardware Optimization: Power down SX1262 LoRa chip ===
  int PIN_LORA_NSS = 42;   // P1.10
  int PIN_LORA_RESET = 38; // P1.06
  int PIN_ANT_SW = 39;     // P1.07 (Antenna switch)

  // 1. Set NSS high to disable SPI
  pinMode(PIN_LORA_NSS, OUTPUT);
  digitalWrite(PIN_LORA_NSS, HIGH);
  
  // 2. Hold LoRa RESET low to force complete shutdown
  pinMode(PIN_LORA_RESET, OUTPUT);
  digitalWrite(PIN_LORA_RESET, LOW);

  // 3. Ensure RF antenna switch has no ground leakage
  pinMode(PIN_ANT_SW, OUTPUT);
  digitalWrite(PIN_ANT_SW, LOW);

  // Turn off onboard LEDs to save power
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);
  
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  // ==========================================================

  // I2C Timeout to prevent MCU lockup if OLED fails
  Wire.setClock(100000); 
  Wire.setTimeout(1000); // 1 second max wait on I2C bus
  Wire.begin();
  
  u8g2.begin();
  u8g2.setPowerSave(1); 
  u8g2.setFont(u8g2_font_fub30_tf);
  bt_init();
}

void scan_callback(ble_gap_evt_adv_report_t* report) {
  Bluefruit.Central.connect(report);
}

void connect_callback(uint16_t conn_handle) {
  if (!obdService.discover(conn_handle) || !obdNotifyChar.discover() || !obdWriteChar.discover()) {
    Bluefruit.disconnect(conn_handle);
    return;
  }
  if (obdNotifyChar.enableNotify()) {
    connection_handle = conn_handle; 
    silentCheckCount = 0; 
    lastRequestTime = 0; 
    
    // Reset data watchdog on connection
    lastDataReceivedTime = millis(); 
  }
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  u8g2.setPowerSave(1);
  isScreenOn = false;
  connection_handle = BLE_CONN_HANDLE_INVALID; 
  currentRpm = 0;
  rxBuffer = ""; // Clear buffer on disconnect
}

void loop() {
  if (Bluefruit.Central.connected()) {
    uint32_t now = millis();
    
    // Zombie Connection Watchdog
    // Force disconnect if no valid data is received for 45 seconds
    if (now - lastDataReceivedTime > 45000) {
      if (connection_handle != BLE_CONN_HANDLE_INVALID) {
          Bluefruit.disconnect(connection_handle);
      }
      return; // Exit loop to process disconnection
    }

    // --- SILENT PROBING MODE (Engine assumed off) ---
    if (currentRpm == 0) {
      if (now - lastRequestTime > 3000) {
        obdWriteChar.write("010C\r", 5); 
        lastRequestTime = now;
        silentCheckCount++;
      }

      if (silentCheckCount >= 3) {
        if (connection_handle != BLE_CONN_HANDLE_INVALID) {
            Bluefruit.disconnect(connection_handle);
        }
      }
    } 
    // --- ACTIVE MODE (Engine on) ---
    else {
      silentCheckCount = 0; 
      if (now - lastRequestTime > 10000) {
        if (!requestingTemp) obdWriteChar.write("010C\r", 5); 
        else obdWriteChar.write("0105\r", 5); 
        requestingTemp = !requestingTemp; 
        lastRequestTime = now;
      }
    }
    delay(10);
    return;
  }

  // --- DISCONNECTED MODE ---
  Bluefruit.Scanner.start(0); 
  delay(2500); 
  Bluefruit.Scanner.stop(); 
  
  if (Bluefruit.Central.connected()) return;

  // 1. Disable I2C (TWI) hardware to save ~50uA
  Wire.end(); 
  
  // 2. Set SDA and SCL to high impedance (INPUT) to prevent leakage
  pinMode(PIN_WIRE_SDA, INPUT); 
  pinMode(PIN_WIRE_SCL, INPUT);

  // 3. Sleep for 30 seconds. CPU enters System ON Idle (~2uA)
  delay(30000); 

  // 4. Wake up and re-enable I2C and display
  Wire.begin();
  u8g2.begin();
  u8g2.setPowerSave(1); // Keep display off until connected
  u8g2.setFont(u8g2_font_fub30_tf);
}

void obd_notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len) {
  // Valid data received, feed the watchdog
  lastDataReceivedTime = millis();
  
  for (int i = 0; i < len; i++) rxBuffer += (char)data[i];
  
  // Buffer Overflow Protection
  // Clear buffer if it exceeds 60 characters to prevent RAM exhaustion
  if (rxBuffer.length() > 60) {
      rxBuffer = "";
  }
  
  if (rxBuffer.indexOf('>') >= 0) {
    rxBuffer.replace(" ", ""); rxBuffer.replace("\r", ""); rxBuffer.replace("\n", "");
    
    // 1. Process RPM
    int rpmPos = rxBuffer.indexOf("410C");
    if (rpmPos >= 0 && rxBuffer.length() >= rpmPos + 8) {
      String hexA = rxBuffer.substring(rpmPos + 4, rpmPos + 6);
      String hexB = rxBuffer.substring(rpmPos + 6, rpmPos + 8);
      long A = strtol(hexA.c_str(), NULL, 16);
      long B = strtol(hexB.c_str(), NULL, 16);
      currentRpm = ((A * 256) + B) / 4;

      if (currentRpm > 0 && !isScreenOn) {
        u8g2.setPowerSave(0);
        isScreenOn = true;
      } else if (currentRpm == 0 && isScreenOn) {
        u8g2.setPowerSave(1);
        isScreenOn = false;
      }
    }

    // 2. Process Temperature
    int tempPos = rxBuffer.indexOf("4105");
    if (tempPos >= 0 && rxBuffer.length() >= tempPos + 6) {
      String hexTemp = rxBuffer.substring(tempPos + 4, tempPos + 6);
      int tempCelsius = (int)strtol(hexTemp.c_str(), NULL, 16) - 40;
      
      if (currentRpm > 0 && isScreenOn) {
        upDateValue(tempCelsius);
      }
    }
    rxBuffer = ""; // Clear buffer after successfully processing a packet
  }
}

void upDateValue(int t) {
  u8g2.clearBuffer();
  sprintf(displayStr, "%d\xB0""C", t);
  // Screen offset for 128x32 OLED
  u8g2.drawStr(0, 32, displayStr);
  u8g2.sendBuffer();
}