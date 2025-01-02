#include <WiFi.h>
#include <HTTPClient.h>

// Wi-Fi credentials
const char *ssid = "kiwkiw";  // Replace with your Wi-Fi SSID
const char *password = "12345678"; // Replace with your Wi-Fi password

// Application endpoint (replace with your server's IP or domain)
const char *applicationURL = "http://192.168.166.20:5000/infrared"; // Replace with your app endpoint

// Sensor Configuration
const int analogPin = 33;                // GPIO for analog input
const float INFRARED_THRESHOLD = 30.0;   // Threshold for detecting "true" (in cm)

// Unique identifier for this sensor
const char *sensorID = "cone0-A";        // Replace "Sensor_A" with "Sensor_B" for other sensors

float lastDistance = 0;                  // Store the previous distance
bool objectDetected = false;             // State flag: true when object is detected

void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");
  Serial.printf("Device IP Address: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    // Read sensor value
    int sensorValue = analogRead(analogPin);
    float voltage = sensorValue * (3.3 / 4095.0); // Assuming 3.3V ADC reference
    float distance = 27.61 / (voltage - 0.1696);  // Example calibration formula
    Serial.printf("SensorID: %s | Distance: %.2f cm\n", sensorID, distance);

    // Object enters the threshold
    if (distance < INFRARED_THRESHOLD && !objectDetected) {
      objectDetected = true; // Mark as triggered
      sendData(true);        // Send infrared status as true
      Serial.println("Object detected! Triggered once.");

    // Object leaves the threshold
    } else if (distance > INFRARED_THRESHOLD && objectDetected) {
      objectDetected = false; // Reset the trigger
      Serial.println("Object has left the threshold.");
    }
  } else {
    Serial.println("Wi-Fi disconnected. Reconnecting...");
    WiFi.begin(ssid, password);
  }

  delay(500); // Adjust delay for the application
}

void sendData(bool infraredStatus) {
  // Create JSON payload
  String payload = "{";
  payload += "\"sensorID\":\"" + String(sensorID) + "\","; // Add sensorID field
  payload += "\"infrared\":" + String(infraredStatus ? "true" : "false"); // Boolean for infrared
  payload += "}";

  // Send POST request to the application
  HTTPClient http;
  http.begin(applicationURL);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("Response: %s\n", response.c_str());
  } else {
    Serial.printf("Error on sending POST: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}
