#define Version "SDD V1.5"

#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include "TFT_eSPI.h"
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <EEPROM.h>

// Font library and image library
#include "weathernum.h"

#include "font/dateFont_20.h"
#include "font/timeFont_85.h"
#include "font/timeFont_42.h"
#include "img/temperature.h"
#include "img/humidity.h"

#include "img/pangzi/i0.h"
#include "img/pangzi/i1.h"
#include "img/pangzi/i2.h"
#include "img/pangzi/i3.h"
#include "img/pangzi/i4.h"
#include "img/pangzi/i5.h"
#include "img/pangzi/i6.h"
#include "img/pangzi/i7.h"
#include "img/pangzi/i8.h"
#include "img/pangzi/i9.h"

#define LCD_BL_PIN 5

#define WebSever_EN 0
WiFiManager wm;

int Anim = 0;
int AprevTime = 0;

// Parameter Settings
struct config_type {
  char stassid[32];
  char stapsw[64];
};

config_type wificonf = {{""}, {""}};
unsigned long update_weather_time = 15;
int LCD_BL_PWM = 80;

// OpenWeatherMap API configuration
String cityName = "Saigon";
String apiKey = "12aab1a35807dfc547f54288dbf91f3b";
String weatherLang = "en";

// LCD screen related settings
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite clk = TFT_eSprite(&tft);
uint16_t bgColor = 0x0000;

// Other status flags
uint8_t Wifi_en = 1;
int prevTime = 0;

// EEPROM parameter storage address bits
int BL_addr = 0;    // Brightness (1 byte)
int wifi_addr = 1;  // WiFi SSID+Password (96 bytes: 32+64)
int CN_addr = 97;   // City name (31 bytes max with null terminator)

time_t prevDisplay = 0;       // Display time display record
unsigned long weatherTime = 0; // Weather update time record
int weatherUpdateCount = 0;    // Counter for NTP sync (sync every 2 weather updates)
WeatherNum wrat;

int tempnum = 0;
int huminum = 0;
int tempcol = 0xffff;
int humicol = 0xffff;

// Web server
ESP8266WebServer server(80);

// NTP client setup
WiFiUDP ntpUDP;
WiFiClient wificlient;
NTPClient timeClient(ntpUDP, "asia.pool.ntp.org", 7 * 3600);

// TimeLib compatibility wrappers
int hour() { return timeClient.getHours(); }
int minute() { return timeClient.getMinutes(); }
int second() { return timeClient.getSeconds(); }
int day() {
  time_t rawtime = timeClient.getEpochTime();
  struct tm *ti = localtime(&rawtime);
  return ti->tm_mday;
}
int month() {
  time_t rawtime = timeClient.getEpochTime();
  struct tm *ti = localtime(&rawtime);
  return ti->tm_mon + 1;
}
int year() {
  time_t rawtime = timeClient.getEpochTime();
  struct tm *ti = localtime(&rawtime);
  return ti->tm_year + 1900;
}
int weekday() {
  return timeClient.getDay() + 1; // NTPClient uses 0=Sunday
}
time_t now() { return timeClient.getEpochTime(); }

// Clear EEPROM area (helper function)
void clearEEPROMArea(int startAddr, int length) {
  for (int i = 0; i < length; i++) {
    EEPROM.write(startAddr + i, 0xFF);
  }
  EEPROM.commit();
  delay(10);
}

// Save city name to EEPROM (for OpenWeatherMap)
void saveCityNametoEEP(String cityname) {
  clearEEPROMArea(CN_addr, 31);
  
  for (unsigned int i = 0; i < cityname.length() && i < 30; i++) {
    EEPROM.write(CN_addr + i, cityname[i]);
  }
  EEPROM.write(CN_addr + cityname.length(), '\0');
  EEPROM.commit();
  delay(10);
}

