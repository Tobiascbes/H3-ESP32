/**
 * @file main.cpp
 * @brief ESP32 project for handling WiFi credentials, WebSocket, and SPIFFS file management.
 * 
 * Features:
 * - Save WiFi credentials via a web form.
 * - Switch between AP and Station modes based on saved credentials.
 * - Touch count logging and WebSocket notifications.
 * - File management using SPIFFS.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <FS.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "ArduinoJson.h"

// Global Variables
/** @brief Soft AP SSID */
const char* ap_ssid = "TTESP32";

/** @brief Soft AP password */
const char* ap_password = "12345678";

/** @brief Saved SSID from SPIFFS */
String savedSSID;

/** @brief Saved Password from SPIFFS */
String savedPassword;

/** @brief Local IP for Soft AP */
IPAddress local_ip(192, 168, 1, 1);

/** @brief Gateway IP for Soft AP */
IPAddress gateway(192, 168, 1, 1);

/** @brief Subnet mask for Soft AP */
IPAddress subnet(255, 255, 255, 0);

/** @brief WebServer instance running on port 80 */
WebServer server(80);

/** @brief AsyncWebServer instance running on port 81 */
AsyncWebServer asyncServer(81);

/** @brief WebSocket server for real-time touch count updates */
AsyncWebSocket ws("/ws");

/** @brief Pin number for physical button */
int buttonPin = 21;

/** @brief State of the physical button */
int buttonState = 0;

/** @brief Pin number for touch sensor */
#define TOUCH_PIN T0

/** @brief Counter for touch events */
int touchCount = 0;

/** 
 * @brief HTML form for WiFi credentials input 
 */
const char* html_form = R"(
<!DOCTYPE html>
<html>
<body>
<h2>Enter WiFi Credentials</h2>
<form action="/save" method="POST">
  <label for="ssid">SSID:</label><br>
  <input type="text" id="ssid" name="ssid"><br>
  <label for="password">Password:</label><br>
  <input type="password" id="password" name="password"><br><br>
  <input type="submit" value="Save">
</form>
</body>
</html>
)";

/**
 * @brief Handles 404 errors for WebServer.
 */
void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

/**
 * @brief Serves the root HTML form for WiFi credentials input.
 */
void handleRoot() {
  server.send(200, "text/html", html_form);
}

/**
 * @brief Reads saved WiFi credentials from SPIFFS.
 */
void readSpiff() {
  File file = SPIFFS.open("/Cred.txt", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.startsWith("SSID: ")) {
      savedSSID = line.substring(6);
      savedSSID.trim();
    } else if (line.startsWith("Password: ")) {
      savedPassword = line.substring(10);
      savedPassword.trim();
    }
  }
  file.close();
}

/**
 * @brief Saves WiFi credentials submitted via the web form to SPIFFS.
 */
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String input_ssid = server.arg("ssid");
    String input_password = server.arg("password");

    File file = SPIFFS.open("/Cred.txt", "w");
    if (!file) {
      server.send(500, "text/plain", "Failed to open file for writing");
      return;
    }

    file.println("SSID: " + input_ssid);
    file.println("Password: " + input_password);
    file.close();

    server.send(200, "text/plain", "Credentials saved successfully");
    ESP.restart(); // Restart ESP to apply changes
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

/**
 * @brief Logs an impulse event to impulses.json in SPIFFS.
 */
void logImpulse() {
  File file = SPIFFS.open("/impulses.json", "a");
  if (!file) {
    Serial.println("Failed to open impulses file for appending");
    return;
  }

  DynamicJsonDocument doc(1024);
  doc["timestamp"] = millis();
  doc["impulse"] = "Impulse detected";

  serializeJson(doc, file);
  file.println();
  file.close();
}

/**
 * @brief Notifies WebSocket clients of the updated touch count.
 */
void notifyClients() {
  ws.textAll(String(touchCount));
}

/**
 * @brief Updates the touch count file in SPIFFS.
 */
void updateTouchCountFile() {
  File file = SPIFFS.open("/count.json", "w");
  if (!file) {
    Serial.println("Failed to open count.json for writing");
    return;
  }
  file.println(touchCount);
  file.close();
}

/**
 * @brief ISR for handling touch sensor events.
 */
void onTouch() {
  touchCount++;
  updateTouchCountFile();
  notifyClients();
}

/**
 * @brief Deletes the touch count file from SPIFFS.
 */
void spiffDeleteCount() {
  if (SPIFFS.remove("/count.json")) {
    Serial.println("count.json deleted successfully");
  } else {
    Serial.println("Failed to delete count.json");
  }
  touchCount = 0; // Reset touch count
}

