#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <FS.h>

const char* ap_ssid = "TTESP32";
const char* ap_password = "12345678";

IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

WebServer server(80);

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
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

// Open file for reading
void readCredentials() {
  File file = SPIFFS.open("/Cred.txt", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.println("Reading file content:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println("\nFile closed");
}

void setup() {
  Serial.begin(115200);

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");

  // Start the Access Point
  WiFi.softAP(ap_ssid, ap_password);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  delay(100);

  // Set up HTTP server routes
  server.onNotFound(handle_NotFound);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
