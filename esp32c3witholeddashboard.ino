#include <U8g2lib.h>
#include <DHT.h>
#include <SPI.h>
#include <Wire.h>
#include <MQUnifiedsensor.h>

// --- Tambahan untuk Wi-Fi dan HTTP ---
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
// --- Akhir Tambahan ---

// --- Konfigurasi Wi-Fi Anda ---
const char* ssid = "Tenda_160305";
const char* password = "didegamers123";

// --- URL Endpoint Server Anda ---
const char* serverUrl = "http://192.168.0.136:8000/api/sensor-data"; 

// --- ID Unik Perangkat ---
const char* deviceUniqueId = "ESP32_C3_KAMAR_01"; 

// Interval pengiriman data ke server (ms)
const unsigned long dataSendInterval = 0.5 * 60 * 1000; // setiap 30 detik
unsigned long lastDataSendTime = 0;
// --- Akhir Konfigurasi Wi-Fi & Server ---

// Konfigurasi untuk MQUnifiedsensor
#define Placa "XIAO ESP32C3"
#define Pin_MQ135 (0)
#define Type_MQ135 "MQ-135"
#define Voltage_Resolution (3.3f)
#define ADC_Bit_Resolution (12)
#define MQ135_RL (10.0f)

MQUnifiedsensor MQ135_sensor(Placa, Voltage_Resolution, ADC_Bit_Resolution, Pin_MQ135, Type_MQ135);

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ D5, /* data=*/ D4);

#define DHTPIN D1
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

int airQuality = 0;

// Variabel untuk animasi mata
unsigned long nextBlinkTime = 0;
bool isBlinking = false;
unsigned long blinkInterval = 120000;
const unsigned long blinkDuration = 300;
const int blinkCount = 2;
unsigned long lastBlinkTime = 0;
int currentBlink = 0;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;

// Variabel untuk pergantian layar otomatis
bool showDataScreen = false;
unsigned long lastScreenSwitchTime = 0;
const unsigned long eyesDuration = 20000; 
const unsigned long dataDuration = 10000; 

// --- Fungsi untuk koneksi Wi-Fi ---
void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int wifiConnectAttempts = 0; // Nama variabel diubah
  
  while (WiFi.status() != WL_CONNECTED && wifiConnectAttempts < 30) { // Menggunakan nama variabel baru
    delay(500);
    Serial.print(".");
    wifiConnectAttempts++; // Menggunakan nama variabel baru
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi. Will retry later if needed.");
  }
}
// --- Akhir Fungsi Wi-Fi ---

