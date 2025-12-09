/*
  board settings:
  board:            ESP32 Dev Module
  upload speed:     921600
  cpu frequency:    240MHz (WiFi/BT)
  flash frequency:  40MHz
  flash mode:       DIO
  flash size:       4MB (32Mb)
  psram:            disabled

  model: GxGDEW0213M21
  resolution: 212x104

  model: GDEM0213B74
  resolution: 250x122
*/

#define LILYGO_T5_V213

#include "env.h"
#include <ArduinoJson.h>     // https://github.com/bblanchon/ArduinoJson
#include "time.h"
#include <SPI.h>
#define  ENABLE_GxEPD2_display 0
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "icons.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

enum alignmentType {LEFT, RIGHT, CENTER};

// Connections for Lilygo TTGO T5 V2.3_2.13 from
// https://github.com/lewisxhe/TTGO-EPaper-Series#board-pins
static const uint8_t EPD_BUSY = 4;
static const uint8_t EPD_CS   = 5;
static const uint8_t EPD_RST  = 16;
static const uint8_t EPD_DC   = 17; //Data/Command
static const uint8_t EPD_SCK  = 18; //CLK on pinout?
static const uint8_t EPD_MISO = -1; // Master-In Slave-Out not used, as no data from display
static const uint8_t EPD_MOSI = 23;

// Uncomment the correct one based on the model being used
// GxEPD2_BW<GxEPD2_213_M21, GxEPD2_213_M21::HEIGHT> display(GxEPD2_213_M21(/*CS=D8*/ EPD_CS, /*DC=D3*/ EPD_DC, /*RST=D4*/ EPD_RST, /*BUSY=D2*/ EPD_BUSY));
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(GxEPD2_213_B74(/*CS=D8*/ EPD_CS, /*DC=D3*/ EPD_DC, /*RST=D4*/ EPD_RST, /*BUSY=D2*/ EPD_BUSY));

// Select u8g2 font from here: https://github.com/olikraus/u8g2/wiki/fntlistall
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

const uint8_t* FONT_SMALL = u8g2_font_courB08_tf;
const uint8_t* FONT_MEDIUM = u8g2_font_courB10_tf;
const uint8_t* FONT_LARGE = u8g2_font_courB14_tf;
const uint8_t* FONT_XLARGE = u8g2_font_courB18_tf;

String  time_string, date_string;
int     current_hour = 0, current_min = 0, current_sec = 0;
long    startTime = 0;

// using ° can cause spacing issues
const char DEGREE_SIGN = char(176);

typedef struct {
    int      timestamp;
    String   icon;
    String   description;
    float    temperature;
    float    feels_like;
    float    humidity;
    float    high;
    float    low;
    float    wind_direction;
    float    wind_speed;
    int      sunrise;
    int      sunset;
} Weather;

#define forecast_count 5
Weather  weather_current[1];
Weather  weather_forecast[forecast_count];

void setup() {
    startTime = millis();
    Serial.begin(115200);
    if (startWiFi() == WL_CONNECTED && setupTime() == true) {
        if ((current_hour >= WAKEUP_TIME && current_hour <= SLEEP_TIME)) {
            initializeDisplay();

            Serial.println("Attempt to get weather");
            int attempts = 1;
            bool received_weather = false, received_forecast = false;
            WiFiClient client;
            // Try up-to 5 times for Weather and Forecast data
            while ((received_weather == false || received_forecast == false) && attempts <= 5) {
                if (received_weather  == false) received_weather  = getWeatherData(client, "weather");
                if (received_forecast == false) received_forecast = getWeatherData(client, "forecast");
                attempts++;
            }
            // Reduces power consumption
            StopWiFi();

            // Only if received both Weather or Forecast proceed
            if (received_weather && received_forecast) {
                Serial.println("Displaying Weather");
                displayWeather();
                // Full screen update mode
                display.display(false);
            }
        }
    }
    sleep();
}

// never runs
void loop() {
}