/**
 * @brief Deletes the credentials file from SPIFFS and restarts ESP.
 */
void spiffDeleteCred() {
  if (SPIFFS.remove("/Cred.txt")) {
    Serial.println("Cred.txt deleted successfully");
    ESP.restart();
  } else {
    Serial.println("Failed to delete Cred.txt");
  }

  // Create a new empty file with the same name
  File file = SPIFFS.open("/Cred.txt", "w");
  if (!file) {
    Serial.println("Failed to create new Cred.txt");
  } else {
    Serial.println("New Cred.txt created successfully");
    file.close();
  }
}

/**
 * @brief Deletes all SPIFFS files and restarts ESP.
 */
void spiffDeleteAll() {
  spiffDeleteCount();
  spiffDeleteCred();
  ESP.restart();
}

/**
 * @brief Configures WiFi mode based on saved credentials and starts servers.
 */
void setup() {
  delay(1000);
  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);
  Serial.println("Button setup complete. Waiting for button press...");

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");

  // Read and extract SSID and Password
  readSpiff();

  // Read initial touch count from count.txt
  File file = SPIFFS.open("/count.json", "r");
  if (file) {
    touchCount = file.parseInt();
    file.close();
  } else {
    Serial.println("Failed to open count.txt for reading");
  }

  // Timeout setting in seconds
  const int wifiTimeout = 10; // Adjust this to set the timeout duration
  unsigned long startAttemptTime = millis();

  // Connect to WiFi using the saved credentials
  if (savedSSID.length() > 0 && savedPassword.length() > 0) {
    Serial.println("Switching to Wifi station mode");
    WiFi.mode(WIFI_STA);
    Serial.println("Connecting to WiFi...");
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    // Attempt to connect for wifiTimeout seconds
    while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < wifiTimeout * 1000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nFailed to connect within the timeout, starting Access Point");

      // Start the Access Point
      WiFi.softAP(ap_ssid, ap_password);
      WiFi.softAPConfig(local_ip, gateway, subnet);
      delay(100);

      Serial.print("Access Point IP Address: ");
      Serial.println(WiFi.softAPIP());
    }
  } else {
    Serial.println("No saved credentials found, starting Access Point");

    // Start the Access Point
    WiFi.softAP(ap_ssid, ap_password);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    delay(100);

    Serial.print("Access Point IP Address: ");
    Serial.println(WiFi.softAPIP());
  }

  // Set up HTTP server routes for WebServer (port 80)
  server.onNotFound(handle_NotFound);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("HTTP server started on port 80");

  // Set up HTTP server routes for AsyncWebServer (port 81)
  asyncServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false);
  });
  asyncServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });
  asyncServer.on("/count", HTTP_GET, [](AsyncWebServerRequest *request){
    File file = SPIFFS.open("/count.json", "r");
    if (!file) {
      request->send(500, "text/plain", "Failed to open count.json");
      return;
    }
    String data = file.readString();
    file.close();
    request->send(200, "application/json", data);
  });
  asyncServer.on("/deleteCount", HTTP_GET, [](AsyncWebServerRequest *request){
    spiffDeleteCount();
    request->send(200, "text/plain", "count.json deleted");
  });
  asyncServer.on("/deleteCred", HTTP_GET, [](AsyncWebServerRequest *request){
    spiffDeleteCred();
    request->send(200, "text/plain", "Cred.txt deleted and recreated");
  });
  asyncServer.on("/deleteAll", HTTP_GET, [](AsyncWebServerRequest *request){
    spiffDeleteAll();
    request->send(200, "text/plain", "All files deleted");
  });
  asyncServer.begin();
  Serial.println("Async HTTP server started on port 81");

  // Add WebSocket handler
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.println("WebSocket client connected");
      client->text(String(touchCount));
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.println("WebSocket client disconnected");
    } else if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        if (strcmp((char*)data, "getCount") == 0) {
          client->text(String(touchCount));
        }
      }
    }
  });
  asyncServer.addHandler(&ws);

  // Configure the touch pin
  pinMode(TOUCH_PIN, INPUT);
  attachInterrupt(TOUCH_PIN, onTouch, FALLING);
}

/**
 * @brief Main loop to handle web server and touch sensor events.
 */
void loop() {
  server.handleClient();

  int touchValue = touchRead(TOUCH_PIN);

  if (touchValue < 40) { // If touch is detected (adjust threshold as needed)
    Serial.println("Touch detected");
    touchCount++;
    Serial.print("Touch count: ");
    Serial.println(touchCount);
    updateTouchCountFile();
    notifyClients(); // Notify WebSocket clients of touch count update
    delay(500); // Debounce delay to prevent rapid triggering
  }
}