// --- Fungsi untuk mengirim data ke server ---
void sendDataToServer(float co2, float temp, float hum, int aq_level, float voltage_mq) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    Serial.print("[HTTP] begin...\n");
    if (http.begin(serverUrl)) {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Accept", "application/json");

      StaticJsonDocument<256> jsonDoc;
      jsonDoc["unique_device_id"] = deviceUniqueId;
      
      if (!isnan(co2) && co2 >= 0) jsonDoc["co2_ppm"] = round(co2 * 10) / 10.0;
      if (!isnan(temp)) jsonDoc["temperature_c"] = round(temp * 10) / 10.0;
      if (!isnan(hum)) jsonDoc["humidity_percent"] = round(hum * 10) / 10.0;
      jsonDoc["air_quality_level"] = aq_level;
      if (!isnan(voltage_mq)) jsonDoc["voltage_sensor"] = round(voltage_mq * 100) / 100.0;

      String jsonPayload;
      serializeJson(jsonDoc, jsonPayload);

      Serial.print("[HTTP] Sending payload: ");
      Serial.println(jsonPayload);

      int httpResponseCode = http.POST(jsonPayload);

      if (httpResponseCode > 0) {
        Serial.print("[HTTP] POST Response code: ");
        Serial.println(httpResponseCode);
        String responsePayload = http.getString();
        Serial.println("[HTTP] Response payload: " + responsePayload);
      } else {
        Serial.print("[HTTP] POST Error: ");
        Serial.println(httpResponseCode);
        Serial.printf("[HTTP] POST error: %s\n", http.errorToString(httpResponseCode).c_str());
      }
      http.end();
    } else {
      Serial.printf("[HTTP] Unable to connect to server: %s\n", serverUrl);
    }
  } else {
    Serial.println("WiFi not connected. Cannot send data to server.");
  }
}
// --- Akhir Fungsi Pengiriman Data ---

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  dht.begin();

  connectToWiFi();

  MQ135_sensor.setRegressionMethod(1);
  MQ135_sensor.init();

  Serial.println("MQ135 R0 Calibration: Using getVoltage() for Rs calculation.");
  Serial.println("Ensure sensor is in clean air and has warmed up.");

  float R0_sum_temp = 0.0f;
  int calibration_samples = 15;
  int valid_samples_count = 0;

  for (int i = 1; i <= calibration_samples; i++) {
    MQ135_sensor.update();
    float V_sensor = MQ135_sensor.getVoltage(true);
    float current_Rs = -1.0f;
    if (V_sensor > 0.001f && V_sensor < Voltage_Resolution - 0.001f) {
        current_Rs = MQ135_RL * (Voltage_Resolution - V_sensor) / V_sensor;
    } else if (V_sensor <= 0.001f) {
        Serial.print("V_sensor near zero, Rs high/inf. ");
    } else {
        Serial.print("V_sensor near Vcc, Rs low/zero. ");
    }

    if (current_Rs > 0.0f) {
        R0_sum_temp += current_Rs;
        valid_samples_count++;
    } else {
        Serial.print("Invalid Rs sample skipped. ");
    }
    Serial.print(".");
    delay(500);
  }

  float calibrated_R0 = -1.0f;
  if (valid_samples_count > 0) {
      calibrated_R0 = R0_sum_temp / valid_samples_count;
      MQ135_sensor.setR0(calibrated_R0);
  } else {
      Serial.print("\nNo valid Rs samples obtained during R0 calibration! ");
  }

  if (calibrated_R0 <= 0.0f || isinf(calibrated_R0) || isnan(calibrated_R0)) {
    Serial.println("\nMQ135 Warning: R0 is invalid after calibration. PPM readings will be inaccurate.");
  } else {
    Serial.print("\nMQ135 R0 calibrated and set to: ");
    Serial.println(MQ135_sensor.getR0());
  }
  
  MQ135_sensor.setA(110.47f); MQ135_sensor.setB(-2.862f); // Untuk CO2

  Serial.println("Sensor setup complete.");
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_helvB12_tr);

  int load = 0;
  while (load < 10) { // Loading animation
    u8g2.firstPage();
    do {
      u8g2.setCursor(10, SCREEN_HEIGHT / 2 - 5);
      u8g2.print("Loading");
      u8g2.setCursor(10, SCREEN_HEIGHT / 2 + 15);
      for (int i = 0; i <= load; i++) {
        u8g2.print(".");
      }
    } while (u8g2.nextPage());
    delay(200);
    load++;
  }
  u8g2.clearBuffer();
  lastScreenSwitchTime = millis(); 
  lastDataSendTime = millis(); 
}

void loop() {
  MQ135_sensor.update();
  float co2_ppm;
  float current_voltage = MQ135_sensor.getVoltage(true);

  if (isnan(MQ135_sensor.getR0()) || isinf(MQ135_sensor.getR0()) || MQ135_sensor.getR0() <= 0.0f) {
    Serial.println("MQ135 Error: R0 is not valid. PPM calculation skipped.");
    airQuality = 3;
    co2_ppm = -2.0f;
  } else {
    // Pembacaan suhu dan kelembaban dari DHT (tetap dibaca, meskipun tidak untuk kompensasi MQ135 via library)
    // float temp_dht = dht.readTemperature(); 
    // float humidity_dht = dht.readHumidity();
    // Pemanggilan MQ135_sensor.setTempAndHum() dihapus

    co2_ppm = MQ135_sensor.readSensor(); // Pembacaan PPM tanpa kompensasi internal library
    if (isnan(co2_ppm) || isinf(co2_ppm) || co2_ppm < 0.0f) {
      Serial.print("MQ135 Warning: Invalid PPM reading (");
      Serial.print(co2_ppm);
      Serial.println(").");
      airQuality = 3;
    } else {
      if (co2_ppm <= 700.0f) { airQuality = 0; }
      else if (co2_ppm <= 1200.0f) { airQuality = 1; }
      else if (co2_ppm <= 2000.0f) { airQuality = 2; }
      else { airQuality = 3; }
    }
  }

  float temp_for_display = dht.readTemperature();
  float humidity_for_display = dht.readHumidity();

  if (isnan(temp_for_display) || isnan(humidity_for_display)) {
    Serial.println("DHT read error for display!");
  }

  unsigned long currentTime = millis();

  // --- LOGIKA PENGIRIMAN DATA KE SERVER ---
  if (WiFi.status() == WL_CONNECTED && (currentTime - lastDataSendTime >= dataSendInterval)) {
    bool co2_data_sendable = (co2_ppm == -2.0f) || (!isnan(co2_ppm) && !isinf(co2_ppm) && co2_ppm >= 0);

    if (co2_data_sendable && !isnan(temp_for_display) && !isnan(humidity_for_display) ) {
      sendDataToServer(co2_ppm, temp_for_display, humidity_for_display, airQuality, current_voltage);
    } else {
      Serial.println("Sensor data not ready or invalid, skipping server send.");
    }
    lastDataSendTime = currentTime;
  } else if (WiFi.status() != WL_CONNECTED && (currentTime - lastDataSendTime >= dataSendInterval)) {
      Serial.println("Attempting to reconnect WiFi to send data...");
      connectToWiFi(); 
      lastDataSendTime = currentTime; 
  }
  // --- AKHIR LOGIKA PENGIRIMAN DATA ---

  if (showDataScreen) {
    if (!isnan(temp_for_display) && !isnan(humidity_for_display)) {
        displaySensorData(temp_for_display, humidity_for_display, airQuality, co2_ppm);
    } else {
        u8g2.firstPage();
        do {
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.setCursor(5, SCREEN_HEIGHT / 2);
            u8g2.print("DHT Error! No Data.");
        } while (u8g2.nextPage());
    }

    if (currentTime - lastScreenSwitchTime >= dataDuration) {
      showDataScreen = false;
      lastScreenSwitchTime = currentTime;
      isBlinking = false; 
      currentBlink = 0;   
    }
  } else {
    int comfortLevel = calculateComfortLevel(airQuality, temp_for_display, humidity_for_display);
    updateDisplay(comfortLevel);

    if (currentTime - lastScreenSwitchTime >= eyesDuration) {
      showDataScreen = true;
      lastScreenSwitchTime = currentTime;
    }
  }

  Serial.print("CO2 PPM: ");
  Serial.print(co2_ppm);
  Serial.print(" | V_sens: ");
  Serial.print(current_voltage);
  Serial.print(" | R0: ");
  Serial.print(MQ135_sensor.getR0());
  Serial.print(" | AirQ Level: ");
  Serial.print(airQuality);
  Serial.print(" | Temp: ");
  Serial.print(temp_for_display);
  Serial.print("C | Hum: ");
  Serial.print(humidity_for_display);
  Serial.println("%");

  delay(500);
}