void sleep() {
    display.powerOff();
    //Some ESP32 are too fast to maintain accurate time
    long sleep_time = (SLEEP_DURATION * 60 - ((current_min % SLEEP_DURATION) * 60 + current_sec));
    // Added 20-sec extra delay to cater for slow ESP32 RTC timers
    esp_sleep_enable_timer_wakeup((sleep_time+20) * 1000000LL);
#ifdef BUILTIN_LED
    // If it's On, turn it off and some boards use GPIO-5 for SPI-SS, which remains low after screen use
    pinMode(BUILTIN_LED, INPUT);
    digitalWrite(BUILTIN_LED, HIGH);
#endif
    Serial.println("Awake for : " + String((millis() - startTime) / 1000.0, 3) + "-secs");
    Serial.println("Entering " + String(sleep_time) + "-secs of sleep time");
    Serial.println("Starting deep-sleep.");
    esp_deep_sleep_start();  // Sleep for e.g. 30 minutes
}

void displayWeather() {
    updateLocalTime();
    drawHeadingSection();
    drawMainWeatherSection();

    for(int i=0; i < forecast_count; i++) {
        draw3hrForecast(i);
    }
}

void drawHeadingSection() {
    u8g2Fonts.setFont(FONT_SMALL);
    drawString(0, 0, date_string, LEFT);
    drawString(DISPLAY_WIDTH / 2, 0, time_string, CENTER);
    drawBattery();
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, GxEPD_BLACK);
}

void drawMainWeatherSection() {
    u8g2Fonts.setFont(FONT_LARGE); // Explore u8g2 fonts from here: https://github.com/olikraus/u8g2/wiki/fntlistall
    drawString(0, 25, String(weather_current[0].temperature, DECIMALS) + DEGREE_SIGN + "/" + String(weather_current[0].humidity, 0) + "%", LEFT);

    if (MODEL == "GxGDEW0213M21") display.drawBitmap(100, 13, getIcon(weather_current[0].icon, "medium"), 35, 35, GxEPD_BLACK);
    if (MODEL == "GDEM0213B74") display.drawBitmap((DISPLAY_WIDTH / 2) - 12, 14, getIcon(weather_current[0].icon, "large"), 45, 45, GxEPD_BLACK);

    u8g2Fonts.setFont(FONT_SMALL);
    drawString(0, 35, "Feels like " + String(weather_current[0].feels_like, DECIMALS) + DEGREE_SIGN, LEFT);
    String Wx_Description = weather_current[0].description;
    int y_above_forecast = DISPLAY_HEIGHT - 57;
    if (MODEL == "GxGDEW0213M21") y_above_forecast = DISPLAY_HEIGHT - 47;
    drawString(0, y_above_forecast - 10, titleCase(Wx_Description), LEFT);

    // sun section
    int sun_y = 15;
    drawString(DISPLAY_WIDTH - 30, sun_y, convertUnixTime(weather_current[0].sunrise), RIGHT);
    drawString(DISPLAY_WIDTH - 30, sun_y + 12, convertUnixTime(weather_current[0].sunset), RIGHT);
    display.drawBitmap(DISPLAY_WIDTH - 30, sun_y - 9, iconSunrise, 35, 35, GxEPD_BLACK);

    // wind section
    String wind_string = windDegToDirection(weather_current[0].wind_direction) + " " + String(weather_current[0].wind_speed, DECIMALS) + String(UNITS == "M" ? " m/s" : " mph");
    drawString(DISPLAY_WIDTH, y_above_forecast - 10, wind_string, RIGHT);
    
    // bottom line
    display.drawLine(0, y_above_forecast, DISPLAY_WIDTH, y_above_forecast, GxEPD_BLACK);
}

