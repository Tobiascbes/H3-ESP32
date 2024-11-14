#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <FS.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"

const char* ap_ssid = "TTESP32";
const char* ap_password = "12345678";

String savedSSID;
String savedPassword;

IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

WebServer server(80); // WebServer on port 80
AsyncWebServer asyncServer(81); // AsyncWebServer on port 81
AsyncWebSocket ws("/ws"); // WebSocket server

int buttonPin = 21;
int buttonState = 0;

#define TOUCH_PIN T0 // Touch pin
int touchCount = 0; // Variable to hold the touch count

// HTML code for the soft Access point web:
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

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

void handleRoot() {
  server.send(200, "text/html", html_form);
}

void readSpiff() {
  File file = SPIFFS.open("/Cred.txt", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.println("Reading file content:");
  while (file.available()) {
    String line = file.readStringUntil('\n');
    Serial.println(line);
    if (line.startsWith("SSID: ")) {
      savedSSID = line.substring(6);
      savedSSID.trim(); // Trim any leading or trailing spaces
    } else if (line.startsWith("Password: ")) {
      savedPassword = line.substring(10);
      savedPassword.trim(); // Trim any leading or trailing spaces
    }
  }
  file.close();
  Serial.println("\nFile closed");

  // Print the extracted SSID and Password
  Serial.println("Extracted SSID: " + savedSSID);
  Serial.println("Extracted Password: " + savedPassword);
}

void handleSave() {
  // Check if both SSID and password fields are present
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String input_ssid = server.arg("ssid");
    String input_password = server.arg("password");

    // Open file for writing
    File file = SPIFFS.open("/Cred.txt", "w");
    if (!file) {
      server.send(500, "text/plain", "Failed to open file for writing");
      Serial.println("Failed to open file for writing");
      return;
    }

    // Write SSID and Password to file
    file.println("SSID: " + input_ssid);
    file.println("Password: " + input_password);
    file.close();

    server.send(200, "text/plain", "Credentials saved successfully");
    Serial.println("Credentials saved successfully:");
    Serial.println("SSID: " + input_ssid);
    Serial.println("Password: " + input_password);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

void notifyClients() {
  ws.textAll(String(touchCount));
  Serial.println("Notifying WebSocket clients of touch count: " + String(touchCount)); // Debug log
}

void updateTouchCountFile() {
  File file = SPIFFS.open("/count.txt", "w");
  if (!file) {
    Serial.println("Failed to open count.txt for writing");
    return;
  }
  file.println(touchCount);
  file.close();

  // Debug: Print the contents of count.txt to the serial monitor
  file = SPIFFS.open("/count.txt", "r");
  if (file) {
    Serial.println("Contents of count.txt:");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      Serial.println(line);
    }
    file.close();
  } else {
    Serial.println("Failed to open count.txt for reading");
  }
}

void onTouch() {
  touchCount++;
  updateTouchCountFile();
  notifyClients();
}

void spiffDeleteCount() {
  if (SPIFFS.remove("/count.txt")) {
    Serial.println("count.txt deleted successfully");
  } else {
    Serial.println("Failed to delete count.txt");
  }
  touchCount = 0; // Reset touch count
}

void spiffDeleteCred() {
  if (SPIFFS.remove("/Cred.txt")) {
    Serial.println("Cred.txt deleted successfully");
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
void spiffDeleteAll() {
  spiffDeleteCount();
  spiffDeleteCred();
}

void setup() {
  // Add a small delay at the beginning of setup
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
  File file = SPIFFS.open("/count.txt", "r");
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
    request->send(SPIFFS, "/style.css","text/css");
  });
  asyncServer.on("/count", HTTP_GET, [](AsyncWebServerRequest *request){
    File file = SPIFFS.open("/count.txt", "r");
    if (!file) {
      request->send(500, "text/plain", "Failed to open count.txt");
      return;
    }
    String count = file.readStringUntil('\n');
    file.close();
    request->send(200, "text/plain", count);
  });
  asyncServer.on("/deleteCount", HTTP_GET, [](AsyncWebServerRequest *request){
    spiffDeleteCount();
    request->send(200, "text/plain", "count.txt deleted");
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

void loop() {
  server.handleClient();



  int touchValue = touchRead(TOUCH_PIN);

  if (touchValue < 40) {  // If touch is detected (adjust threshold as needed)
    Serial.println("Touch detected");
    touchCount++;
    Serial.print("Touch count: ");
    Serial.println(touchCount);
    updateTouchCountFile();
    notifyClients(); // Notify WebSocket clients of touch count update
    delay(500); // Debounce delay to prevent rapid triggering
  }
}
