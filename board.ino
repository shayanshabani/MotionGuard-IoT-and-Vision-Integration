#include <WiFi.h>
#include <WebServer.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <SD.h>

// WiFi credentials
const char* ssid = "owlmowl";
const char* wifiPassword = "hardPass";

// Control pin
const int relayPin = 15;  
const int trigPin = 5;
const int echoPin = 18;
#define CS 4    // Chip Select pin
#define SCK 14  // Clock pin
#define MISO 2  // Master In Slave Out pin
#define MOSI 13 // Master Out Slave In pin

// MQTT Broker information
const char* mqttServer = "192.168.175.187";
const int mqttPort = 1883; // Default MQTT port
const char* mqttUser = "uname"; // MQTT username
const char* mqttPassword = "upass"; // MQTT password

WiFiClient espClient;
PubSubClient client(espClient);

// Web server object
WebServer server(80);

// NTP Client setup
WiFiUDP udp;
NTPClient timeClient(udp, "pool.ntp.org", 0, 60000);

// Time zone offset (UTC+3:30)
const int timeZoneOffsetHours = 3;
const int timeZoneOffsetMinutes = 30;

// Interval structure
struct Interval {
  int startHour;
  int startMinute;
  int endHour;
  int endMinute;
};

// Store intervals for 7 days
Interval intervals[7];
int enabledDays[7] = {0};


// Function to initialize intervals
void initIntervals() {
  for (int i = 0; i < 7; i++) {
    intervals[i].startHour = 0;   
    intervals[i].startMinute = 0;
    intervals[i].endHour = 0;    
    intervals[i].endMinute = 0;
  }
  // CHECK FOR SAVED CONFIG
  SPI.begin(SCK, MISO, MOSI, CS);
  if (!SD.begin(CS)) {
      Serial.println("Card Mount Failed");
      return;
  }
  File myFile = SD.open("/intervals.txt");
  if (myFile) {
    int i = 0;
    while (myFile.available()) {
      myFile >> intervals[i].startHour;
      myFile >> intervals[i].startMinute;
      myFile >> intervals[i].endHour;
      myFile >> intervals[i].endMinute;
      myFile >> enabledDays[i];
      i++;
    }
    myFile.close();
  } else {
    Serial.println("Error opening file for reading");
  }
}

void saveInvervals(){
  SPI.begin(SCK, MISO, MOSI, CS);

  // Initialize SD card
  if (!SD.begin(CS)) {
      Serial.println("Card Mount Failed");
      return;
  }

  File myFile = SD.open("/intervals.txt", FILE_WRITE);
  if (myFile) {
    for (int i = 0; i < 7; i++) {
      myFile.print(intervals[i].startHour);
      myFile.print(" ");
      myFile.print(intervals[i].startMinute);
      myFile.print(" ");
      myFile.print(intervals[i].endHour);
      myFile.print(" ");
      myFile.print(intervals[i].endMinute);
      myFile.print(" ");
      myFile.print(enabledDays[i]);
      myFile.print("\n");
    }
    myFile.close();
  } else {
    Serial.println("Error opening file for writing");
  }
}


