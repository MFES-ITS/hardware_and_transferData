#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <WiFi.h>
#include <HTTPClient.h>

// // UUID untuk BLE Service dan Characteristic ConeX-A
// #define SERVICE_UUID "f9ef3503-6f45-4fa2-a3d5-6f8f5344a5de"
// #define CHARACTERISTIC_UUID "455824a6-9097-4d09-94fb-997d54b926f3" 

// UUID untuk BLE Service dan Characteristic ConeX-B
#define SERVICE_UUID "2da15dcc-05e3-4443-86de-5465e7ffb388"
#define CHARACTERISTIC_UUID "a4836521-179a-40ff-ad18-5ec6a8ebefe2"

// Infrared Sensor Configuration
const int analogPin = 33;                // GPIO pin for infrared sensor
const float INFRARED_THRESHOLD = 30.0;   // Object detection threshold (in cm)

// Backend API Endpoint
const char *applicationURL = "http://34.160.63.178:80/infrared";
const char *sensorID = "cone0-B";  // Unique sensor identifier

BLECharacteristic *pCharacteristic;
bool newCredentialsReceived = false;
String wifiSSID = "";
String wifiPassword = "";
bool objectDetected = false;

// BLE Callback to Handle Connections
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        Serial.println("✅ BLE Client Connected!");
    }

    void onDisconnect(BLEServer* pServer) {
        Serial.println("❌ BLE Client Disconnected!");
        pServer->startAdvertising();  // Restart advertising
    }
};

// BLE Callback to Receive Wi-Fi Credentials
class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        String rawData = pCharacteristic->getValue();
        Serial.print("📜 Received data: ");
        Serial.println(rawData.c_str());

        // Extract Wi-Fi SSID & Password
        int splitIndex = rawData.indexOf(':');
        if (splitIndex != -1) {
            wifiSSID = rawData.substring(0, splitIndex);
            wifiPassword = rawData.substring(splitIndex + 1);
            newCredentialsReceived = true;

            Serial.println("✅ Wi-Fi SSID: " + wifiSSID);
            Serial.println("✅ Wi-Fi Password: " + wifiPassword);
        } else {
            Serial.println("❌ Error: Invalid Wi-Fi credentials format!");
        }
    }
};

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);  // ✅ Ensure ESP32 is in Station mode

    // Initialize BLE
    BLEDevice::init("ESP32_BLE_WIFI_SETUP");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );

    pCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->start();

    Serial.println("✅ BLE Service Started. Advertising...");
}

void loop() {
    // Check if Wi-Fi credentials are received and connect
    if (newCredentialsReceived) {
        Serial.println("📶 Connecting to Wi-Fi...");

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("⚠️ Already connected. Disconnecting...");
            WiFi.disconnect(true);
            delay(1000);
        }

        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

        int counter = 0;
        while (WiFi.status() != WL_CONNECTED && counter < 20) {
            delay(1000);
            Serial.print(".");
            counter++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n✅ Wi-Fi Connected!");
            pCharacteristic->setValue("SUCCESS");
            sendData(true);  // ✅ Send initial payload after connection
        } else {
            Serial.println("\n❌ Failed to connect!");
            pCharacteristic->setValue("FAILED");
        }

        pCharacteristic->notify();  // Notify Flutter App
        newCredentialsReceived = false;
    }

    // If Wi-Fi is connected, check sensor data
    if (WiFi.status() == WL_CONNECTED) {
        int sensorValue = analogRead(analogPin);
        float voltage = sensorValue * (3.3 / 4095.0);
        float distance = 27.61 / (voltage - 0.1696);  // Example calibration formula

        Serial.printf("SensorID: %s | Distance: %.2f cm\n", sensorID, distance);

        // Object enters the detection zone
        if (distance < INFRARED_THRESHOLD && !objectDetected) {
            objectDetected = true;
            sendData(true);  // ✅ Send "true" when an object is detected
            Serial.println("🔴 Object detected!");

        // Object leaves the detection zone
        } else if (distance > INFRARED_THRESHOLD && objectDetected) {
            objectDetected = false;
            Serial.println("🟢 Object has left the threshold.");
        }
    } else {
        Serial.println("❌ Wi-Fi disconnected. Reconnecting...");
        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    }

    delay(500);  // Adjust delay for real-time detection
}

// Function to Send JSON Payload to Backend
void sendData(bool infraredStatus) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ Cannot send data, Wi-Fi not connected!");
        return;
    }

    String payload = "{";
    payload += "\"sensorID\":\"" + String(sensorID) + "\",";
    payload += "\"infrared\":" + String(infraredStatus ? "true" : "false");
    payload += "}";

    HTTPClient http;
    http.begin(applicationURL);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("✅ Response: %s\n", response.c_str());
    } else {
        Serial.printf("❌ Error on sending POST: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
}
