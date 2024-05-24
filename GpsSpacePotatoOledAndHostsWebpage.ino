#include <WiFi.h>
#include <WebServer.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <math.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int EEPROM_SIZE = 512;
const int EEPROM_LATITUDE_ADDR = 0;
const int EEPROM_LONGITUDE_ADDR = 8;

double homeLatitude;
double homeLongitude;

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

HardwareSerial ss(2);
TinyGPSPlus gps;

WebServer server(80);

double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  double R = 6371000;
  double phi1 = lat1 * (M_PI / 180);
  double phi2 = lat2 * (M_PI / 180);
  double deltaPhi = (lat2 - lat1) * (M_PI / 180);
  double deltaLambda = (lon2 - lon1) * (M_PI / 180);

  double a = sin(deltaPhi / 2) * sin(deltaPhi / 2) +
             cos(phi1) * cos(phi2) *
             sin(deltaLambda / 2) * sin(deltaLambda / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));

  return R * c;
}

double calculateBearing(double lat1, double lon1, double lat2, double lon2) {
  double phi1 = lat1 * (M_PI / 180);
  double phi2 = lat2 * (M_PI / 180);
  double deltaLambda = (lon2 - lon1) * (M_PI / 180);

  double y = sin(deltaLambda) * cos(phi2);
  double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(deltaLambda);
  double theta = atan2(y, x);

  double bearing = fmod((theta * (180 / M_PI) + 360), 360); // Convert from radians to degrees
  return bearing;
}

String getDirection(double bearing) {
  if ((bearing >= 0 && bearing < 22.5) || (bearing >= 337.5 && bearing < 360)) {
    return "North";
  } else if (bearing >= 22.5 && bearing < 67.5) {
    return "North East";
  } else if (bearing >= 67.5 && bearing < 112.5) {
    return "East";
  } else if (bearing >= 112.5 && bearing < 157.5) {
    return "South East";
  } else if (bearing >= 157.5 && bearing < 202.5) {
    return "South";
  } else if (bearing >= 202.5 && bearing < 247.5) {
    return "South West";
  } else if (bearing >= 247.5 && bearing < 292.5) {
    return "West";
  } else if (bearing >= 292.5 && bearing < 337.5) {
    return "North West";
  } else {
    return "Unknown";
  }
}

void saveDoubleToEEPROM(int address, double value) {
  byte *p = (byte *)(void *)&value;
  for (int i = 0; i < sizeof(value); i++)
    EEPROM.write(address++, *p++);
  EEPROM.commit();
}

double readDoubleFromEEPROM(int address) {
  double value = 0.0;
  byte *p = (byte *)(void *)&value;
  for (int i = 0; i < sizeof(value); i++)
    *p++ = EEPROM.read(address++);
  return value;
}

void handleUpdateHome() {
  String page = "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  page += "<title>Update Home Coordinates</title>";
  page += "<style>body {font-family: Arial, sans-serif;text-align: center;font-size: 24px;}</style>";
  page += "</head><body>";
  page += "<h1>Update Home Coordinates</h1>";
  page += "<form action=\"/saveHome\" method=\"POST\">";
  page += "Latitude: <input type=\"text\" name=\"latitude\"><br>";
  page += "Longitude: <input type=\"text\" name=\"longitude\"><br>";
  page += "<input type=\"submit\" value=\"Save\">";
  page += "</form>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleSaveHome() {
  if (server.hasArg("latitude") && server.hasArg("longitude")) {
    String latStr = server.arg("latitude");
    String lonStr = server.arg("longitude");
    homeLatitude = latStr.toDouble();
    homeLongitude = lonStr.toDouble();
    saveDoubleToEEPROM(EEPROM_LATITUDE_ADDR, homeLatitude);
    saveDoubleToEEPROM(EEPROM_LONGITUDE_ADDR, homeLongitude);
    server.send(200, "text/html", "<html><body><h1>Home coordinates saved!</h1></body></html>");
  } else {
    server.send(400, "text/html", "<html><body><h1>Invalid input!</h1></body></html>");
  }
}

void handleSetCurrentAsHome() {
  if (gps.location.isValid()) {
    homeLatitude = gps.location.lat();
    homeLongitude = gps.location.lng();
    saveDoubleToEEPROM(EEPROM_LATITUDE_ADDR, homeLatitude);
    saveDoubleToEEPROM(EEPROM_LONGITUDE_ADDR, homeLongitude);
    server.send(200, "text/html", "<html><body><h1>Current location set as home!</h1></body></html>");
  } else {
    server.send(400, "text/html", "<html><body><h1>GPS location is not valid!</h1></body></html>");
  }
}

void handleDisableWiFi() {
  server.send(200, "text/html", "<html><body><h1>WiFi will be disabled</h1></body></html>");
  WiFi.softAPdisconnect(true);
}

