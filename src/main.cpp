#define Version "SDD V1.4"

#include "ArduinoJson.h"
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <EEPROM.h>
#include "number.h"
#include "weathernum.h"

// Font library and image library
#include "font/ZdyLwFont_20.h"
#include "img/misaka.h"
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

#define LCD_BL_PIN 5 // LCD backlight pin

#define WebSever_EN 1 // Web server enable flag - WiFi sleep function will be disabled when enabled
WiFiManager wm; // global wm instance

int Anim = 0;      // Astronaut icon display pointer record
int AprevTime = 0; // Astronaut update time record

// Parameter Settings
struct config_type {
  char stassid[32]; // Define WIFI name length obtained from configuration (max 32 bytes)
  char stapsw[64];  // Define WIFI password length obtained from configuration (max 64 bytes)
};

// If WEB configuration is enabled, no need to set parameters here. First one is wifi ssid, second is password
config_type wificonf = {{""}, {""}};
unsigned long updateweater_time = 10; // Weather update time X minutes
int LCD_BL_PWM = 80;                  // Screen brightness 0-100, default 80

// OpenWeatherMap API configuration
String cityName = "Saigon";           // City name for OpenWeatherMap (e.g., "Hanoi", "Saigon", "Da Nang")
String apiKey = "12aab1a35807dfc547f54288dbf91f3b"; // OpenWeatherMap API key - Get free key at: https://openweathermap.org/api
String weatherLang = "en";            // Language: vi (Vietnamese), en (English), zh_cn (Chinese)

// LCD screen related settings
TFT_eSPI tft = TFT_eSPI(); // Please configure pins in User_Setup.h file of tft_espi library
TFT_eSprite clk = TFT_eSprite(&tft);
uint16_t bgColor = 0x0000;

// Other status flags
uint8_t Wifi_en = 1;         // wifi status flag  1: On    0: Off
uint8_t UpdateWeater_en = 0; // Update time flag
int prevTime = 0;            // Scrolling display update flag

// EEPROM parameter storage address bits (optimized layout)
int BL_addr = 0;    // Brightness (1 byte)
int UpWeT_addr = 1; // Weather update time (1 byte)
int wifi_addr = 2;  // WiFi SSID+Password (96 bytes: 32+64)
int CN_addr = 98;   // City name (31 bytes max with null terminator)
int API_addr = 129; // API key (41 bytes max with null terminator)

time_t prevDisplay = 0;       // Display time display record
unsigned long weaterTime = 0; // Weather update time record

/*** Component objects ***/
Number dig;
WeatherNum wrat;
uint32_t targetTime = 0;

int tempnum = 0;      // Temperature percentage
int huminum = 0;      // Humidity percentage
int tempcol = 0xffff; // Temperature display color
int humicol = 0xffff; // Humidity display color

// Web server
ESP8266WebServer server(80); // Create esp8266 web server object

// NTP server parameters
static const char ntpServerName[] = "asia.pool.ntp.org";
const int timeZone = 7; // GMT+7 timezone

// WiFi connection UDP setting parameters
WiFiUDP Udp;
WiFiClient wificlient;
unsigned int localPort = 8000;

// Function declarations
String week();
String monthDay();

time_t getNtpTime();
void digitalClockDisplay(int reflash_en);
void sendNTPpacket(IPAddress &address);
void LCD_reflash(int en);
void savewificonfig();
void readwificonfig();
void deletewificonfig();
void scrollBanner();
void imgAnim();
#if WebSever_EN
void Web_Sever_Init();
void Web_Sever();
#endif
void saveParamCallback();
void saveCityNametoEEP(String cityname);
void readCityNamefromEEP();
void saveApiKeytoEEP(String key);
void readApiKeyfromEEP();
void getWeatherDataFromOpenWeatherMap();

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
  }
}

// Save API key to EEPROM
void saveApiKeytoEEP(String key) {
  clearEEPROMArea(API_addr, 41);
  
  for (unsigned int i = 0; i < key.length() && i < 40; i++) {
    EEPROM.write(API_addr + i, key[i]);
  }
  EEPROM.write(API_addr + key.length(), '\0');
  EEPROM.commit();
  delay(10);
}

// Read API key from EEPROM
void readApiKeyfromEEP() {
  char buffer[41];
  memset(buffer, 0, sizeof(buffer));
  
  for (int i = 0; i < 40; i++) {
    char c = EEPROM.read(API_addr + i);
    if (c == '\0' || c == 0xFF) break;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      buffer[i] = c;
    } else {
      break;
    }
  }
  buffer[40] = '\0';
  
  String readKey = String(buffer);
  if (readKey.length() >= 32 && readKey.length() <= 40) {
    apiKey = readKey;
  }
}