// Read city name from EEPROM
void readCityNamefromEEP() {
  char buffer[31];
  memset(buffer, 0, sizeof(buffer));
  
  for (int i = 0; i < 30; i++) {
    char c = EEPROM.read(CN_addr + i);
    if (c == '\0' || c == 0xFF) break;
    if (c >= 32 && c <= 126) {
      buffer[i] = c;
    } else {
      break;
    }
  }
  buffer[30] = '\0';
  
  String readCity = String(buffer);
  if (readCity.length() > 0 && readCity.length() < 30) {
    cityName = readCity;
  } else {
    cityName = "Saigon"; // Default city
  }
}



// wifi ssid, psw save to eeprom
void savewificonfig() {
  clearEEPROMArea(wifi_addr, 96);

  uint8_t *p = (uint8_t *)(&wificonf);
  for (size_t i = 0; i < sizeof(wificonf); i++) {
    EEPROM.write(i + wifi_addr, *(p + i)); // Simulate write in flash memory
  }
  delay(10);
  EEPROM.commit(); // Execute write to ROM
  delay(10);
}

// Read WiFi information ssid, psw from eeprom
void readwificonfig() {
  uint8_t *p = (uint8_t *)(&wificonf);
  for (size_t i = 0; i < sizeof(wificonf); i++) {
    *(p + i) = EEPROM.read(i + wifi_addr);
  }

  Serial.printf("Read WiFi Config.....\r\n");
  Serial.printf("SSID:%s\r\n", wificonf.stassid);
  Serial.printf("PSW:%s\r\n", wificonf.stapsw);
}

// TFT screen output function
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= tft.height())
    return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1; // Return 1 to decode next block
}

// Progress bar function
byte loadNum = 6;
void loading(byte delayTime) // Draw progress bar
{
  clk.setColorDepth(8);

  clk.createSprite(200, 100); // Create window
  clk.fillSprite(0x0000);     // Fill rate

  clk.drawRoundRect(0, 0, 200, 16, 8, 0xFFFF);     // Hollow rounded rectangle
  clk.fillRoundRect(3, 3, loadNum, 10, 5, 0xFFFF); // Solid rounded rectangle
  clk.setTextDatum(CC_DATUM);                      // Set text data
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("Connecting to WiFi......", 100, 40, 2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawRightString(Version, 180, 60, 2);
  clk.pushSprite(20, 120); // Window position

  clk.deleteSprite();
  loadNum += 1;
  delay(delayTime);
}

// Temperature icon display function
void tempWin() {
  clk.setColorDepth(8);
  clk.createSprite(52, 6);                         // Create window
  clk.fillSprite(0x0000);                          // Fill rate
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);       // Hollow rounded rectangle  start position x,y, length, width, arc radius, color
  clk.fillRoundRect(1, 1, tempnum, 4, 2, tempcol); // Solid rounded rectangle
  clk.pushSprite(45, 220);                         // Window position
  clk.deleteSprite();
}

// Humidity icon display function
void humidityWin() {
  clk.setColorDepth(8);
  huminum = huminum / 2;
  clk.createSprite(52, 6);                         // Create window
  clk.fillSprite(0x0000);                          // Fill rate
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);       // Hollow rounded rectangle  start position x,y, length, width, arc radius, color
  clk.fillRoundRect(1, 1, huminum, 4, 2, humicol); // Solid rounded rectangle
  clk.pushSprite(45, 250);                         // Window position
  clk.deleteSprite();
}