// displaying from the bottom going up in order keep positions relative
void draw3hrForecast(int index) {
    int width = (DISPLAY_WIDTH / 5);
    int x_position = width * index;
    int y_position = DISPLAY_HEIGHT - 1;

    u8g2Fonts.setFont(FONT_SMALL);
    display.drawLine(x_position, y_position, x_position + width, y_position, GxEPD_BLACK);
    y_position -= 10;
    String temperature = String(weather_forecast[index].high, 0) + DEGREE_SIGN + "/" + String(weather_forecast[index].low, 0) + DEGREE_SIGN;
    drawString(x_position + (width / 2), y_position, temperature, CENTER);
    if (MODEL == "GxGDEW0213M21") {
        y_position -= 25;
        display.drawBitmap(x_position + ((width - 25) / 2), y_position, getIcon(weather_forecast[index].icon, "small"), 25, 25, GxEPD_BLACK);
    } else {
        y_position -= 35;
        display.drawBitmap(x_position + ((width - 35) / 2), y_position, getIcon(weather_forecast[index].icon, "medium"), 35, 35, GxEPD_BLACK);
    }
    y_position -= 9;
    drawString(x_position + (width / 2), y_position, convertUnixTime(weather_forecast[index].timestamp), CENTER);
    y_position -= 2;
    display.drawLine(x_position + width, y_position, x_position + width, DISPLAY_HEIGHT, GxEPD_BLACK);
}

String windDegToDirection(float winddirection) {
    if (winddirection >= 348.75 || winddirection < 11.25)  return "N";
    if (winddirection >=  11.25 && winddirection < 33.75)  return "NNE";
    if (winddirection >=  33.75 && winddirection < 56.25)  return "NE";
    if (winddirection >=  56.25 && winddirection < 78.75)  return "ENE";
    if (winddirection >=  78.75 && winddirection < 101.25) return "E";
    if (winddirection >= 101.25 && winddirection < 123.75) return "ESE";
    if (winddirection >= 123.75 && winddirection < 146.25) return "SE";
    if (winddirection >= 146.25 && winddirection < 168.75) return "SSE";
    if (winddirection >= 168.75 && winddirection < 191.25) return "S";
    if (winddirection >= 191.25 && winddirection < 213.75) return "SSW";
    if (winddirection >= 213.75 && winddirection < 236.25) return "SW";
    if (winddirection >= 236.25 && winddirection < 258.75) return "WSW";
    if (winddirection >= 258.75 && winddirection < 281.25) return "W";
    if (winddirection >= 281.25 && winddirection < 303.75) return "WNW";
    if (winddirection >= 303.75 && winddirection < 326.25) return "NW";
    if (winddirection >= 326.25 && winddirection < 348.75) return "NNW";
    return "?";
}

void drawBattery() {
    uint8_t percentage = 100;
    float voltage = analogRead(35) / 4096.0 * 7.46;
    if (voltage > 1 ) {
        Serial.println("Voltage = " + String(voltage));
        percentage = 2836.9625 * pow(voltage, 4) - 43987.4889 * pow(voltage, 3) + 255233.8134 * pow(voltage, 2) - 656689.7123 * voltage + 632041.7303;
        if (voltage >= 4.20) percentage = 100;
        if (voltage <= 3.50) percentage = 0;
        display.drawRect(DISPLAY_WIDTH - 22, 0, 20, 9, GxEPD_BLACK);
        display.fillRect(DISPLAY_WIDTH - 2, 2, 2, 5, GxEPD_BLACK);
        display.fillRect(DISPLAY_WIDTH - 20, 2, 16 * percentage / 100.0, 5, GxEPD_BLACK);
        drawString(DISPLAY_WIDTH - 23, 0, String(percentage) + "%", RIGHT);
    }
}

void drawString(int x, int y, String text, alignmentType alignment) {
    int16_t  x1, y1;
    uint16_t w, h;
    display.setTextWrap(false);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    if (alignment == RIGHT)  x = x - w;
    if (alignment == CENTER) x = x - (w / 2);
    u8g2Fonts.setCursor(x, y + h);
    u8g2Fonts.print(text);
}