// Function to check if the current time is within the allowed interval
bool isWithinInterval(int dayOfWeek) {
  int currentHour = hour();
  int currentMinute = minute();
  Interval interval = intervals[dayOfWeek];

  if (currentHour > interval.startHour || 
      (currentHour == interval.startHour && currentMinute >= interval.startMinute)) {
    if (currentHour < interval.endHour || 
        (currentHour == interval.endHour && currentMinute < interval.endMinute)) {
      return true;
    }
  }
  return false;
}
const char* daysOfWeek[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
// Function to update relay status
void updateRelayStatus() {
  int currentDay = weekday() - 1;
  // Serial.printf("Current day: %s\n", daysOfWeek[currentDay]);
  // Serial.printf("RELAY : %d\n", digitalRead(relayPin));
  if (enabledDays[currentDay] == 0) {
    digitalWrite(relayPin, LOW);  
    return;
  }
  if (isWithinInterval(currentDay)) {
    digitalWrite(relayPin, HIGH);  
  } else {
    digitalWrite(relayPin, LOW);  
  }
}

// Sync NTP time
void syncTime() {
  timeClient.begin();
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  epochTime += (timeZoneOffsetHours * 3600) + (timeZoneOffsetMinutes * 60);
  setTime(epochTime);
  // Print time
  Serial.printf("Current time: %02d:%02d:%02d\n", hour(), minute(), second());
}

void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("]: ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();
}

// Function to handle web requests (Runs on Core 0)
void handleMovement(void* parameter) {
  static unsigned long lastMovementTime = 0;
  while(1){
    long duration;
    float distanceCm;

    // Send a short HIGH pulse to trigger measurement
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    // Measure the duration of the echo pulse
    duration = pulseIn(echoPin, HIGH);

    // Convert duration to distance (speed of sound: 343 m/s or 0.0343 cm/µs)
    distanceCm = duration * 0.0343 / 2;

    // Print the distance
    // Serial.print("Distance: ");
    // Serial.print(distanceCm);
    // Serial.println(" cm");

    // If the distance is less than 10 cm notice the user
    if (distanceCm < 10) {
      Serial.println("Movement detected!");
      if (millis() - lastMovementTime > 10000) {
        lastMovementTime = millis();
        client.publish("auth/status", "get_pic");
      }
    }

    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(relayPin, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(relayPin, LOW); 

  // Connect to Wi-Fi
  WiFi.begin(ssid, wifiPassword);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Initialize intervals
  initIntervals();

  // Set up MQTT server and callback function
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  while (!client.connected()) {
        Serial.print("Connecting to MQTT...");
        if (client.connect("ESP32Client", mqttUser, mqttPassword)) {
            Serial.println("connected");
            client.subscribe("auth/status"); // Subscribe to the "auth/status" topic
        } else {
            Serial.print("failed with state ");
            Serial.println(client.state());
            delay(2000);
        }
  }

  // Sync time initially
  syncTime();

  // Serve web page
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>";
    html += ":root { --primary-color: #2563eb; --secondary-color: #1e40af; --background-color: #f8fafc; "
            "--card-background: #ffffff; --text-color: #1e293b; --border-radius: 8px; "
            "--shadow: 0 4px 6px -1px rgb(0 0 0 / 0.1); }";
    html += "* { margin: 0; padding: 0; box-sizing: border-box; font-family: system-ui, -apple-system, sans-serif; }";
    html += "body { background-color: var(--background-color); color: var(--text-color); padding: 1rem; "
            "line-height: 1.5; max-width: 800px; margin: 0 auto; }";
    html += ".image-section { text-align: center; margin-bottom: 2rem; background-color: var(--card-background); "
            "padding: 2rem; border-radius: var(--border-radius); box-shadow: var(--shadow); }";
    html += ".image-title { font-size: 2rem; color: var(--primary-color); margin-bottom: 1rem; "
            "text-transform: uppercase; letter-spacing: 2px; font-weight: bold; "
            "text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.1); }";
    html += ".captured-image { max-width: 100%; height: auto; border: 8px solid var(--primary-color); "
            "border-radius: var(--border-radius); box-shadow: 0 6px 12px -2px rgba(0, 0, 0, 0.2); }";

    html += "h1 { text-align: center; color: var(--primary-color); font-size: 2rem; margin: 1rem 0; }";
    html += "h2 { color: var(--secondary-color); font-size: 1.5rem; margin: 1.5rem 0 1rem; }";
    html += "h3 { color: var(--text-color); font-size: 1.2rem; margin: 1rem 0; }";
    html += ".status-card { background-color: var(--card-background); padding: 1.5rem; "
            "border-radius: var(--border-radius); box-shadow: var(--shadow); margin-bottom: 1rem; text-align: center; }";
    html += ".current-time { font-size: 2rem; font-weight: bold; color: var(--primary-color); }";
    html += ".relay-status { display: inline-block; padding: 0.5rem 1rem; border-radius: var(--border-radius); "
            "background-color: #ef4444; color: white; font-weight: bold; margin-top: 1rem; }";
    html += ".relay-status.on { background-color: #22c55e; }";
    html += "form { background-color: var(--card-background); padding: 1rem; border-radius: var(--border-radius); "
            "box-shadow: var(--shadow); margin-bottom: 1rem; }";
    html += "input[type='number'] { padding: 0.5rem; border: 1px solid #e2e8f0; border-radius: 4px; width: 4rem; }";
    html += "input[type='submit'] { background-color: var(--primary-color); color: white; padding: 0.5rem 1rem; "
            "border: none; border-radius: 4px; cursor: pointer; margin-left: 0.5rem; }";
    html += "input[type='submit']:hover { background-color: var(--secondary-color); }";
    html += ".interval-status { padding: 1rem; background-color: var(--card-background); "
            "border-radius: var(--border-radius); margin-bottom: 1rem; }";
    html += "</style></head><body>";

    
    
    // Header and Current Time
    html += "<h1>Relay Control</h1>";
    html += "<div class='status-card'>";
    html += "<div id='current-time' class='current-time'>" + String(hour()) + ":" + String(minute()) + ":" + String(second()) + "</div>";
    html += "<div class='relay-status " + String(digitalRead(relayPin) == HIGH ? "on" : "") + "'>";
    html += "Relay is " + String(digitalRead(relayPin) == HIGH ? "ON" : "OFF") + "</div></div>";

    // Add this JavaScript just before the closing </body> tag
    html += "<script>";
    html += "function updateTime() {";
    html += "    const timeElement = document.getElementById('current-time');";
    html += "    const now = new Date();";
    html += "    const hours = String(now.getHours()).padStart(2, '0');";
    html += "    const minutes = String(now.getMinutes()).padStart(2, '0');";
    html += "    const seconds = String(now.getSeconds()).padStart(2, '0');";
    html += "    timeElement.textContent = `${hours}:${minutes}:${seconds}`;";
    html += "}";
    html += "updateTime();";
    html += "setInterval(updateTime, 1000);";
    html += "</script>";
    
    // Interval Settings Section
    html += "<h2>Set Intervals for Each Day</h2>";
    for (int i = 0; i < 7; i++) {
        html += "<form action='setInterval' method='get'>";
        html += "<h3>" + String(daysOfWeek[i]) + "</h3>";
        html += "<input type='hidden' name='day' value='" + String(i) + "'>";
        html += "Start: <input type='number' name='startHour' min='0' max='23' value='" + String(intervals[i].startHour) + "'>:";
        html += "<input type='number' name='startMinute' min='0' max='59' value='" + String(intervals[i].startMinute) + "'>";
        html += " End: <input type='number' name='endHour' min='0' max='23' value='" + String(intervals[i].endHour) + "'>:";
        html += "<input type='number' name='endMinute' min='0' max='59' value='" + String(intervals[i].endMinute) + "'>";
        html += "<input type='submit' value='Set Interval'>";
        html += "</form>";
    }
    
    // Enabled Intervals Section
    html += "<h2>Enabled Intervals</h2>";
    for (int i = 0; i < 7; i++) {
        html += "<div class='interval-status'>";
        html += "<h3>" + String(daysOfWeek[i]) + "</h3>";
        if (enabledDays[i] == 1) {
            html += "<p>Interval is enabled: " + String(intervals[i].startHour) + ":" + 
                    String(intervals[i].startMinute) + " - " + String(intervals[i].endHour) + ":" + 
                    String(intervals[i].endMinute) + "</p>";
            html += "<form action='disableInterval' method='get'>";
            html += "<input type='hidden' name='day' value='" + String(i) + "'>";
            html += "<input type='submit' value='Disable Interval'>";
            html += "</form>";
        } else {
            html += "<p>Interval is disabled</p>";
        }
        html += "</div>";
    }
    
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

// Set Interval handler with styled response
  server.on("/setInterval", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>";
    html += "body { font-family: system-ui, -apple-system, sans-serif; background-color: #f8fafc; "
            "display: flex; flex-direction: column; align-items: center; justify-content: center; "
            "min-height: 100vh; margin: 0; padding: 1rem; }";
    html += "h1 { color: #2563eb; }";
    html += "a { color: #2563eb; text-decoration: none; padding: 0.5rem 1rem; border: 2px solid #2563eb; "
            "border-radius: 4px; margin-top: 1rem; display: inline-block; }";
    html += "a:hover { background-color: #2563eb; color: white; }";
    html += "</style></head><body>";

    if (server.hasArg("day") && server.hasArg("startHour") && server.hasArg("startMinute") &&
        server.hasArg("endHour") && server.hasArg("endMinute")) {
        int day = server.arg("day").toInt();
        
        if (day < 0 || day > 6) {
            html += "<h1>Error: Invalid Day</h1>";
        } else {
            intervals[day].startHour = server.arg("startHour").toInt();
            intervals[day].startMinute = server.arg("startMinute").toInt();
            intervals[day].endHour = server.arg("endHour").toInt();
            intervals[day].endMinute = server.arg("endMinute").toInt();
            enabledDays[day] = 1;
            html += "<h1>Interval Set!</h1>";
        }
    }
    
    html += "<a href='/'>Return to Main Page</a></body></html>";
    server.send(200, "text/html", html);
  });

// Disable Interval handler with styled response
  server.on("/disableInterval", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>";
    html += "body { font-family: system-ui, -apple-system, sans-serif; background-color: #f8fafc; "
            "display: flex; flex-direction: column; align-items: center; justify-content: center; "
            "min-height: 100vh; margin: 0; padding: 1rem; }";
    html += "h1 { color: #2563eb; }";
    html += "a { color: #2563eb; text-decoration: none; padding: 0.5rem 1rem; border: 2px solid #2563eb; "
            "border-radius: 4px; margin-top: 1rem; display: inline-block; }";
    html += "a:hover { background-color: #2563eb; color: white; }";
    html += "</style></head><body>";

    if (server.hasArg("day")) {
        int day = server.arg("day").toInt();
        
        if (day < 0 || day > 6) {
            html += "<h1>Error: Invalid Day</h1>";
        } else {
            enabledDays[day] = 0;
            html += "<h1>Interval Disabled!</h1>";
        }
    }
    
    html += "<a href='/'>Return to Main Page</a></body></html>";
    server.send(200, "text/html", html);
  });



  // Start the web server
  server.begin();

  // Run web server handling on Core 0
  xTaskCreatePinnedToCore(
    handleMovement,  // Function to run
    "movementTask",    // Task name
    10000,              // Stack size
    NULL,               // Parameters
    1,                  // Priority
    NULL,               // Task handle
    0                   // Core 0
  );
}

void loop() {
  // Main loop runs on Core 1
  static unsigned long lastSyncTime = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastSyncTime >= 30000) {  
    syncTime();
    lastSyncTime = currentMillis;
  }

  // Update relay status every second instead of 60 seconds!
  updateRelayStatus();
  server.handleClient();
  client.loop();
  delay(10);
}
