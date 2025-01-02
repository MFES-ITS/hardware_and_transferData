#include <WiFi.h>
#include <HTTPClient.h>

// Wi-Fi credentials
const char *ssid = "kiwkiw";  // Replace with your Wi-Fi SSID
const char *password = "12345678"; // Replace with your Wi-Fi password

// Application endpoint (replace with your server's IP or domain)
const char *applicationURL = "http://192.168.166.20:5000/monitor"; // Replace with your app endpoint

// Unique identifier for this sensor
const char *sensorID = "monitor0"; // Replace with a unique sensor ID

// Function to generate random heart rate and pulse oximeter values
int generateRandomHeartRate() {
  return random(60, 100); // Generate heart rate between 60 and 100 BPM
}

int generateRandomPulseOximeter() {
  return random(95, 100); // Generate pulse oximeter value between 95% and 100%
}

void setup() {
  Serial.begin(115200);

  // Seed the random number generator with a unique value (time-based)
  randomSeed(analogRead(0));

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
    HTTPClient http;

    // Generate random heart rate and pulse oximeter values
    int heartRate = generateRandomHeartRate();
    int pulseOximeter = generateRandomPulseOximeter();

    // Create JSON payload
    String payload = "{";
    payload += "\"monitorID\":\"" + String(sensorID) + "\","; // Add sensorID to the payload
    payload += "\"heartRate\":" + String(heartRate) + ","; // Heart rate value
    payload += "\"pulseOximeter\":" + String(pulseOximeter); // Pulse oximeter value
    payload += "}";

    // Print payload to Serial Monitor
    Serial.println("\n[DEBUG] Data Sent to Flask:");
    Serial.println(payload);

    // Send POST request to the application
    http.begin(applicationURL);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(payload);

    // Check the response
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("[DEBUG] Response from Flask: " + response);
    } else {
      Serial.printf("[ERROR] Failed to send POST: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  } else {
    Serial.println("[ERROR] Wi-Fi disconnected. Reconnecting...");
    WiFi.begin(ssid, password);
  }

  delay(5000); // Send data every 5 seconds
}
