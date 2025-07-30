
/* 
 * ===============================================================
 *                       ESP32 GPS TRACKER
 * ===============================================================
 * 
 * FEATURES:
 * - Real-time GPS tracking with TinyGPS++ library
 * - WiFi connectivity with auto-reconnection
 * - Email alerts with HTML format and map preview
 * - Google Maps integration with location markers
 * - Periodic location updates
 * - Visual status indicators with LEDs
 * - Clean and optimized code structure
 * 
 * HARDWARE REQUIREMENTS:
 * - ESP32 development board
 * - GPS module (NMEA compatible)
 * - WiFi network access
 * - 3 LEDs (Red, Green, Blue) with 220Ω resistors
 * 
 * PIN CONNECTIONS:
 * - GPS RX: GPIO16
 * - GPS TX: GPIO17
 * - WiFi Status LED (Blue): GPIO2
 * - GPS Status LED (Green): GPIO4
 * - System Status LED (Red): GPIO5
 * 
 * LED STATUS INDICATORS:
 * - Blue LED (WiFi): Solid = Connected, Blinking = Connecting, Off = Disconnected
 * - Green LED (GPS): Solid = GPS Lock, Blinking = Searching, Off = No GPS
 * - Red LED (System): Solid = Error, Blinking = Warning, Off = Normal
 * 
 * ===============================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP_Mail_Client.h>
#include <TinyGPS++.h>
#include <GeoLinker.h>
#include <time.h>

// ==================================================================
//                    HARDWARE CONFIGURATION
// ==================================================================

// GPS Serial Communication Setup
HardwareSerial gpsSerial(1);
#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 9600

// LED Pin Definitions
#define LED_WIFI_PIN 2    // Blue LED for WiFi status
#define LED_GPS_PIN 4     // Green LED for GPS status
#define LED_SYSTEM_PIN 5  // Red LED for system status

// LED States
enum LEDState {
  LED_OFF = 0,
  LED_ON = 1,
  LED_BLINK_SLOW = 2,
  LED_BLINK_FAST = 3
};

// LED Status Variables
LEDState wifiLEDState = LED_OFF;
LEDState gpsLEDState = LED_OFF;
LEDState systemLEDState = LED_OFF;

unsigned long lastLEDUpdate = 0;
bool ledBlinkState = false;
const unsigned long LED_BLINK_INTERVAL = 500;       // 500ms for slow blink
const unsigned long LED_FAST_BLINK_INTERVAL = 200;  // 200ms for fast blink

// ==================================================================
//                    NETWORK CONFIGURATION
// ==================================================================

// WiFi Credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ==================================================================
//                   EMAIL CONFIGURATION (Gmail SMTP)
// ==================================================================

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "your-email@gmail.com"
#define AUTHOR_PASSWORD "your-app-password"  // Use App Password, not regular password

// Email Recipients
const char* emailRecipients[][2] = {
  { "Primary Contact", "prolayjitbiswas14112004@gmail.com" },
  { "Friend", "your.name@gmail.com" }
};

// ==================================================================
//                   GEOLINKER CONFIGURATION
// ==================================================================

const char* geoApiKey = "YOUR_GEOLINKER_API_KEY";
const char* deviceID = "YOUR_DEVICE_ID";
const uint16_t updateInterval = 30;  // seconds for cloud updates
const bool enableOfflineStorage = true;
const uint8_t offlineBufferLimit = 50;
const bool enableAutoReconnect = true;
const int8_t timeOffsetHours = 5;
const int8_t timeOffsetMinutes = 30;

// ==================================================================
//                    GPS & TIMING CONFIGURATION
// ==================================================================

TinyGPSPlus gps;
SMTPSession smtp;
GeoLinker geo;

// GPS Data Storage
double currentLatitude = 0.0;
double currentLongitude = 0.0;
bool gpsDataValid = false;
bool firstGPSLock = true;
String currentLocationName = "Unknown Location";

// Timing Variables
unsigned long lastLocationSend = 0;
const unsigned long LOCATION_SEND_INTERVAL = 300000;  // 5 minutes

// ==================================================================
//                    FUNCTION DECLARATIONS
// ==================================================================

void connectToWiFi();
bool syncTime();
void initializeGeoLinker();
void updateGPSData();
void sendLocationEmail();
String createLocationHTML();
String getAddressFromCoordinates(double lat, double lng);
String getCurrentTimestamp();
void handleGeoLinkerStatus(uint8_t status);

// LED Control Functions
void initializeLEDs();
void updateLEDs();
void setWiFiLED(LEDState state);
void setGPSLED(LEDState state);
void setSystemLED(LEDState state);
void controlLED(int pin, LEDState state);

// ==================================================================
//                    LED CONTROL FUNCTIONS
// ==================================================================

void initializeLEDs() {
  pinMode(LED_WIFI_PIN, OUTPUT);
  pinMode(LED_GPS_PIN, OUTPUT);
  pinMode(LED_SYSTEM_PIN, OUTPUT);

  // Initial LED test sequence
  Serial.println("🔄 Testing LEDs...");

  // Test each LED
  digitalWrite(LED_WIFI_PIN, HIGH);
  delay(300);
  digitalWrite(LED_WIFI_PIN, LOW);

  digitalWrite(LED_GPS_PIN, HIGH);
  delay(300);
  digitalWrite(LED_GPS_PIN, LOW);

  digitalWrite(LED_SYSTEM_PIN, HIGH);
  delay(300);
  digitalWrite(LED_SYSTEM_PIN, LOW);

  Serial.println("✓ LED test complete");
}

void updateLEDs() {
  unsigned long currentTime = millis();

  // Update blink state for all LEDs
  if (currentTime - lastLEDUpdate >= LED_BLINK_INTERVAL) {
    ledBlinkState = !ledBlinkState;
    lastLEDUpdate = currentTime;
  }

  // Control each LED based on its state
  controlLED(LED_WIFI_PIN, wifiLEDState);
  controlLED(LED_GPS_PIN, gpsLEDState);
  controlLED(LED_SYSTEM_PIN, systemLEDState);
}

void controlLED(int pin, LEDState state) {
  switch (state) {
    case LED_OFF:
      digitalWrite(pin, LOW);
      break;
    case LED_ON:
      digitalWrite(pin, HIGH);
      break;
    case LED_BLINK_SLOW:
      digitalWrite(pin, ledBlinkState ? HIGH : LOW);
      break;
    case LED_BLINK_FAST:
      // Fast blink using different timing
      digitalWrite(pin, (millis() % (LED_FAST_BLINK_INTERVAL * 2)) < LED_FAST_BLINK_INTERVAL ? HIGH : LOW);
      break;
  }
}

void setWiFiLED(LEDState state) {
  wifiLEDState = state;
  String stateStr = "";
  switch (state) {
    case LED_OFF: stateStr = "OFF (Disconnected)"; break;
    case LED_ON: stateStr = "ON (Connected)"; break;
    case LED_BLINK_SLOW: stateStr = "BLINKING (Connecting)"; break;
    case LED_BLINK_FAST: stateStr = "FAST BLINK (Error)"; break;
  }
  Serial.println("📶 WiFi LED: " + stateStr);
}

void setGPSLED(LEDState state) {
  gpsLEDState = state;
  String stateStr = "";
  switch (state) {
    case LED_OFF: stateStr = "OFF (No GPS)"; break;
    case LED_ON: stateStr = "ON (GPS Lock)"; break;
    case LED_BLINK_SLOW: stateStr = "BLINKING (Searching)"; break;
    case LED_BLINK_FAST: stateStr = "FAST BLINK (GPS Error)"; break;
  }
  Serial.println("🛰️ GPS LED: " + stateStr);
}

void setSystemLED(LEDState state) {
  systemLEDState = state;
  String stateStr = "";
  switch (state) {
    case LED_OFF: stateStr = "OFF (Normal)"; break;
    case LED_ON: stateStr = "ON (Critical Error)"; break;
    case LED_BLINK_SLOW: stateStr = "BLINKING (Warning)"; break;
    case LED_BLINK_FAST: stateStr = "FAST BLINK (System Error)"; break;
  }
  Serial.println("⚠️ System LED: " + stateStr);
}

// ==================================================================
//                    INITIALIZATION SETUP
// ==================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP32 GPS Tracker System with LED Status ===");
  Serial.println("Initializing components...");

  // Initialize LEDs first
  initializeLEDs();
  setSystemLED(LED_BLINK_SLOW);  // System starting up

  // Initialize GPS
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("✓ GPS Serial initialized");
  setGPSLED(LED_BLINK_SLOW);  // GPS searching

  // Initialize WiFi
  setWiFiLED(LED_BLINK_SLOW);  // WiFi connecting
  connectToWiFi();

  // Initialize time synchronization
  if (!syncTime()) {
    Serial.println("⚠ Time sync failed - continuing without proper timestamps");
    setSystemLED(LED_BLINK_SLOW);  // Warning state
  } else {
    setSystemLED(LED_OFF);  // Normal operation
  }

  // Initialize GeoLinker for data visualization
  initializeGeoLinker();

  Serial.println("\n=== System Ready ===");
  Serial.println("GPS tracking active...");
  Serial.println("Sending location updates every 5 minutes");
  Serial.println("GeoLinker cloud visualization: https://geolinker.org");
  Serial.println("Device ID: " + String(deviceID));
  Serial.println("\nLED Status Indicators:");
  Serial.println("🔵 Blue LED (GPIO2): WiFi Status");
  Serial.println("🟢 Green LED (GPIO4): GPS Status");
  Serial.println("🔴 Red LED (GPIO5): System Status\n");
}

// ==================================================================
//                    WIFI CONNECTION FUNCTIONS
// ==================================================================

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("🔌 Connecting to WiFi");
  setWiFiLED(LED_BLINK_SLOW);  // Connecting state

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
    updateLEDs();  // Keep LEDs updating during connection
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    setWiFiLED(LED_ON);  // Connected state
  } else {
    Serial.println("\n❌ WiFi connection failed!");
    setWiFiLED(LED_BLINK_FAST);  // Error state
  }
}

bool syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("⏳ Syncing time");

  time_t now = time(nullptr);
  unsigned long start = millis();

  while (now < 8 * 3600 * 2 && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
    updateLEDs();  // Keep LEDs updating during time sync
    now = time(nullptr);
  }

  if (now < 8 * 3600 * 2) {
    Serial.println("\n❌ Time sync failed.");
    return false;
  }

  Serial.println("\n✅ Time synced successfully!");
  return true;
}

// ==================================================================
//                    GEOLINKER INITIALIZATION
// ==================================================================

void initializeGeoLinker() {
  Serial.println("🌐 Initializing GeoLinker for data visualization...");

  geo.begin(gpsSerial);
  geo.setApiKey(geoApiKey);
  geo.setDeviceID(deviceID);
  geo.setUpdateInterval_seconds(updateInterval);
  geo.setDebugLevel(DEBUG_BASIC);
  geo.enableOfflineStorage(enableOfflineStorage);
  geo.enableAutoReconnect(enableAutoReconnect);
  geo.setOfflineBufferLimit(offlineBufferLimit);
  geo.setTimeOffset(timeOffsetHours, timeOffsetMinutes);
  geo.setNetworkMode(GEOLINKER_WIFI);
  geo.setWiFiCredentials(ssid, password);

  if (WiFi.status() == WL_CONNECTED) {
    if (geo.connectToWiFi()) {
      Serial.println("✓ GeoLinker WiFi connected");
      Serial.println("📊 Data visualization available at: https://www.circuitdigest.cloud/geolinker-online-gps-visualizer");
      Serial.println("🔑 Your tracking dashboard: Device ID - " + String(deviceID));
    } else {
      Serial.println("⚠ GeoLinker WiFi connection failed - will retry automatically");
      setSystemLED(LED_BLINK_SLOW);  // Warning state
    }
  }

  Serial.println("✓ GeoLinker initialized for cloud visualization");
}

// ==================================================================
//                    GPS DATA PROCESSING
// ==================================================================

void updateGPSData() {
  bool gpsDataReceived = false;

  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    gpsDataReceived = true;

    if (gps.encode(c)) {
      if (gps.location.isValid()) {
        currentLatitude = gps.location.lat();
        currentLongitude = gps.location.lng();
        gpsDataValid = true;

        // GPS lock achieved
        setGPSLED(LED_ON);

        Serial.print("📍 GPS: ");
        Serial.print(currentLatitude, 6);
        Serial.print(", ");
        Serial.print(currentLongitude, 6);
        Serial.print(" | Satellites: ");
        Serial.println(gps.satellites.value());

        // Send first GPS lock notification
        if (firstGPSLock) {
          firstGPSLock = false;
          Serial.println("🎯 First GPS lock acquired!");
          sendLocationEmail();
        }
      } else {
        // GPS data received but no valid location
        setGPSLED(LED_BLINK_SLOW);
      }
    }
  }

  // Check if GPS module is responding
  static unsigned long lastGPSData = 0;
  if (gpsDataReceived) {
    lastGPSData = millis();
  } else if (millis() - lastGPSData > 5000) {  // No GPS data for 5 seconds
    setGPSLED(LED_OFF);                        // No GPS communication
  }
}
// ==================================================================
//                    EMAIL SYSTEM
// ==================================================================

void sendLocationEmail() {
  if (WiFi.status() != WL_CONNECTED || !gpsDataValid) {
    Serial.println("❌ Cannot send email - WiFi or GPS not available");
    return;
  }

  Serial.println("📧 Preparing location email...");

  // Get current address
  currentLocationName = getAddressFromCoordinates(currentLatitude, currentLongitude);

  // Configure SMTP
  ESP_Mail_Session session;
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "";
  session.secure.startTLS = false;

  // Create message
  SMTP_Message message;
  message.subject = "📍 GPS Tracker Location Update - " + getCurrentTimestamp();
  message.sender.name = "GPS Tracker System";
  message.sender.email = AUTHOR_EMAIL;

  // Add recipients
  int recipientCount = sizeof(emailRecipients) / sizeof(emailRecipients[0]);
  for (int i = 0; i < recipientCount; i++) {
    message.addRecipient(emailRecipients[i][0], emailRecipients[i][1]);
    Serial.println("📨 Adding recipient: " + String(emailRecipients[i][1]));
  }

  // Create HTML email body
  String htmlBody = createLocationHTML();
  message.html.content = htmlBody;
  message.html.charSet = "utf-8";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  // Send email
  Serial.println("📡 Connecting to SMTP server...");
  if (!smtp.connect(&session)) {
    Serial.println("❌ SMTP connection failed: " + smtp.errorReason());
    return;
  }

  Serial.println("📤 Sending location email...");
  if (!MailClient.sendMail(&smtp, &message, true)) {
    Serial.println("❌ Email send failed: " + smtp.errorReason());
  } else {
    Serial.println("✅ Location email sent successfully!");
  }

  smtp.closeSession();
}

// ==================================================================
//                    HTML EMAIL TEMPLATE
// ==================================================================

String createLocationHTML() {
  String googleMapsURL = "https://www.google.com/maps/search/?api=1&query=" + String(currentLatitude, 6) + "," + String(currentLongitude, 6);

  // Using LocationIQ API instead of Google Maps API
  const String locationIQ_API_KEY = "YOUR_LOCATIONIQ_API_KEY";
  String staticMapURL = "https://maps.locationiq.com/v3/staticmap?key=" + locationIQ_API_KEY + "&center=" + String(currentLatitude, 6) + "," + String(currentLongitude, 6) + "&zoom=16&size=600x300&markers=icon:large-red-cutout|" + String(currentLatitude, 6) + "," + String(currentLongitude, 6) + "&format=png";

  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>GPS Location Update</title>";
  html += "<style>";

  // CSS Styling
  html += "* { margin: 0; padding: 0; box-sizing: border-box; }";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Arial, sans-serif; background: #f5f5f5; color: #333; line-height: 1.6; }";
  html += ".email-container { max-width: 600px; margin: 20px auto; background: #ffffff; border-radius: 12px; overflow: hidden; box-shadow: 0 4px 20px rgba(0,0,0,0.1); }";

  // Header
  html += ".header { background: linear-gradient(135deg, #4285f4 0%, #34a853 100%); color: white; padding: 30px 20px; text-align: center; }";
  html += ".header h1 { font-size: 28px; font-weight: 700; margin-bottom: 8px; }";
  html += ".header .subtitle { font-size: 16px; opacity: 0.9; font-weight: 400; }";

  // Location Info Section
  html += ".location-info { padding: 30px 20px; background: #ffffff; }";
  html += ".info-row { display: flex; align-items: center; margin-bottom: 16px; padding: 12px 0; border-bottom: 1px solid #f0f0f0; }";
  html += ".info-row:last-child { border-bottom: none; margin-bottom: 0; }";
  html += ".info-icon { font-size: 20px; margin-right: 15px; width: 25px; text-align: center; }";
  html += ".info-label { font-weight: 600; color: #555; min-width: 120px; margin-right: 15px; }";
  html += ".info-value { color: #333; font-weight: 500; flex: 1; }";

  // Map Section
  html += ".map-section { padding: 0 20px 30px 20px; }";
  html += ".map-header { text-align: center; margin-bottom: 20px; }";
  html += ".map-header h2 { color: #333; font-size: 22px; font-weight: 600; margin-bottom: 8px; }";
  html += ".map-header p { color: #666; font-size: 14px; }";

  html += ".map-container { position: relative; border-radius: 12px; overflow: hidden; box-shadow: 0 4px 15px rgba(0,0,0,0.1); margin-bottom: 20px; cursor: pointer; transition: transform 0.3s ease; }";
  html += ".map-container:hover { transform: scale(1.02); }";
  html += ".map-container img { width: 100%; height: 300px; object-fit: cover; display: block; transition: opacity 0.3s; }";
  html += ".map-overlay { position: absolute; top: 0; left: 0; right: 0; bottom: 0; background: rgba(66, 133, 244, 0.15); display: flex; align-items: center; justify-content: center; opacity: 0; transition: all 0.3s; cursor: pointer; }";
  html += ".map-container:hover .map-overlay { opacity: 1; }";
  html += ".map-overlay span { background: rgba(66, 133, 244, 0.95); color: white; padding: 12px 24px; border-radius: 25px; font-size: 14px; font-weight: 600; box-shadow: 0 4px 15px rgba(0,0,0,0.2); }";
  html += ".map-overlay::before { content: '🗺️'; font-size: 24px; margin-right: 8px; }";

  // Action Button
  html += ".action-button { display: block; width: 100%; max-width: 300px; margin: 0 auto; padding: 15px 20px; background: #4285f4; color: white; text-decoration: none; border-radius: 8px; text-align: center; font-weight: 600; font-size: 16px; transition: all 0.3s; }";
  html += ".action-button:hover { background: #3367d6; transform: translateY(-2px); box-shadow: 0 6px 20px rgba(66, 133, 244, 0.3); }";
  html += ".action-button .icon { margin-right: 8px; }";

  // Footer
  html += ".footer { padding: 20px; background: #f8f9fa; text-align: center; color: #666; font-size: 12px; }";
  html += ".footer p { margin-bottom: 5px; }";
  html += ".footer .brand { color: #4285f4; font-weight: 600; }";

  // Animations
  html += "@keyframes fadeIn { from { opacity: 0; transform: translateY(20px); } to { opacity: 1; transform: translateY(0); } }";
  html += ".email-container { animation: fadeIn 0.6s ease-out; }";

  html += "</style></head><body><div class='email-container'>";

  // HEADER
  html += "<header class='header'><h1>📍 GPS Location Update</h1>";
  html += "<div class='subtitle'>Real-time tracking information</div></header>";

  // LOCATION INFO
  html += "<section class='location-info'>";
  html += "<div class='info-row'><span class='info-icon'>⏰</span><span class='info-label'>Timestamp:</span><span class='info-value'>" + getCurrentTimestamp() + "</span></div>";
  html += "<div class='info-row'><span class='info-icon'>🌍</span><span class='info-label'>Coordinates:</span><span class='info-value'>" + String(currentLatitude, 6) + ", " + String(currentLongitude, 6) + "</span></div>";
  html += "<div class='info-row'><span class='info-icon'>📍</span><span class='info-label'>Location:</span><span class='info-value'>" + currentLocationName + "</span></div>";

  html += "<div class='info-row'><span class='info-icon'>📊</span><span class='info-label'>Visualization:</span><span class='info-value'><a href='https://geolinker.org' target='_blank' style='color: #4285f4; text-decoration: none;'>View Live Tracking Dashboard</a></span></div>";
  html += "<div class='info-row'><span class='info-icon'>🆔</span><span class='info-label'>Device ID:</span><span class='info-value'>" + String(deviceID) + "</span></div>";

  if (gps.satellites.isValid()) {
    html += "<div class='info-row'><span class='info-icon'>🛰️</span><span class='info-label'>Satellites:</span><span class='info-value'>" + String(gps.satellites.value()) + " connected</span></div>";
  }

  if (gps.speed.isValid()) {
    html += "<div class='info-row'><span class='info-icon'>🚗</span><span class='info-label'>Speed:</span><span class='info-value'>" + String(gps.speed.kmph(), 1) + " km/h</span></div>";
  }

  html += "</section>";

  // MAP SECTION
  html += "<section class='map-section'>";
  html += "<div class='map-header'><h2>Current Location</h2>";
  html += "<p>Click the map or button below to view in Google Maps</p></div>";

  html += "<a href='" + googleMapsURL + "' class='map-container' target='_blank' title='Click to open location in Google Maps'>";
  html += "<img src='" + staticMapURL + "' alt='GPS Location Map Preview' onerror=\"this.src='data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iNjAwIiBoZWlnaHQ9IjMwMCIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj48ZGVmcz48bGluZWFyR3JhZGllbnQgaWQ9ImJnIiB4MT0iMCUiIHkxPSIwJSIgeDI9IjEwMCUiIHkyPSIxMDAlIj48c3RvcCBvZmZzZXQ9IjAlIiBzdHlsZT0ic3RvcC1jb2xvcjojNDI4NWY0O3N0b3Atb3BhY2l0eTowLjEiLz48c3RvcCBvZmZzZXQ9IjEwMCUiIHN0eWxlPSJzdG9wLWNvbG9yOiMzNGE4NTM7c3RvcC1vcGFjaXR5OjAuMSIvPjwvbGluZWFyR3JhZGllbnQ+PC9kZWZzPjxyZWN0IHdpZHRoPSIxMDAlIiBoZWlnaHQ9IjEwMCUiIGZpbGw9InVybCgjYmcpIi8+PGNpcmNsZSBjeD0iMzAwIiBjeT0iMTUwIiByPSI0MCIgZmlsbD0iI2ZmNDQ0NCIgb3BhY2l0eT0iMC44Ii8+PGNpcmNsZSBjeD0iMzAwIiBjeT0iMTUwIiByPSIyMCIgZmlsbD0iI2ZmZmZmZiIvPjx0ZXh0IHg9IjUwJSIgeT0iNjAlIiBmb250LWZhbWlseT0iQXJpYWwiIGZvbnQtc2l6ZT0iMTYiIGZpbGw9IiM2NjYiIHRleHQtYW5jaG9yPSJtaWRkbGUiIGR5PSIuM2VtIj5NYXAgUHJldmlldyBVbmF2YWlsYWJsZTwvdGV4dD48dGV4dCB4PSI1MCUiIHk9Ijc1JSIgZm9udC1mYW1pbHk9IkFyaWFsIiBmb250LXNpemU9IjEyIiBmaWxsPSIjOTk5IiB0ZXh0LWFuY2hvcj0ibWlkZGxlIiBkeT0iLjNlbSI+Q2xpY2sgdG8gdmlldyBpbiBHb29nbGUgTWFwczwvdGV4dD48L3N2Zz4='\">";
  html += "<div class='map-overlay'><span>Click to Open in Google Maps</span></div></a>";

  html += "<a href='" + googleMapsURL + "' class='action-button' target='_blank'>";
  html += "<span class='icon'>🗺️</span>View Full Map & Directions</a>";

  html += "<br><a href='https://www.circuitdigest.cloud/geolinker-online-gps-visualizer' class='action-button' target='_blank' style='background: #34a853; margin-top: 15px;'>";
  html += "<span class='icon'>📊</span>View Live Tracking Dashboard</a>";

  html += "</section>";

  // FOOTER
  html += "<footer class='footer'>";
  html += "<p><span class='brand'>ESP32 GPS Tracker with GeoLinker</span></p>";
  html += "<p>Automated location update • Live visualization at <a href='https://www.circuitdigest.cloud/geolinker-online-gps-visualizer' style='color: #4285f4;'>GeoLinker.org</a></p>";
  html += "<p>Device ID: " + String(deviceID) + " | " + getCurrentTimestamp() + "</p>";
  html += "</footer></div></body></html>";

  return html;
}

// ==================================================================
//                    ADDRESS LOOKUP
// ==================================================================

String getAddressFromCoordinates(double lat, double lng) {
  if (WiFi.status() != WL_CONNECTED) {
    return "Network unavailable";
  }

  WiFiClientSecure client;
  client.setInsecure();

  String url = "/reverse?format=json&lat=" + String(lat, 6) + "&lon=" + String(lng, 6) + "&zoom=16&addressdetails=1";

  if (client.connect("nominatim.openstreetmap.org", 443)) {
    client.println("GET " + url + " HTTP/1.1");
    client.println("Host: nominatim.openstreetmap.org");
    client.println("User-Agent: ESP32-GPS-Tracker/1.0");
    client.println("Connection: close");
    client.println();

    String response = "";
    bool headersEnded = false;

    while (client.connected() || client.available()) {
      if (client.available()) {
        String line = client.readStringUntil('\n');
        if (!headersEnded) {
          if (line == "\r") {
            headersEnded = true;
          }
        } else {
          response += line;
        }
      }
    }
    client.stop();

    // Parse JSON response
    int addressStart = response.indexOf("\"display_name\":\"");
    if (addressStart != -1) {
      addressStart += 16;
      int addressEnd = response.indexOf("\"", addressStart);
      if (addressEnd != -1) {
        return response.substring(addressStart, addressEnd);
      }
    }
  }

  return "Address lookup failed";
}

// ==================================================================
//                    UTILITY FUNCTIONS
// ==================================================================

String getCurrentTimestamp() {
  time_t now = time(nullptr);
  if (now < 8 * 3600 * 2) {
    return "Time not synchronized";
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char timeStr[50];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S IST", &timeinfo);

  return String(timeStr);
}

// ==================================================================
//                    GEOLINKER STATUS HANDLER
// ==================================================================

void handleGeoLinkerStatus(uint8_t status) {
  switch (status) {
    case STATUS_SENT:
      Serial.println("✓ Location data sent to GeoLinker cloud visualization");
      break;
    case STATUS_GPS_ERROR:
      Serial.println("✗ GPS module error - check connections");
      break;
    case STATUS_NETWORK_ERROR:
      Serial.println("⚠ Network error - data buffered offline for GeoLinker");
      break;
    case STATUS_BAD_REQUEST_ERROR:
      Serial.println("✗ GeoLinker server rejected request - check API key");
      break;
    case STATUS_PARSE_ERROR:
      Serial.println("✗ GPS data parsing error for GeoLinker");
      break;
    case STATUS_INTERNAL_SERVER_ERROR:
      Serial.println("✗ GeoLinker server error - will retry automatically");
      break;
    default:
      Serial.println("? Unknown GeoLinker status: " + String(status));
      break;
  }
}

// ==================================================================
//                    MAIN PROGRAM LOOP
// ==================================================================

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ WiFi disconnected - attempting reconnection...");
    connectToWiFi();
  }

  // Update GPS data
  updateGPSData();

  // Run GeoLinker loop for cloud visualization
  uint8_t geoStatus = geo.loop();
  if (geoStatus > 0) {
    handleGeoLinkerStatus(geoStatus);
  }

  // Send periodic location updates (every 5 minutes)
  if (millis() - lastLocationSend > LOCATION_SEND_INTERVAL && gpsDataValid) {
    Serial.println("⏰ Sending periodic location update...");
    sendLocationEmail();
    lastLocationSend = millis();
  }

  delay(100);
}