// wifi ssid, psw save to eeprom
void savewificonfig() {
  // Start writing
  uint8_t *p = (uint8_t *)(&wificonf);
  for (size_t i = 0; i < sizeof(wificonf); i++) {
    EEPROM.write(i + wifi_addr, *(p + i)); // Simulate write in flash memory
  }
  delay(10);
  EEPROM.commit(); // Execute write to ROM
  delay(10);
}

// Delete existing information in eeprom
void deletewificonfig() {
  config_type deletewifi = {{""}, {""}};
  uint8_t *p = (uint8_t *)(&deletewifi);
  for (size_t i = 0; i < sizeof(deletewifi); i++) {
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
  Serial.printf("Connecting.....\r\n");
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

// Humidity icon display function
void humidityWin() {
  clk.setColorDepth(8);
  huminum = huminum / 2;
  clk.createSprite(52, 6);                         // Create window
  clk.fillSprite(0x0000);                          // Fill rate
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);       // Hollow rounded rectangle  start position x,y, length, width, arc radius, color
  clk.fillRoundRect(1, 1, huminum, 4, 2, humicol); // Solid rounded rectangle
  clk.pushSprite(45, 242);                         // Window position
  clk.deleteSprite();
}

// Temperature icon display function
void tempWin() {
  clk.setColorDepth(8);
  clk.createSprite(52, 6);                         // Create window
  clk.fillSprite(0x0000);                          // Fill rate
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);       // Hollow rounded rectangle  start position x,y, length, width, arc radius, color
  clk.fillRoundRect(1, 1, tempnum, 4, 2, tempcol); // Solid rounded rectangle
  clk.pushSprite(45, 212);                         // Window position
  clk.deleteSprite();
}

#if WebSever_EN
// web configuration page
void handleconfig() {
  String msg;
  int web_lcdbl, web_upt;
  String web_city, web_apikey;

  if (server.hasArg("web_city") || server.hasArg("web_apikey") || 
      server.hasArg("web_bl") || server.hasArg("web_upwe_t")) {
    web_city = server.arg("web_city");
    web_apikey = server.arg("web_apikey");
    web_lcdbl = server.arg("web_bl").toInt();
    web_upt = server.arg("web_upwe_t").toInt();
    Serial.println("");
    
    // Save city name
    if (web_city.length() > 0 && web_city.length() < 30) {
      cityName = web_city;
      saveCityNametoEEP(cityName);
      Serial.print("City Name: ");
      Serial.println(cityName);
    }
    
    // Save API key
    if (web_apikey.length() > 0 && web_apikey.length() < 40) {
      apiKey = web_apikey;
      saveApiKeytoEEP(apiKey);
      Serial.print("API Key saved (length): ");
      Serial.println(apiKey.length());
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
    if (web_upt > 0 && web_upt <= 60) {
      EEPROM.write(UpWeT_addr, web_upt); // Write weather update time value to update time address
      EEPROM.commit();                   // Save changed data
      delay(5);
      updateweater_time = web_upt;
      Serial.print("Weather update time (minutes):");
      Serial.println(web_upt);
    }
  }

  // Web page interface code segment
  String content = "<html><style>html,body{ background: #1aceff; color: #fff; font-size: 12px;}</style>";
  content += "<body><form action='/' method='POST'><br><div style='font-size:16px;font-weight:bold;'>SDD Web Config (OpenWeatherMap)</div><br>";
  content += "City Name:<br><input type='text' name='web_city' placeholder='Hanoi' value='" + cityName + "' style='width:200px;'><br>";
  content += "<br>API Key:<br><input type='text' name='web_apikey' placeholder='your_api_key_here' value='" + apiKey + "' style='width:200px;'><br>";
  content += "<br>Back Light(1-100):(default:50)<br><input type='text' name='web_bl' placeholder='50' style='width:100px;'><br>";
  content += "<br>Weather Update Time(min):(default:10)<br><input type='text' name='web_upwe_t' placeholder='10' style='width:100px;'><br>";
  content += "<br><div><input type='submit' name='Save' value='Save' style='padding:10px 20px;'></form></div>" + msg + "<br>";
  content += "<p style='font-size:10px;'>Get free API key at: <a href='https://openweathermap.org/api' target='_blank' style='color:#fff;'>openweathermap.org/api</a></p>";
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

// WEB configuration function
void Webconfig() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  delay(3000);
  wm.resetSettings(); // wipe settings

  WiFiManagerParameter custom_bl("LCDBL", "LCD BackLight(1-100)", "80", 3);
  WiFiManagerParameter custom_weatertime("WeaterUpdateTime", "Weather Update Time(Min)", "10", 3);
  WiFiManagerParameter custom_city("CityName", "City Name", "Saigon", 30);
  WiFiManagerParameter custom_apikey("APIKey", "OpenWeatherMap API Key", "12aab1a35807dfc547f54288dbf91f3b", 40);
  WiFiManagerParameter p_lineBreak_notext("<p></p>");
  WiFiManagerParameter p_hint("<p style='font-size:11px;'>Get free API key: openweathermap.org/api</p>");

  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_city);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_apikey);
  wm.addParameter(&p_hint);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_bl);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_weatertime);
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
  updateweater_time = getParam("WeaterUpdateTime").toInt();
  cityName = getParam("CityName");
  apiKey = getParam("APIKey");
  LCD_BL_PWM = getParam("LCDBL").toInt();

  // Process the obtained data
  // City name
  Serial.print("City Name = ");
  Serial.println(cityName);
  if (cityName.length() > 0 && cityName.length() < 30) {
    saveCityNametoEEP(cityName);
  }
  
  // API Key
  Serial.print("API Key length = ");
  Serial.println(apiKey.length());
  if (apiKey.length() > 0 && apiKey.length() < 40) {
    saveApiKeytoEEP(apiKey);
  }
  tft.setRotation(0);
  tft.fillScreen(0x0000);
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

  // Weather update time
  Serial.printf("Weather update time adjusted to: ");
  Serial.println(updateweater_time);
}