uint8_t startWiFi() {
    Serial.println("Connecting to: " + String(SSID));

    WiFi.disconnect();
    WiFi.mode(WIFI_MODE_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(SSID, PASSWORD);

    unsigned long start = millis();
    uint8_t connection_status;
    bool connecting = true;
    while (connecting) {
        connection_status = WiFi.status();

        // Wait 15-secs maximum
        if (millis() > start + 15000) {
            connecting = false;
        }
        if (connection_status == WL_CONNECTED || connection_status == WL_CONNECT_FAILED) {
            connecting = false;
        }
        delay(50);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected at: " + WiFi.localIP().toString());
    } else {
        Serial.println("WiFi connection failed");
    }
    return WiFi.status();
}

void StopWiFi() {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
}

boolean setupTime() {
    // configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER, "time.nist.gov");
    configTime(0, 0, NTP_SERVER);  // UTC only
    setenv("TZ", TIMEZONE, 1);
    tzset();
    delay(100);
    bool time_status = updateLocalTime();
    return time_status;
}

boolean updateLocalTime() {
    struct tm time;
    char   time_output[30], day_output[30], update_time[30];

    // Wait for 5-sec for time to synchronise
    while (!getLocalTime(&time, 5000)) {
        Serial.println("Failed to obtain time");
        return false;
    }
    current_hour = time.tm_hour;
    current_min  = time.tm_min;
    current_sec  = time.tm_sec;

    strftime(day_output, sizeof(day_output), DATE_FORMAT, &time);
    strftime(update_time, sizeof(update_time), TIME_FORMAT, &time);
    sprintf(time_output, "%s", update_time);

    date_string = day_output;
    time_string = time_output;
    return true;
}

void initializeDisplay() {
    Serial.println("Initializing Display");
    display.init(115200, true, 2, false);
    SPI.end();
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    display.setRotation(3);                    // Use 1 or 3 for landscape modes
    u8g2Fonts.begin(display);                  // connect u8g2 procedures to Adafruit GFX
    u8g2Fonts.setFontMode(1);                  // use u8g2 transparent mode (this is default)
    u8g2Fonts.setFontDirection(0);             // left to right (this is default)
    u8g2Fonts.setForegroundColor(GxEPD_BLACK); // apply Adafruit GFX color
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE); // apply Adafruit GFX color
    u8g2Fonts.setFont(FONT_MEDIUM);   // Explore u8g2 fonts from here: https://github.com/olikraus/u8g2/wiki/fntlistall
    display.fillScreen(GxEPD_WHITE);
    display.setFullWindow();
}

String titleCase(String text){
    if (text.length() > 0) {
        String temp_text = text.substring(0,1);
        temp_text.toUpperCase();
        return temp_text + text.substring(1);
    } else {
        return text;
    }
}

// convert unix time based on TIME_FORMAT env setting
String convertUnixTime(int unix_time) {
    time_t time = unix_time;
    struct tm *time_now = localtime(&time);
    char output[40];
    strftime(output, sizeof(output), TIME_FORMAT, time_now);
    return output;
}