#if WebSever_EN
// web configuration page
void handleconfig() {
  String msg;
  int web_lcdbl, web_upt;
  String web_city, web_apikey;

  if (server.hasArg("web_city") || server.hasArg("web_bl")) {
    web_city = server.arg("web_city");
    web_lcdbl = server.arg("web_bl").toInt();
    Serial.println("");
    
    // Save city name
    if (web_city.length() > 0 && web_city.length() < 30) {
      cityName = web_city;
      saveCityNametoEEP(cityName);
      Serial.print("City Name: ");
      Serial.println(cityName);
    }
    if (web_lcdbl > 0 && web_lcdbl <= 100) {
      EEPROM.write(BL_addr, web_lcdbl); // Write brightness value to brightness address
      EEPROM.commit();                  // Save changed data
      delay(5);
      LCD_BL_PWM = EEPROM.read(BL_addr);
      delay(5);
      Serial.printf("Brightness adjusted to: ");
      analogWrite(LCD_BL_PIN, map(LCD_BL_PWM, 0, 100, 0, 255));
      Serial.println(LCD_BL_PWM);
      Serial.println("");
    }
  }

  // Web page interface code segment
  String content = "<html><style>html,body{ background: #1aceff; color: #fff; font-size: 12px;}</style>";
  content += "<body><form action='/' method='POST'><br><div style='font-size:16px;font-weight:bold;'>SDD Web Config</div><br>";
  content += "City Name:<br><input type='text' name='web_city' placeholder='Hanoi' value='" + cityName + "' style='width:200px;'><br>";
  content += "<br>Back Light(1-100):(default:50)<br><input type='text' name='web_bl' placeholder='50' style='width:100px;'><br>";
  content += "<br><div><input type='submit' name='Save' value='Save' style='padding:10px 20px;'></form></div>" + msg + "<br>";
  content += "<p style='font-size:10px;'>By WCY | Modified for OpenWeatherMap</p>";
  content += "</body></html>";
  server.send(200, "text/html", content);
}

// no need authentication
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

// Web service initialization
void Web_Sever_Init() {
  uint32_t counttime = 0; // Record mDNS creation time
  Serial.println("mDNS responder building...");
  counttime = millis();
  while (!MDNS.begin("SD3")) {
    if (millis() - counttime > 30000)
      ESP.restart(); // If over 30 seconds, restart device
  }

  Serial.println("mDNS responder started");

  server.on("/", handleconfig);
  server.onNotFound(handleNotFound);

  // Start TCP service
  server.begin();
  Serial.println("HTTP server started");

  Serial.println("Connect: http://sd3.local");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  // Add server to mDNS
  MDNS.addService("http", "tcp", 80);
}
// Web page setting function
void Web_Sever() {
  MDNS.update();
  server.handleClient();
}
// LCD displays login URL and IP after web service is opened
void Web_sever_Win() {
  // strcpy(IP_adr,WiFi.localIP().toString());
  clk.setColorDepth(8);

  clk.createSprite(200, 70); // Create window
  clk.fillSprite(0x0000);    // Fill rate

  // clk.drawRoundRect(0,0,200,100,5,0xFFFF);       //Hollow rounded rectangle
  clk.setTextDatum(CC_DATUM); // Set text data
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("Connect to Config:", 70, 10, 2);
  // clk.drawString("IP:",45,60,2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawString("http://sd3.local", 100, 40, 4);
  // clk.drawString(&IP_adr,125,70,2);
  clk.pushSprite(20, 40); // Window position

  clk.deleteSprite();
}
#endif