// Fungsi calculateComfortLevel tetap sama
int calculateComfortLevel(int air, float temp, float humid) {
  int airLevel = air; 
  int tempLevel = (temp >= 20 && temp <= 24) ? 0 : (temp >= 18 && temp <= 26) ? 1 : (temp >= 15 && temp <= 30) ? 2 : 3;
  if (isnan(temp)) tempLevel = 3; 
  int humidLevel = (humid >= 40 && humid <= 60) ? 0 : (humid >= 30 && humid <= 70) ? 1 : (humid >= 20 && humid <= 80) ? 2 : 3;
  if (isnan(humid)) humidLevel = 3; 
  
  return max(max(airLevel, tempLevel), humidLevel);
}

// Fungsi updateDisplay dan drawEyes tetap sama
void updateDisplay(int comfortLevel) {
  unsigned long currentTime = millis();
  
  if (currentTime - lastBlinkTime >= blinkInterval && !isBlinking) {
    isBlinking = true;
    currentBlink = 0;
  }

  if (isBlinking) {
    if (currentBlink < blinkCount * 2) { 
      unsigned long timeSinceLastBlinkAction = currentTime - lastBlinkTime;
      bool shouldToggleBlink = false;

      if (currentBlink % 2 == 0) { 
        if (timeSinceLastBlinkAction >= 0) { 
            shouldToggleBlink = true;
        }
      } else { 
        if (timeSinceLastBlinkAction >= blinkDuration) {
            shouldToggleBlink = true;
        }
      }
      
      if(shouldToggleBlink){
        u8g2.firstPage();
        do {
          drawEyes(0, true); 
        } while (u8g2.nextPage());
        lastBlinkTime = currentTime; 
        currentBlink++;
      }
    } else { 
      isBlinking = false;
      lastBlinkTime = currentTime; 
      blinkInterval = random(60000, 180000); 
      u8g2.firstPage();
      do {
        int eyelidOpenPercentage;
        switch (comfortLevel) {
            case 0: eyelidOpenPercentage = 0; break;  // Mata terbuka penuh (tidak ada kelopak atas)
            case 1: eyelidOpenPercentage = 20; break; // Sedikit menyipit
            case 2: eyelidOpenPercentage = 40; break; // Lebih menyipit
            case 3: eyelidOpenPercentage = 55; break; // Sangat sipit (maksimum sekitar 60-70 agar masih terlihat)
            default: eyelidOpenPercentage = 0; break;
        }
        drawEyes(eyelidOpenPercentage, false); 
      } while (u8g2.nextPage());
    }
  } else { // Tidak dalam sequence kedipan, gambar mata normal
      u8g2.firstPage();
      do {
        int eyelidOpenPercentage;
        switch (comfortLevel) {
            case 0: eyelidOpenPercentage = 0; break;  // Mata terbuka penuh
            case 1: eyelidOpenPercentage = 20; break; // Sedikit menyipit
            case 2: eyelidOpenPercentage = 40; break; // Lebih menyipit
            case 3: eyelidOpenPercentage = 55; break; // Sangat sipit
            default: eyelidOpenPercentage = 0; break;
        }
        drawEyes(eyelidOpenPercentage, false); 
      } while (u8g2.nextPage());
  }
}