String getGPSData() {
  DynamicJsonDocument doc(200);

  JsonObject root = doc.to<JsonObject>();
  JsonObject gpsData = root.createNestedObject("gps");
  gpsData["latitude"] = gps.location.lat();
  gpsData["longitude"] = gps.location.lng();
  gpsData["satellites"] = gps.satellites.value();

  double distanceToHome = calculateDistance(gps.location.lat(), gps.location.lng(), homeLatitude, homeLongitude);
  gpsData["distanceToHome"] = distanceToHome;

  double bearing = calculateBearing(gps.location.lat(), gps.location.lng(), homeLatitude, homeLongitude);
  String direction = getDirection(bearing);
  gpsData["direction"] = direction;

  String output;
  serializeJson(root, output);
  return output;
}

void handleRoot() {
  String page = "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  page += "<title>GPS Data</title>";
  page += "<style>body {font-family: Arial, sans-serif;text-align: center;font-size: 24px;}</style>";
  page += "</head><body>";
  page += "<h1>Help me return home</h1>";
  page += "Hi there! I'm a lost space potato; please use the distance readout to carry me back home. Thank you, kind soul!</p>";
  page += "<div id=\"gpsData\"></div>";

  // Add button to set current location as home
  page += "<button onclick=\"setCurrentAsHome()\">Set Current Location as Home</button>";

  // Add button to disable WiFi
  page += "<button onclick=\"disableWiFi()\">Disable WiFi</button>";

  // JavaScript to set current location as home
  page += "<script>";
  page += "function setCurrentAsHome() {";
  page += "fetch('/setCurrentAsHome').then(response => {";
  page += "if (response.ok) {";
  page += "console.log('Current location set as home successfully');";
  page += "} else {";
  page += "console.error('Error setting current location as home');";
  page += "}";
  page += "});";
  page += "}";
  page += "</script>";

  // JavaScript to disable WiFi
  page += "<script>";
  page += "function disableWiFi() {";
  page += "fetch('/disableWiFi').then(response => {";
  page += "if (response.ok) {";
  page += "console.log('WiFi disabled successfully');";
  page += "} else {";
  page += "console.error('Error disabling WiFi');";
  page += "}";
  page += "});";
  page += "}";
  page += "</script>";

  // JavaScript to fetch and display GPS data
  page += "<script>";
  page += "function updateGPSData() {";
  page += "fetch('/gps').then(response => response.json()).then(data => {";
  page += "const gpsDataDiv = document.getElementById('gpsData');";
  page += "gpsDataDiv.innerHTML = `Latitude: ${data.gps.latitude.toFixed(6)}<br>Longitude: ${data.gps.longitude.toFixed(6)}<br>Satellites: ${data.gps.satellites}<br>Distance to Home: ${data.gps.distanceToHome.toFixed(2)} m<br>Direction: ${data.gps.direction}`;";
  page += "}).catch(error => console.error('Error fetching GPS data:', error));";
  page += "}";
  page += "setInterval(updateGPSData, 1000);"; // Update every second
  page += "</script>";

  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleGPS() {
  String jsonData = getGPSData();
  server.send(200, "application/json", jsonData);
}

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  ss.begin(9600, SERIAL_8N1, 16, 17); // RX, TX

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Read home coordinates from EEPROM
  homeLatitude = readDoubleFromEEPROM(EEPROM_LATITUDE_ADDR);
  homeLongitude = readDoubleFromEEPROM(EEPROM_LONGITUDE_ADDR);

  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Obtaining GPS.. Setup via wifi on 192.168.4.1"));
  display.println(F("Setup via wifi on"));
  display.println(F("192.168.4.1"));
  display.display();
  delay(2000);

  // Setup Wi-Fi access point
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP("Space Potato");

  // Initialize web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/gps", HTTP_GET, handleGPS);
  server.on("/updateHome", HTTP_GET, handleUpdateHome);
  server.on("/saveHome", HTTP_POST, handleSaveHome);
  server.on("/setCurrentAsHome", HTTP_GET, handleSetCurrentAsHome);
  server.on("/disableWiFi", HTTP_GET, handleDisableWiFi);  // Handle disable WiFi request
  server.begin();
}

void loop() {
  // Handle web server
  server.handleClient();

  // Read GPS data
  while (ss.available() > 0) {
    gps.encode(ss.read());
  }

  // Update OLED display with distance and direction
  if (gps.location.isUpdated()) {
    double distanceToHome = calculateDistance(gps.location.lat(), gps.location.lng(), homeLatitude, homeLongitude);
    double bearing = calculateBearing(gps.location.lat(), gps.location.lng(), homeLatitude, homeLongitude);
    String direction = getDirection(bearing);

    Serial.print("Latitude: ");
    Serial.println(gps.location.lat(), 6);
    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 6);
    Serial.print("Satellites: ");
    Serial.println(gps.satellites.value());
    Serial.print("Distance to Home: ");
    Serial.println(distanceToHome);
    Serial.print("Direction: ");
    Serial.println(direction);

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print((int)distanceToHome);
    display.print(" m");
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("To the ");
    display.print(direction);
    display.display();
  }
}