bool getWeatherData(WiFiClient& client, const String& request_type) {
    Serial.println("Attempting to get weather data");
    const String units = (UNITS == "M" ? "metric" : "imperial");
    client.stop();

    HTTPClient http;
    // for city name
    // String url = API_URL + "/data/2.5/" + request_type + "?q=" + CITY + "," + COUNTRY + "&APPID=" + API_KEY + "&mode=json&units=" + units + "&lang=" + LANGUAGE;
    // for city id
    String url = API_URL + "/data/2.5/" + request_type + "?id=" + CITY_ID + "&APPID=" + API_KEY + "&mode=json&units=" + units + "&lang=" + LANGUAGE;

    if(request_type != "weather") {
        url += "&cnt=" + String(forecast_count);
    }

    http.begin(url);
    int httpCode = http.GET();
    if(httpCode == HTTP_CODE_OK) {
        DynamicJsonDocument doc(35 * 1024);
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (error) {
            Serial.println("deserializeJson() failed: " + String(error.c_str()));
            client.stop();
            http.end();
            return false;
        }

        JsonObject root = doc.as<JsonObject>();
        if (request_type == "weather") {
            weather_current[0].description = root["weather"][0]["description"].as<String>();
            if(root["weather"][1]["description"].as<String>() != "" && root["weather"][1]["description"].as<String>() != "null") weather_current[0].description += " & " + root["weather"][1]["description"].as<String>();
            if(root["weather"][2]["description"].as<String>() != "" && root["weather"][2]["description"].as<String>() != "null") weather_current[0].description += " & " + root["weather"][2]["description"].as<String>();
            weather_current[0].icon             = root["weather"][0]["icon"].as<String>();
            weather_current[0].temperature      = root["main"]["temp"].as<float>();
            weather_current[0].humidity         = root["main"]["humidity"].as<float>();
            weather_current[0].feels_like       = root["main"]["feels_like"].as<float>();
            weather_current[0].wind_speed       = root["wind"]["speed"].as<float>();
            weather_current[0].wind_direction   = root["wind"]["deg"].as<float>();
            weather_current[0].sunrise          = root["sys"]["sunrise"].as<int>();
            weather_current[0].sunset           = root["sys"]["sunset"].as<int>();
            // others include: [ ["coord"]["lat"], ["coord"]["lon"], ["clouds"]["all"], ["visibility"], ["rain"][x], ["snow"][x], ["main"]["pressure"], ["main"]["temp_min"], ["main"]["temp_max"], [0]["main"] ]
        }
        if (request_type == "forecast") {
            JsonArray list = root["list"];
            for (byte i = 0; i < forecast_count; i++) {
                weather_forecast[i].timestamp    = list[i]["dt"].as<int>();
                weather_forecast[i].low   = list[i]["main"]["temp_min"].as<float>();
                weather_forecast[i].high  = list[i]["main"]["temp_max"].as<float>();
                weather_forecast[i].icon  = list[i]["weather"][0]["icon"].as<String>();
            }
        }

        client.stop();
        http.end();
        return true;
    } else {
        Serial.printf("connection failed, error: %s", http.errorToString(httpCode).c_str());
        client.stop();
        http.end();
        return false;
    }
    return true;
}

const unsigned char* getIcon(String icon, String iconSize) {
    // if (iconSize == "large") return iconClearLarge;
    // clear sky
    if (icon == "01d") return (iconSize == "small") ? iconClearSmall : (iconSize == "large") ? iconClearLarge : iconClear;
    if (icon == "01n") return (iconSize == "small") ? iconClearNightSmall : ((iconSize == "large") ? iconClearNightLarge : iconClearNight);
    // few clouds
    if (icon == "02d") return (iconSize == "small") ? iconFewCloudsSmall : ((iconSize == "large") ? iconFewCloudsLarge : iconFewClouds);
    if (icon == "02n") return (iconSize == "small") ? iconFewCloudsNightSmall : ((iconSize == "large") ? iconFewCloudsNightLarge : iconFewCloudsNight);
    // scattered clouds
    if (icon == "03d" || icon == "03n") return (iconSize == "small") ? iconScatteredCloudsSmall : ((iconSize == "large") ? iconScatteredCloudsLarge : iconScatteredClouds);
    // broken clouds
    if (icon == "04d" || icon == "04n") return (iconSize == "small") ? iconBrokenCloudsSmall : ((iconSize == "large") ? iconBrokenCloudsLarge : iconBrokenClouds);
    // shower rain
    if (icon == "09d" || icon == "09n") return (iconSize == "small") ? iconShowerRainSmall : ((iconSize == "large") ? iconShowerRainLarge : iconShowerRain);
    // rain
    if (icon == "10d" || icon == "10n") return (iconSize == "small") ? iconRainSmall : ((iconSize == "large") ? iconRainLarge : iconRain);
    // thunderstorm
    if (icon == "11d" || icon == "11n") return (iconSize == "small") ? iconThunderstormSmall : ((iconSize == "large") ? iconThunderstormLarge : iconThunderstorm);
    // snow
    if (icon == "13d" || icon == "13n") return (iconSize == "small") ? iconSnowSmall : ((iconSize == "large") ? iconSnowLarge : iconSnow);
    // mist
    if (icon == "50d") return (iconSize == "small") ? iconMistSmall : ((iconSize == "large") ? iconMistLarge : iconMist);
    if (icon == "50n") return (iconSize == "small") ? iconMistNightSmall : ((iconSize == "large") ? iconMistNightLarge : iconMistNight);
    return (iconSize == "small") ? iconNASmall : ((iconSize == "large") ? iconNALarge : iconNA);
}