// Fungsi drawEyes tetap sama
void drawEyes(int eyelidOpenPercentage, bool isActivelyBlinking) { 
  const int eyeY = SCREEN_HEIGHT / 2;    
  const int eyeRadiusX = 20;             
  const int eyeRadiusY = 25;             
  const int pupilRadius = 6;
  const int pupilOffsetX = 6;            

  const int leftEyeX = SCREEN_WIDTH / 4 + 10; 
  const int rightEyeX = SCREEN_WIDTH * 3 / 4 - 10;

  const int blinkLineWidth = eyeRadiusX * 1.8; 
  const int blinkLineThickness = 3;     

  u8g2.setDrawColor(0); 
  u8g2.drawBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); 
  u8g2.setDrawColor(1); 

  if (isActivelyBlinking) { 
    for (int i = 0; i < blinkLineThickness; i++) {
      u8g2.drawHLine(leftEyeX - blinkLineWidth / 2, eyeY - blinkLineThickness / 2 + i, blinkLineWidth);
      u8g2.drawHLine(rightEyeX - blinkLineWidth / 2, eyeY - blinkLineThickness / 2 + i, blinkLineWidth);
    }
  } else { 
    u8g2.drawFilledEllipse(leftEyeX, eyeY, eyeRadiusX, eyeRadiusY, U8G2_DRAW_ALL);
    u8g2.drawFilledEllipse(rightEyeX, eyeY, eyeRadiusX, eyeRadiusY, U8G2_DRAW_ALL);

    u8g2.setDrawColor(0); 
    u8g2.drawDisc(leftEyeX + pupilOffsetX, eyeY, pupilRadius, U8G2_DRAW_ALL);
    u8g2.drawDisc(rightEyeX - pupilOffsetX, eyeY, pupilRadius, U8G2_DRAW_ALL);
    
    int eyelidDrawHeight = (int)((eyelidOpenPercentage / 100.0) * (eyeRadiusY * 1.8)); 
    if (eyelidDrawHeight < 0) eyelidDrawHeight = 0;
    if (eyelidDrawHeight > eyeRadiusY * 1.8) eyelidDrawHeight = eyeRadiusY * 1.8;

    u8g2.setDrawColor(0); 
    u8g2.drawBox(leftEyeX - eyeRadiusX, eyeY - eyeRadiusY, eyeRadiusX * 2, eyelidDrawHeight);
    u8g2.drawBox(rightEyeX - eyeRadiusX, eyeY - eyeRadiusY, eyeRadiusX * 2, eyelidDrawHeight);
  }
}

// Fungsi displaySensorData tetap sama
void displaySensorData(float temp, float humid, int airQualityValue, float co2_ppm_val) {
  u8g2.firstPage();
  do {
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_profont10_tf);

    int yPos = 8; 
    int lineHeight = 11;

    u8g2.setCursor(2, yPos);
    u8g2.print("T: ");
    if(isnan(temp)) u8g2.print("N/A"); else u8g2.print(temp, 1);
    u8g2.print((char)176); 
    u8g2.print("C");
    yPos += lineHeight;

    u8g2.setCursor(2, yPos);
    u8g2.print("H: ");
    if(isnan(humid)) u8g2.print("N/A"); else u8g2.print(humid, 0);
    u8g2.print("%");
    yPos += lineHeight;

    u8g2.setCursor(2, yPos);
    u8g2.print("CO2: ");
    if (co2_ppm_val < -1.5f && co2_ppm_val > -2.5f) { 
        u8g2.print("R0Err");
    } else if (co2_ppm_val < -0.5f && co2_ppm_val > -1.5f) { 
        u8g2.print("RsErr"); 
    } else if (co2_ppm_val < 0.0f) { 
        u8g2.print("CalcErr"); 
    } else if (isnan(co2_ppm_val)) { 
        u8g2.print("NoVal");
    }
     else {
        u8g2.print(co2_ppm_val, 0); 
        u8g2.print("ppm");
    }
    yPos += lineHeight;

    String airQStr;
    switch (airQualityValue) {
      case 0: airQStr = "Fresh Air"; break;
      case 1: airQStr = "Low Pollut."; break;
      case 2: airQStr = "High Pollut."; break;
      case 3: airQStr = "Alert/Error"; break; 
      default: airQStr = "Unknown"; break;
    }
    u8g2.setCursor(2, yPos);
    u8g2.print("Air: ");
    yPos += lineHeight; 
    u8g2.setCursor(10, yPos);
    u8g2.print(airQStr);

  } while (u8g2.nextPage());
}