void LCD_reflash(int en) {
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

  if (millis() - weaterTime > (60000UL * updateweater_time) || en == 1 || UpdateWeater_en != 0) { // Update weather every 10 minutes
    if (Wifi_en == 0) {
      WiFi.forceSleepWake(); // wifi on
      Serial.println("WIFI recovering......");
      Wifi_en = 1;
    }

    if (WiFi.status() == WL_CONNECTED) {
      getWeatherDataFromOpenWeatherMap();
      if (UpdateWeater_en != 0)
        UpdateWeater_en = 0;
      weaterTime = millis();
      getNtpTime();
#if !WebSever_EN
      WiFi.forceSleepBegin(); // Wifi Off
      Serial.println("WIFI sleeping......");
      Wifi_en = 0;
#endif
    }
  }
}

// Get weather data from OpenWeatherMap API
String scrollText[5]; // Weather information storage
void getWeatherDataFromOpenWeatherMap() {
  if (apiKey.length() == 0) {
    Serial.println("ERROR: API Key not set!");
    return;
  }
  
  if (cityName.length() == 0) {
    cityName = "Saigon"; // Default city name
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
    
  // Use DynamicJsonDocument with a suitable capacity for parsing the OpenWeatherMap response.
  // Adjust the size (2048) if you encounter heap/memory issues or if the JSON payload is larger.
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      httpClient.end();
      return;
    }

    // Extract weather data
    JsonObject weather = doc["weather"][0];
    JsonObject main = doc["main"];
    JsonObject wind = doc["wind"];
    
    String weatherDesc = weather["main"].as<String>();
    int weatherCode = weather["id"].as<int>();
    float temp = main["temp"].as<float>();
    int humidity = main["humidity"].as<int>();
    float temp_min = main["temp_min"].as<float>();
    float temp_max = main["temp_max"].as<float>();
    String cityNameResp = doc["name"].as<String>();
    
    if (cityNameResp.indexOf("Ho Chi Minh") != -1)
      cityNameResp = "HCM";
    
    float windSpeed = wind["speed"].as<float>();
    int windDeg = wind["deg"].as<int>();

    /***Draw weather information on screen***/
    clk.setColorDepth(8);
    clk.loadFont(ZdyLwFont_20);

    // Temperature
    clk.createSprite(58, 24);
    clk.fillSprite(bgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(TFT_WHITE, bgColor);
    clk.drawString(String((int)temp) + "°C", 28, 13);
    clk.pushSprite(100, 204);
    clk.deleteSprite();
    
    tempnum = (int)temp + 10;
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

    // Humidity
    clk.createSprite(58, 24);
    clk.fillSprite(bgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(TFT_WHITE, bgColor);
    clk.drawString(String(humidity) + "%", 28, 13);
    clk.pushSprite(100, 234);
    clk.deleteSprite();
    
    huminum = humidity;
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

    // City name
    clk.createSprite(94, 30);
    clk.fillSprite(bgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(TFT_WHITE, bgColor);
    clk.drawString(cityNameResp, 44, 16);
    clk.pushSprite(15, 15);
    clk.deleteSprite();

    // Air Quality Index (simplified based on weather conditions)
    uint16_t pm25BgColor = tft.color565(156, 202, 127); // Good
    String aqiTxt = "G";
    
    // Simplified AQI based on weather conditions
    if (weatherCode >= 200 && weatherCode < 300) { // Thunderstorm
      pm25BgColor = tft.color565(242, 159, 57);
      aqiTxt = "M";
    } else if (weatherCode >= 300 && weatherCode < 600) { // Rain
      pm25BgColor = tft.color565(156, 202, 127);
      aqiTxt = "G";
    } else if (weatherCode >= 700 && weatherCode < 800) { // Atmosphere (fog, mist, etc)
      pm25BgColor = tft.color565(247, 219, 100);
      aqiTxt = "F";
    } else if (weatherCode == 800) { // Clear
      pm25BgColor = tft.color565(156, 202, 127);
      aqiTxt = "E";
    }
    
    clk.createSprite(56, 24);
    clk.fillSprite(bgColor);
    clk.fillRoundRect(0, 0, 50, 24, 4, pm25BgColor);
    clk.setTextDatum(CC_DATUM);
    clk.setTextColor(0x0000);
    clk.drawString(aqiTxt, 25, 13);
    clk.pushSprite(104, 18);
    clk.deleteSprite();

    // Scrolling text information
    scrollText[0] = "Now " + weatherDesc;
    scrollText[1] = "Humidity " + String(humidity) + "%";
    
    // Wind direction
    String windDir = "N";
    if (windDeg >= 45 && windDeg < 135) windDir = "E";
    else if (windDeg >= 135 && windDeg < 225) windDir = "S";
    else if (windDeg >= 225 && windDeg < 315) windDir = "W";
    scrollText[2] = "Wind " + windDir + " " + String(windSpeed, 1) + "m/s";
    
    // scrollText[3] = "Now " + weatherDesc;
    scrollText[3] = "Min " + String((int)temp_min) + "°C";
    scrollText[4] = "Max " + String((int)temp_max) + "°C";

    // Weather icon (map OpenWeatherMap codes to existing icon codes)
    int iconCode = 0;
    if (weatherCode >= 200 && weatherCode < 300) iconCode = 4; // Thunderstorm
    else if (weatherCode >= 300 && weatherCode < 400) iconCode = 7; // Drizzle
    else if (weatherCode >= 500 && weatherCode < 600) iconCode = 8; // Rain
    else if (weatherCode >= 600 && weatherCode < 700) iconCode = 14; // Snow
    else if (weatherCode >= 701 && weatherCode < 800) iconCode = 18; // Fog/Mist
    else if (weatherCode == 800) iconCode = 0; // Clear
    else if (weatherCode > 800) iconCode = 1; // Clouds
    
    wrat.printfweather(170, 15, iconCode);

    clk.unloadFont();
    
  } else {
    Serial.print("Weather request failed: HTTP ");
    Serial.println(httpCode);
    
    if (httpCode == 401) {
      Serial.println("Invalid API Key - Wait 10-15 min or check: https://home.openweathermap.org/api_keys");
    } else if (httpCode == 404) {
      Serial.print("City not found: ");
      Serial.println(cityName);
    } else if (httpCode == -1) {
      Serial.println("Connection failed - Check WiFi");
    } else if (httpCode == 429) {
      Serial.println("Too many requests - Rate limit exceeded");
    }
  }

  httpClient.end();
}

int currentIndex = 0;
TFT_eSprite clkb = TFT_eSprite(&tft);
void scrollBanner() {
  if (scrollText[currentIndex]) {
    clkb.setColorDepth(8);
    clkb.loadFont(ZdyLwFont_20);
    clkb.createSprite(150, 30);
    clkb.fillSprite(bgColor);
    clkb.setTextWrap(false);
    clkb.setTextDatum(CC_DATUM);
    clkb.setTextColor(TFT_WHITE, bgColor);
    clkb.drawString(scrollText[currentIndex], 74, 16);
    clkb.pushSprite(10, 45);

    clkb.deleteSprite();
    clkb.unloadFont();

    if (currentIndex >= 4)
      currentIndex = 0; // Back to first
    else
      currentIndex += 1; // Prepare to switch to next
  }
  prevTime = 1;
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

unsigned char Hour_sign = 60;
unsigned char Minute_sign = 60;
unsigned char Second_sign = 60;
void digitalClockDisplay(int reflash_en) {
  int timey = 90;
  if (hour() != Hour_sign || reflash_en == 1) // Clock refresh
  {
    dig.printfW3660(20, timey, hour() / 10);
    dig.printfW3660(60, timey, hour() % 10);
    Hour_sign = hour();
  }
  if (minute() != Minute_sign || reflash_en == 1) // Minute refresh
  {
    dig.printfO3660(101, timey, minute() / 10);
    dig.printfO3660(141, timey, minute() % 10);
    Minute_sign = minute();
  }
  if (second() != Second_sign || reflash_en == 1) // Second refresh
  {
    dig.printfW1830(182, timey + 30, second() / 10);
    dig.printfW1830(202, timey + 30, second() % 10);
    Second_sign = second();
  }

  if (reflash_en == 1)
    reflash_en = 0;
  /***Date****/
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);

  // Day of week
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(week(), 29, 16);
  clk.pushSprite(125, 170);
  clk.deleteSprite();

  // Day/Month/Year
  clk.createSprite(110, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(monthDay(), 55, 16);
  clk.pushSprite(15, 170);
  clk.deleteSprite();

  clk.unloadFont();
  /***Date****/
}

// Week
String week() {
  String wk[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  String s = wk[weekday() - 1];
  return s;
}

// Day/Month/Year
String monthDay() {
  String s = String(day());
  s = s + "/" + month() + "/" + year();
  return s;
}

/*-------- NTP code ----------*/
const int NTP_PACKET_SIZE = 48;     // NTP time is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming & outgoing packets
time_t getNtpTime() {
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0);
  //  get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE); // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 = (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      // Serial.println(secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // Return 0 when unable to get time
}

// Send request to NTP server
void sendNTPpacket(IPAddress &address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;          // Stratum, or type of clock
  packetBuffer[2] = 6;          // Polling Interval
  packetBuffer[3] = 0xEC;       // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void setup() {
  Serial.begin(115200);
  Serial.println("");
  EEPROM.begin(1024);
  pinMode(LCD_BL_PIN, OUTPUT);

  // Read backlight brightness setting from eeprom
  if (EEPROM.read(BL_addr) > 0 && EEPROM.read(BL_addr) <= 100)
    LCD_BL_PWM = EEPROM.read(BL_addr);

  // Read weather update time from eeprom
  updateweater_time = EEPROM.read(UpWeT_addr);

  tft.begin();
  tft.setRotation(0);
  analogWrite(LCD_BL_PIN, map(LCD_BL_PWM, 0, 100, 0, 255));
  tft.fillScreen(0x0000);
  tft.setTextColor(TFT_BLACK, bgColor);

  targetTime = millis() + 1000;
  readwificonfig(); // Read stored wifi information
  Serial.print("Connecting to WIFI ");
  Serial.println(wificonf.stassid);
  WiFi.begin(wificonf.stassid, wificonf.stapsw);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

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
    readwificonfig();
#if WebSever_EN
    // Start web server initialization
    Web_Sever_Init();
    Web_sever_Win();
    delay(1000);
#endif
  }

  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.println("Waiting for sync...");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  // Read saved city name and API key from EEPROM
  readCityNamefromEEP();
  readApiKeyfromEEP();
  
  // Set defaults if not configured
  if (cityName.length() == 0) {
    cityName = "Hanoi";
  }
  
  if (apiKey.length() == 0 || apiKey.length() < 32) {
    Serial.println("WARNING: API Key not configured!");
    Serial.println("Get free key at: https://openweathermap.org/api");
  }

  tft.fillScreen(TFT_BLACK); // Clear screen

  TJpgDec.drawJpg(15, 203, temperature, sizeof(temperature)); // Temperature icon
  TJpgDec.drawJpg(15, 233, humidity, sizeof(humidity));       // Humidity icon

  getWeatherDataFromOpenWeatherMap();
#if !WebSever_EN
  WiFi.forceSleepBegin(); // wifi off
  Serial.println("WIFI sleeping......");
  Wifi_en = 0;
#endif
}

void loop() {
#if WebSever_EN
  Web_Sever();
#endif
  LCD_reflash(0);
}
