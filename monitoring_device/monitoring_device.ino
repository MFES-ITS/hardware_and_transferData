#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <WiFi.h>
#include <HTTPClient.h>

// UUID for BLE Service and Characteristic
#define SERVICE_UUID "abda6195-ac4d-445f-99d5-bff9d9ae3c4e"
#define CHARACTERISTIC_UUID "58e6af79-6625-4b21-8671-595ecd5a1d71"

// Backend API Endpoint
const char *applicationURL = "http://34.160.63.178:80/monitor";
const char *sensorID = "monitor0";

BLECharacteristic *pCharacteristic;
bool newCredentialsReceived = false;
String wifiSSID = "";
String wifiPassword = "";

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
            sendSensorData();
        } else {
            Serial.println("\n❌ Failed to connect!");
            pCharacteristic->setValue("FAILED");
        }

        pCharacteristic->notify();  // Notify Flutter App
        newCredentialsReceived = false;
    }

    // If Wi-Fi is connected, check sensor data
    if (WiFi.status() == WL_CONNECTED) {
        sendSensorData();
    } else {
        Serial.println("❌ Wi-Fi disconnected. Reconnecting...");
        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    }

    delay(5000);  // Adjust delay for real-time detection
}

// Generate random heart rate & pulse oximeter values
int generateRandomHeartRate() {
    return random(60, 100);
}

int generateRandomPulseOximeter() {
    return random(95, 100);
}

// Send data to the server
void sendSensorData() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ Cannot send data, Wi-Fi not connected!");
        return;
    }

    HTTPClient http;
    http.begin(applicationURL);
    http.addHeader("Content-Type", "application/json");

    int heartRate = generateRandomHeartRate();
    int pulseOximeter = generateRandomPulseOximeter();

    String payload = "{";
    payload += "\"monitorID\":\"" + String(sensorID) + "\",";
    payload += "\"heartRate\":" + String(heartRate) + ",";
    payload += "\"pulseOximeter\":" + String(pulseOximeter);
    payload += "}";

    Serial.println("\n📤 Sending Data: " + payload);
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("✅ Response: %s\n", response.c_str());
    } else {
        Serial.printf("❌ Error on sending POST: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
}