// WEB configuration LCD display function
void Web_win() {
  clk.setColorDepth(8);

  clk.createSprite(200, 60); // Create window
  clk.fillSprite(0x0000);    // Fill rate

  clk.setTextDatum(CC_DATUM); // Set text data
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("WiFi Connect Fail!", 100, 10, 2);
  clk.drawString("SSID:", 45, 40, 2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawString("AstronautWeather", 125, 40, 2);
  clk.pushSprite(20, 50); // Window position

  clk.deleteSprite();
}

String getParam(String name) {
  // read parameter from server, for customhmtl input
  String value;
  if (wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback() {
  Serial.println("[CALLBACK] saveParamCallback fired");

  // Save data obtained from the page
  cityName = getParam("CityName");
  LCD_BL_PWM = getParam("LCDBL").toInt();

  // Process the obtained data
  // City name
  Serial.print("City Name = ");
  Serial.println(cityName);
  if (cityName.length() > 0 && cityName.length() < 30) {
    saveCityNametoEEP(cityName);
  }
  tft.fillScreen(bgColor);
  Web_win();
  loadNum--;
  loading(1);
  if (EEPROM.read(BL_addr) != LCD_BL_PWM) {
    EEPROM.write(BL_addr, LCD_BL_PWM);
    EEPROM.commit();
    delay(5);
  }

  // Screen brightness
  Serial.printf("Brightness adjusted to: ");
  analogWrite(LCD_BL_PIN, map(LCD_BL_PWM, 0, 100, 0, 255));
  Serial.println(LCD_BL_PWM);
}

// WEB configuration function
void Webconfig() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  delay(3000);
  wm.resetSettings(); // wipe settings

  WiFiManagerParameter custom_bl("LCDBL", "LCD BackLight(1-100)", "80", 3);
  WiFiManagerParameter custom_city("CityName", "City Name", "Saigon", 30);
  WiFiManagerParameter p_lineBreak_notext("<p></p>");

  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_city);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_bl);
  wm.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  std::vector<const char *> menu = {"wifi", "restart"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");
  wm.setMinimumSignalQuality(20); // set min RSSI (percentage) to show in scans, null = 8%

  bool res;
  res = wm.autoConnect("AstronautWeather"); // anonymous ap

  while (!res);
}

String scrollText[3]; // Weather information storage
int currentIndex = 0;
TFT_eSprite clkb = TFT_eSprite(&tft);

void scrollBanner() {
  if (scrollText[currentIndex]) {
    clkb.setColorDepth(8);
    clkb.loadFont(dateFont_20);
    clkb.createSprite(150, 30);
    clkb.fillSprite(bgColor);
    clkb.setTextWrap(false);
    clkb.setTextDatum(CC_DATUM);
    clkb.setTextColor(TFT_WHITE, bgColor);
    clkb.drawString(scrollText[currentIndex], 74, 16);
    clkb.pushSprite(10, 50);

    clkb.deleteSprite();
    clkb.unloadFont();

    if (currentIndex >= 2)
      currentIndex = 0; // Back to first
    else
      currentIndex += 1; // Prepare to switch to next
  }
  prevTime = 1;
}

// Helper function to capitalize each word in a string
String capitalizeWords(String str) {
  String result = str;
  bool capitalizeNext = true;
  
  for (unsigned int i = 0; i < result.length(); i++) {
    if (result.charAt(i) == ' ') {
      capitalizeNext = true;
    } else if (capitalizeNext && result.charAt(i) >= 'a' && result.charAt(i) <= 'z') {
      result.setCharAt(i, result.charAt(i) - 32); // Convert to uppercase
      capitalizeNext = false;
    } else {
      capitalizeNext = false;
    }
  }
  
  return result;
}

// Get weather data from OpenWeatherMap
// Weather data cache
struct WeatherData {
  String cityName;
  String countryCode;
  String weatherDesc;
  String weatherStatus;
  int weatherCode;
  float temp;
  int humidity;
  float temp_feels;
  float windSpeed;
  int windDeg;
  bool hasData;
} weatherCache = {"NULL", "NULL", "NULL", "NULL", 0, 0, 0, 0, 0, 0, false};

void getWeatherData() {
  if (apiKey.length() == 0) {
    Serial.println("ERROR: API Key not set!");
    return;
  }

  // OpenWeatherMap API endpoint - Current weather
  String URL = "http://api.openweathermap.org/data/2.5/weather?q=" + cityName +
               "&appid=" + apiKey +
               "&units=metric&lang=" + weatherLang;

  // Create HTTPClient object
  HTTPClient httpClient;
  httpClient.begin(wificlient, URL);

  // Start connection and send HTTP request
  int httpCode = httpClient.GET();

  // If server responds OK, parse JSON response
  if (httpCode == HTTP_CODE_OK) {
    String payload = httpClient.getString();

    // Use DynamicJsonDocument, adjust the size (2048)
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      httpClient.end();
      return;
    }

    // Extract weather data - ONLY PARSE, DON'T DRAW YET
    JsonObject weather = doc["weather"][0];
    JsonObject main = doc["main"];
    JsonObject wind = doc["wind"];
    JsonObject sys = doc["sys"];

    weatherCache.weatherDesc = capitalizeWords(weather["description"].as<String>());
    weatherCache.weatherStatus = weather["main"].as<String>();
    if (weatherCache.weatherStatus.indexOf("Thunderstorm") != -1) {
      weatherCache.weatherStatus = "T-Storm";
    }
    weatherCache.weatherCode = weather["id"].as<int>();
    weatherCache.temp = main["temp"].as<float>();
    weatherCache.humidity = main["humidity"].as<int>();
    weatherCache.temp_feels = main["feels_like"].as<float>();
    weatherCache.cityName = doc["name"].as<String>();

    if (weatherCache.cityName.indexOf("Ho Chi Minh") != -1)
      weatherCache.cityName = "HCM";

    weatherCache.countryCode = sys["country"].as<String>();
    weatherCache.windSpeed = wind["speed"].as<float>();
    weatherCache.windDeg = wind["deg"].as<int>();
    weatherCache.hasData = true;

    Serial.println("Weather data fetched successfully");
    
    // Prepare scroll text data
    scrollText[0] = weatherCache.weatherDesc;
    scrollText[1] = "Feels like " + String((int)weatherCache.temp_feels) + "°C";

    // Wind direction
    String windDir = "N";
    if (weatherCache.windDeg >= 45 && weatherCache.windDeg < 135)
      windDir = "E";
    else if (weatherCache.windDeg >= 135 && weatherCache.windDeg < 225)
      windDir = "S";
    else if (weatherCache.windDeg >= 225 && weatherCache.windDeg < 315)
      windDir = "W";
    scrollText[2] = "Wind " + windDir + " " + String(weatherCache.windSpeed, 1) + "m/s";

  } else {
    Serial.print("Weather request failed: HTTP ");
    Serial.println(httpCode);
  }

  httpClient.end();
}

// Update weather display from cache
// Previous displayed values for change detection
struct WeatherDisplayCache {
  String cityName;
  String countryCode;
  int weatherCode;
  int temp;
  int humidity;
  String weatherDesc;
  bool initialized;
} prevWeatherDisplay = {"", "", -1, -999, -999, "", false};

void updateWeatherDisplay() {
  if (!weatherCache.hasData) return;
  
  int currentTemp = (int)weatherCache.temp;
  int currentHumidity = weatherCache.humidity;
  bool needUpdate = false;
  
  // Check if anything changed
  if (!prevWeatherDisplay.initialized ||
      prevWeatherDisplay.cityName != weatherCache.cityName ||
      prevWeatherDisplay.countryCode != weatherCache.countryCode ||
      prevWeatherDisplay.weatherCode != weatherCache.weatherCode ||
      prevWeatherDisplay.temp != currentTemp ||
      prevWeatherDisplay.humidity != currentHumidity ||
      prevWeatherDisplay.weatherDesc != weatherCache.weatherDesc) {
    needUpdate = true;
  }
  
  if (!needUpdate) return; // Nothing changed, skip drawing
  
  clk.setColorDepth(8);
  clk.loadFont(dateFont_20);
  
  // Update City name (only if changed)
  if (!prevWeatherDisplay.initialized || 
      prevWeatherDisplay.cityName != weatherCache.cityName) {
    clk.createSprite(94, 30);
    clk.fillSprite(bgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(TFT_WHITE, bgColor);
    clk.drawString(weatherCache.cityName, 44, 16);
    clk.pushSprite(15, 15);
    clk.deleteSprite();
    Serial.println("cityName updated");
  }

  // Update Country Code (only if country code changed)
  if (!prevWeatherDisplay.initialized || 
      prevWeatherDisplay.countryCode != weatherCache.countryCode) {

    clk.createSprite(56, 24);
    clk.fillSprite(bgColor);
    clk.fillRoundRect(0, 0, 50, 24, 4, 0x9E67); // Light green RGB565 (RGB: 156, 202, 127)
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(0x0000);
    clk.drawString(weatherCache.countryCode, 25, 13);
    clk.pushSprite(104, 18);
    clk.deleteSprite();
    Serial.println("Country code updated");
  }

  // Update Weather Icon (only if weather code changed)
  if (!prevWeatherDisplay.initialized ||
      prevWeatherDisplay.weatherCode != weatherCache.weatherCode) {

    // Weather icon
    int iconCode = 0;
    if (weatherCache.weatherCode >= 200 && weatherCache.weatherCode < 300)
      iconCode = 4;
    else if (weatherCache.weatherCode >= 300 && weatherCache.weatherCode < 400)
      iconCode = 7;
    else if (weatherCache.weatherCode >= 500 && weatherCache.weatherCode < 600)
      iconCode = 8;
    else if (weatherCache.weatherCode >= 600 && weatherCache.weatherCode < 700)
      iconCode = 14;
    else if (weatherCache.weatherCode >= 701 && weatherCache.weatherCode < 800)
      iconCode = 18;
    else if (weatherCache.weatherCode == 800)
      iconCode = 0;
    else if (weatherCache.weatherCode > 800)
      iconCode = 1;

    wrat.printfweather(170, 15, iconCode);

    clk.createSprite(60, 24);
    clk.fillSprite(bgColor);
    clk.fillRoundRect(0, 0, 60, 24, 4, 0xFFFF);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(0x0000);
    clk.drawString(weatherCache.weatherStatus, 30, 13);
    clk.pushSprite(170, 72);
    clk.deleteSprite();

    Serial.println("Weather icon updated");
  }

  // Update Temperature (only if changed)
  if (!prevWeatherDisplay.initialized || 
      prevWeatherDisplay.temp != currentTemp) {
    
    clk.createSprite(58, 24);
    clk.fillSprite(bgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(TFT_WHITE, bgColor);
    clk.drawString(String(currentTemp) + "°C", 28, 13);
    clk.pushSprite(100, 210);
    clk.deleteSprite();

    tempnum = currentTemp + 10;
    if (tempnum < 10)
      tempcol = 0x00FF;
    else if (tempnum < 28)
      tempcol = 0x0AFF;
    else if (tempnum < 34)
      tempcol = 0x0F0F;
    else if (tempnum < 41)
      tempcol = 0xFF0F;
    else if (tempnum < 49)
      tempcol = 0xF00F;
    else {
      tempcol = 0xF00F;
      tempnum = 50;
    }
    tempWin();
    Serial.println("Temperature updated");
  }

  // Update Humidity (only if changed)
  if (!prevWeatherDisplay.initialized || 
      prevWeatherDisplay.humidity != currentHumidity) {
    
    clk.createSprite(58, 24);
    clk.fillSprite(bgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(TFT_WHITE, bgColor);
    clk.drawString(String(currentHumidity) + "%", 28, 13);
    clk.pushSprite(100, 240);
    clk.deleteSprite();

    huminum = currentHumidity;
    if (huminum > 90)
      humicol = 0x00FF;
    else if (huminum > 70)
      humicol = 0x0AFF;
    else if (huminum > 40)
      humicol = 0x0F0F;
    else if (huminum > 20)
      humicol = 0xFF0F;
    else
      humicol = 0xF00F;
    humidityWin();
    Serial.println("Humidity updated");
  }

  clk.unloadFont();
  
  // Update cache
  prevWeatherDisplay.cityName = weatherCache.cityName;
  prevWeatherDisplay.countryCode = weatherCache.countryCode;
  prevWeatherDisplay.weatherCode = weatherCache.weatherCode;
  prevWeatherDisplay.temp = currentTemp;
  prevWeatherDisplay.humidity = currentHumidity;
  prevWeatherDisplay.weatherDesc = weatherCache.weatherDesc;
  prevWeatherDisplay.initialized = true;  
}

// Week
String week() {
  String wk[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  String s = wk[weekday() - 1];
  return s;
}

// Day/Month/Year with zero padding (DD/MM/YYYY)
String monthDay() {
  String d = (day() < 10) ? "0" + String(day()) : String(day());
  String m = (month() < 10) ? "0" + String(month()) : String(month());
  String s = d + "/" + m + "/" + year();
  return s;
}

// Digital clock display function
// Cache for date display
unsigned char Hour_sign = 60;
unsigned char Minute_sign = 60;
unsigned char Second_sign = 60;
static String prev_week = "";
static String prev_monthDay = "";
int timey = 93;

void digitalClockDisplay(int reflash_en) {
  if (hour() != Hour_sign || reflash_en == 1) {
    clk.setColorDepth(8);
    clk.loadFont(timeFont_85);
    
    clk.createSprite(80, 70);
    clk.fillSprite(bgColor);
    clk.setTextDatum(TC_DATUM);
    clk.setTextColor(TFT_RED, bgColor);
    
    String hourStr = (hour() < 10) ? "0" + String(hour()) : String(hour());
    clk.drawString(hourStr, 40, 0);
    clk.pushSprite(10, timey);
    clk.deleteSprite();
    clk.unloadFont();
    
    Hour_sign = hour();
    Serial.println("Hour updated");
  }
  
  if (minute() != Minute_sign || reflash_en == 1) {
    clk.setColorDepth(8);
    clk.loadFont(timeFont_85);
    
    clk.createSprite(80, 70);
    clk.fillSprite(bgColor);
    clk.setTextDatum(TC_DATUM);
    clk.setTextColor(TFT_ORANGE, bgColor);
    
    String minuteStr = (minute() < 10) ? "0" + String(minute()) : String(minute());
    clk.drawString(minuteStr, 40, 0);
    clk.pushSprite(90, timey);
    clk.deleteSprite();
    clk.unloadFont();
    
    Minute_sign = minute();
    
    // Display free RAM every minute
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.print("Minute updated | Free RAM: ");
    Serial.print(freeHeap);
    Serial.println(" bytes");
  }
  
  if (second() != Second_sign || reflash_en == 1) {
    clk.setColorDepth(8);
    clk.loadFont(timeFont_42);

    clk.createSprite(44, 40);
    clk.fillSprite(bgColor);
    clk.setTextDatum(TC_DATUM);
    clk.setTextColor(TFT_SKYBLUE, bgColor);

    String secondStr = (second() < 10) ? "0" + String(second()) : String(second());
    clk.drawString(secondStr, 22, 0);
    clk.pushSprite(181, timey+30);
    clk.deleteSprite();
    clk.unloadFont();

    Second_sign = second();
  }

  /***Date - Only update when changed***/ 
  String current_week = week();
  String current_monthDay = monthDay();
  
  // Check if date changed or forced refresh
  if (current_week != prev_week || reflash_en == 1) {
    clk.setColorDepth(8);
    clk.loadFont(dateFont_20);
    
    // Day of week
    clk.createSprite(64, 30);
    clk.fillSprite(bgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(TFT_VIOLET, bgColor);
    clk.drawString(current_week, 32, 16);
    clk.pushSprite(132, 170);
    clk.deleteSprite();
    
    clk.unloadFont();
    prev_week = current_week;
    Serial.println("Week updated");
  }
  
  // Update day/month/year only when changed
  if (current_monthDay != prev_monthDay || reflash_en == 1) {
    clk.setColorDepth(8);
    clk.loadFont(dateFont_20);
    
    // Day/Month/Year - Increased width to 130 for full date display
    clk.createSprite(130, 30);
    clk.fillSprite(bgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(TFT_WHITE, bgColor);
    clk.drawString(current_monthDay, 65, 16);
    clk.pushSprite(15, 170);
    clk.deleteSprite();
    
    clk.unloadFont();
    prev_monthDay = current_monthDay;
    Serial.println("Date updated");
  }
  /***Date****/
}

void imgAnim() {
  int x = 160, y = 200;
  if (millis() - AprevTime > 37) // Switch every x ms
  {
    Anim++;
    AprevTime = millis();
  }
  if (Anim == 10)
    Anim = 0;

  switch (Anim) {
  case 0:
    TJpgDec.drawJpg(x, y, i0, sizeof(i0));
    break;
  case 1:
    TJpgDec.drawJpg(x, y, i1, sizeof(i1));
    break;
  case 2:
    TJpgDec.drawJpg(x, y, i2, sizeof(i2));
    break;
  case 3:
    TJpgDec.drawJpg(x, y, i3, sizeof(i3));
    break;
  case 4:
    TJpgDec.drawJpg(x, y, i4, sizeof(i4));
    break;
  case 5:
    TJpgDec.drawJpg(x, y, i5, sizeof(i5));
    break;
  case 6:
    TJpgDec.drawJpg(x, y, i6, sizeof(i6));
    break;
  case 7:
    TJpgDec.drawJpg(x, y, i7, sizeof(i7));
    break;
  case 8:
    TJpgDec.drawJpg(x, y, i8, sizeof(i8));
    break;
  case 9:
    TJpgDec.drawJpg(x, y, i9, sizeof(i9));
    break;
  default:
    Serial.println("Display Anim error");
    break;
  }
}

void LCD_reflash(int en) {
  updateWeatherDisplay();

  if (now() != prevDisplay || en == 1) {
    prevDisplay = now();
    digitalClockDisplay(en);
    prevTime = 0;
  }

  // Update every two seconds
  if ((second() % 2 == 0 && prevTime == 0) || en == 1) {
    scrollBanner();
  }

  imgAnim();
}

void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.println("Astronaut Weather Station");

  EEPROM.begin(1024);
  pinMode(LCD_BL_PIN, OUTPUT);

  // Read backlight brightness setting from eeprom
  if (EEPROM.read(BL_addr) > 0 && EEPROM.read(BL_addr) <= 100)
    LCD_BL_PWM = EEPROM.read(BL_addr);

  tft.begin();
  analogWrite(LCD_BL_PIN, map(LCD_BL_PWM, 0, 100, 0, 255));
  tft.fillScreen(bgColor);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  readwificonfig();
  Serial.print("Connecting to ");
  Serial.println(wificonf.stassid);
  WiFi.begin(wificonf.stassid, wificonf.stapsw);

  while (WiFi.status() != WL_CONNECTED) {
    loading(30);
    if (loadNum >= 194) {
      Web_win();
      Webconfig();
      break;
    }
  }
  delay(10);
  while (loadNum < 194) loading(1); // Let animation complete

  if (WiFi.status() == WL_CONNECTED) {
    strcpy(wificonf.stassid, WiFi.SSID().c_str()); // Name copy
    strcpy(wificonf.stapsw, WiFi.psk().c_str());   // Password copy
    savewificonfig();
#if WebSever_EN
    Web_Sever_Init();
    Web_sever_Win(); // Start webserver initialization
    delay(1000);
#endif
  }

  Serial.println("Initializing NTP client...");
  timeClient.begin();
  
  // Try to sync time
  bool ntpSuccess = false;
  for (int i = 0; i < 10; ++i) {
    if (timeClient.update()) {
      ntpSuccess = true;
      Serial.println("NTP sync successful");
      break;
    }
    delay(1000);
  }
  
  if (!ntpSuccess) {
    Serial.println("NTP sync failed, will retry later");
  }

  // Read saved city name from EEPROM
  readCityNamefromEEP();

  tft.fillScreen(TFT_BLACK);
  Serial.println("Getting weather data...");
  getWeatherData();
  // updateWeatherDisplay();

#if !WebSever_EN
  WiFi.forceSleepBegin(); // wifi off
  Serial.println("WIFI sleeping......");
  Wifi_en = 0;
#endif

  TJpgDec.drawJpg(15, 209, temperature, sizeof(temperature)); // Temperature icon
  TJpgDec.drawJpg(15, 239, humidity, sizeof(humidity));       // Humidity icon

  weatherTime = millis();
}

void loop() {
#if WebSever_EN
  Web_Sever();
#endif
  LCD_reflash(0);

  if (millis() - weatherTime > (60000UL * update_weather_time)) {
    if (Wifi_en == 0) {
      WiFi.forceSleepWake(); // wifi on
      Serial.println("WIFI recovering......");
      Wifi_en = 1;
    }

    if (WiFi.status() == WL_CONNECTED) {
      getWeatherData();
      weatherUpdateCount++;

      // Sync NTP only every 3 weather updates
      if (weatherUpdateCount >= 3) {
        if (timeClient.update()) {
          Serial.println("NTP sync successful");
        } else {
          Serial.println("NTP sync failed");
        }
        weatherUpdateCount = 0;
      }

#if !WebSever_EN
      WiFi.forceSleepBegin(); // Wifi Off
      Serial.println("WIFI sleeping......");
      Wifi_en = 0;
#endif
      weatherTime = millis();
    }
  }
}
