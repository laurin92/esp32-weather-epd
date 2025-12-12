/* Client side utilities for esp32-weather-epd.
 * Copyright (C) 2022-2024  Luke Marzen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// built-in C++ libraries
#include <cstring>
#include <vector>

// arduino/esp32 libraries
#include <Arduino.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <time.h>
#include <WiFi.h>

// additional libraries
#include <Adafruit_BusIO_Register.h>
#include <ArduinoJson.h>

// header files
#include "_locale.h"
#include "api_response.h"
#include "aqi.h"
#include "client_utils.h"
#include "config.h"
#include "display_utils.h"
#include "renderer.h"
#include "mb_response.h"
#ifndef USE_HTTP
  #include <WiFiClientSecure.h>
#endif

#ifdef USE_HTTP
  static const uint16_t OWM_PORT = 80;
#else
  static const uint16_t OWM_PORT = 443;
#endif

/* Power-on and connect WiFi.
 * Takes int parameter to store WiFi RSSI, or “Received Signal Strength
 * Indicator"
 *
 * Returns WiFi status.
 */
wl_status_t startWiFi(int &wifiRSSI)
{
  WiFi.mode(WIFI_STA);
  Serial.printf("%s '%s'", TXT_CONNECTING_TO, WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // timeout if WiFi does not connect in WIFI_TIMEOUT ms from now
  unsigned long timeout = millis() + WIFI_TIMEOUT;
  wl_status_t connection_status = WiFi.status();

  while ((connection_status != WL_CONNECTED) && (millis() < timeout))
  {
    Serial.print(".");
    delay(50);
    connection_status = WiFi.status();
  }
  Serial.println();

  if (connection_status == WL_CONNECTED)
  {
    wifiRSSI = WiFi.RSSI(); // get WiFi signal strength now, because the WiFi
                            // will be turned off to save power!
    Serial.println("IP: " + WiFi.localIP().toString());
  }
  else
  {
    Serial.printf("%s '%s'\n", TXT_COULD_NOT_CONNECT_TO, WIFI_SSID);
  }
  return connection_status;
} // startWiFi

/* Disconnect and power-off WiFi.
 */
void killWiFi()
{
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
} // killWiFi

/* Prints the local time to serial monitor.
 *
 * Returns true if getting local time was a success, otherwise false.
 */
bool printLocalTime(tm *timeInfo)
{
  int attempts = 0;
  while (!getLocalTime(timeInfo) && attempts++ < 3)
  {
    Serial.println(TXT_FAILED_TO_GET_TIME);
    return false;
  }
  Serial.println(timeInfo, "%A, %B %d, %Y %H:%M:%S");
  return true;
} // printLocalTime

/* Waits for NTP server time sync, adjusted for the time zone specified in
 * config.cpp.
 *
 * Returns true if time was set successfully, otherwise false.
 *
 * Note: Must be connected to WiFi to get time from NTP server.
 */
bool waitForSNTPSync(tm *timeInfo)
{
  // Wait for SNTP synchronization to complete
  unsigned long timeout = millis() + NTP_TIMEOUT;
  if ((sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET)
      && (millis() < timeout))
  {
    Serial.print(TXT_WAITING_FOR_SNTP);
    delay(100); // ms
    while ((sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET)
        && (millis() < timeout))
    {
      Serial.print(".");
      delay(100); // ms
    }
    Serial.println();
  }
  return printLocalTime(timeInfo);
} // waitForSNTPSync

/* Perform an HTTP GET request to OpenWeatherMap's "One Call" API
 * If data is received, it will be parsed and stored in the global variable
 * owm_onecall.
 *
 * Returns the HTTP Status Code.
 */
#ifdef USE_HTTP
  int getOWMonecall(WiFiClient &client, owm_resp_onecall_t &r)
#else
  int getOWMonecall(WiFiClientSecure &client, owm_resp_onecall_t &r)
#endif
{
  int attempts = 0;
  bool rxSuccess = false;
  DeserializationError jsonErr = {};
  String uri = "/data/" + OWM_ONECALL_VERSION
               + "/onecall?lat=" + LAT + "&lon=" + LON + "&lang=" + OWM_LANG
               + "&units=standard&exclude=minutely";
#if !DISPLAY_ALERTS
  // exclude alerts
  uri += ",alerts";
#endif

  // This string is printed to terminal to help with debugging. The API key is
  // censored to reduce the risk of users exposing their key.
  String sanitizedUri = OWM_ENDPOINT + uri + "&appid={API key}";

  uri += "&appid=" + OWM_APIKEY;

  Serial.print(TXT_ATTEMPTING_HTTP_REQ);
  Serial.println(": " + sanitizedUri);
  int httpResponse = 0;
  while (!rxSuccess && attempts < 3)
  {
    wl_status_t connection_status = WiFi.status();
    if (connection_status != WL_CONNECTED)
    {
      // -512 offset distinguishes these errors from httpClient errors
      return -512 - static_cast<int>(connection_status);
    }

    HTTPClient http;
    http.setConnectTimeout(HTTP_CLIENT_TCP_TIMEOUT); // default 5000ms
    http.setTimeout(HTTP_CLIENT_TCP_TIMEOUT); // default 5000ms
    http.begin(client, OWM_ENDPOINT, OWM_PORT, uri);
    httpResponse = http.GET();
    if (httpResponse == HTTP_CODE_OK)
    {
      jsonErr = deserializeOneCall(http.getStream(), r);
      if (jsonErr)
      {
        // -256 offset distinguishes these errors from httpClient errors
        httpResponse = -256 - static_cast<int>(jsonErr.code());
      }
      rxSuccess = !jsonErr;
    }
    client.stop();
    http.end();
    Serial.println("  " + String(httpResponse, DEC) + " "
                   + getHttpResponsePhrase(httpResponse));
    ++attempts;
  }

  return httpResponse;
} // getOWMonecall

/* Perform an HTTP GET request to OpenWeatherMap's "Air Pollution" API
 * If data is received, it will be parsed and stored in the global variable
 * owm_air_pollution.
 *
 * Returns the HTTP Status Code.
 */
#ifdef USE_HTTP
  int getOWMairpollution(WiFiClient &client, owm_resp_air_pollution_t &r)
#else
  int getOWMairpollution(WiFiClientSecure &client, owm_resp_air_pollution_t &r)
#endif
{
  int attempts = 0;
  bool rxSuccess = false;
  DeserializationError jsonErr = {};

  // set start and end to appropriate values so that the last 24 hours of air
  // pollution history is returned. Unix, UTC.
  time_t now;
  int64_t end = time(&now);
  // minus 1 is important here, otherwise we could get an extra hour of history
  int64_t start = end - ((3600 * OWM_NUM_AIR_POLLUTION) - 1);
  char endStr[22];
  char startStr[22];
  sprintf(endStr, "%lld", end);
  sprintf(startStr, "%lld", start);
  String uri = "/data/2.5/air_pollution/history?lat=" + LAT + "&lon=" + LON
               + "&start=" + startStr + "&end=" + endStr
               + "&appid=" + OWM_APIKEY;
  // This string is printed to terminal to help with debugging. The API key is
  // censored to reduce the risk of users exposing their key.
  String sanitizedUri = OWM_ENDPOINT +
               "/data/2.5/air_pollution/history?lat=" + LAT + "&lon=" + LON
               + "&start=" + startStr + "&end=" + endStr
               + "&appid={API key}";

  Serial.print(TXT_ATTEMPTING_HTTP_REQ);
  Serial.println(": " + sanitizedUri);
  int httpResponse = 0;
  while (!rxSuccess && attempts < 3)
  {
    wl_status_t connection_status = WiFi.status();
    if (connection_status != WL_CONNECTED)
    {
      // -512 offset distinguishes these errors from httpClient errors
      return -512 - static_cast<int>(connection_status);
    }

    HTTPClient http;
    http.setConnectTimeout(HTTP_CLIENT_TCP_TIMEOUT); // default 5000ms
    http.setTimeout(HTTP_CLIENT_TCP_TIMEOUT); // default 5000ms
    http.begin(client, OWM_ENDPOINT, OWM_PORT, uri);
    httpResponse = http.GET();
    if (httpResponse == HTTP_CODE_OK)
    {
      jsonErr = deserializeAirQuality(http.getStream(), r);
      if (jsonErr)
      {
        // -256 offset to distinguishes these errors from httpClient errors
        httpResponse = -256 - static_cast<int>(jsonErr.code());
      }
      rxSuccess = !jsonErr;
    }
    client.stop();
    http.end();
    Serial.println("  " + String(httpResponse, DEC) + " "
                   + getHttpResponsePhrase(httpResponse));
    ++attempts;
  }

  return httpResponse;
} // getOWMairpollution

/* Perform an HTTP GET request to Meteoblue API
 * Fetches weather data and converts it to OWM-compatible structures.
 * The MB_API_URL contains the complete URL including the API key.
 *
 * Returns the HTTP Status Code.
 */
#ifdef USE_HTTP
  int getMBweather(WiFiClient &client, owm_resp_onecall_t &owm_onecall,
                   owm_resp_air_pollution_t &owm_air)
#else
  int getMBweather(WiFiClientSecure &client, owm_resp_onecall_t &owm_onecall,
                   owm_resp_air_pollution_t &owm_air)
#endif
{
  int attempts = 0;
  bool rxSuccess = false;
  DeserializationError jsonErr = {};
  mb_raw_response_t mb_raw = {};

  // MB_API_URL should contain the full URL including the API key
  // Example: "https://my.meteoblue.com/packages/basic-1h_clouds-1h?apikey=YOURAPIKEY&lat=48.5&lon=9.0&format=json"
  
  // Parse the MB_API_URL to extract host and path
  String url = MB_API_URL;
  String host = "";
  String path = "";
  int port = 443; // Default HTTPS port
  
  // Simple URL parsing
  if (url.startsWith("https://"))
  {
    url = url.substring(8); // Remove "https://"
    port = 443;
  }
  else if (url.startsWith("http://"))
  {
    url = url.substring(7); // Remove "http://"
    port = 80;
  }
  
  int slashIndex = url.indexOf('/');
  if (slashIndex > 0)
  {
    host = url.substring(0, slashIndex);
    path = url.substring(slashIndex);
  }
  else
  {
    host = url;
    path = "/";
  }

  Serial.print(TXT_ATTEMPTING_HTTP_REQ);
  Serial.println(": " + host + path);
  
  int httpResponse = 0;
  while (!rxSuccess && attempts < 3)
  {
    wl_status_t connection_status = WiFi.status();
    if (connection_status != WL_CONNECTED)
    {
      // -512 offset distinguishes these errors from httpClient errors
      return -512 - static_cast<int>(connection_status);
    }

    HTTPClient http;
    http.setConnectTimeout(HTTP_CLIENT_TCP_TIMEOUT);
    http.setTimeout(HTTP_CLIENT_TCP_TIMEOUT);
    http.begin(client, host, port, path);
    httpResponse = http.GET();
    
#if DEBUG_LEVEL >= 2
    Serial.print("[debug] MB HTTP Response Code: ");
    Serial.println(httpResponse);
#endif
    
    if (httpResponse == HTTP_CODE_OK)
    {
#if DEBUG_LEVEL >= 2
      // Print raw response for debugging
      String payload = http.getString();
      Serial.println("[debug] MB API Response Length: " + String(payload.length()));
      Serial.println("[debug] MB API Response:");
      Serial.println(payload);
      
      // Deserialize from the string we just got
      JsonDocument doc;
      jsonErr = deserializeJson(doc, payload);
      
      if (jsonErr)
      {
        Serial.print("[debug] JSON Parse Error: ");
        Serial.println(jsonErr.c_str());
        httpResponse = -256 - static_cast<int>(jsonErr.code());
      }
      else
      {
        Serial.println("[debug] JSON parsed successfully, now extracting to MB structure...");
        
        // Parse metadata
        JsonObject metadata = doc["metadata"];
        mb_raw.latitude = metadata["latitude"].as<float>();
        mb_raw.longitude = metadata["longitude"].as<float>();
        mb_raw.timezone_abbr = metadata["timezone_abbrevation"].as<const char *>();
        mb_raw.utc_timeoffset = metadata["utc_timeoffset"].as<int>();

        // Parse hourly data arrays
        JsonObject data_1h = doc["data_1h"];
        JsonArray time_array = data_1h["time"];
        JsonArray temp_array = data_1h["temperature"];
        JsonArray felt_array = data_1h["felttemperature"];
        JsonArray humidity_array = data_1h["relativehumidity"];
        JsonArray windspeed_array = data_1h["windspeed"];
        JsonArray winddir_array = data_1h["winddirection"];
        JsonArray precip_array = data_1h["precipitation"];
        JsonArray precip_prob_array = data_1h["precipitation_probability"];
        JsonArray snow_array = data_1h["snowfraction"];
        JsonArray picto_array = data_1h["pictocode"];
        JsonArray conv_precip_array = data_1h["convective_precipitation"];
        JsonArray uv_array = data_1h["uvindex"];
        JsonArray pressure_array = data_1h["sealevelpressure"];
        JsonArray daylight_array = data_1h["isdaylight"];
        JsonArray rainspot_array = data_1h["rainspot"];

        // Store first 48 hours for hourly display
        int total_hours = time_array.size();
        int num_hours = (total_hours > 48) ? 48 : total_hours;
        
        Serial.println("[debug] Total hours in API: " + String(total_hours) + 
                       ", storing " + String(num_hours) + " for hourly display");
        
        for (int i = 0; i < num_hours; ++i)
        {
          mb_raw_hourly_t hour_data = {};
          
          hour_data.time = time_array[i].as<const char *>();
          hour_data.temperature = temp_array[i].as<float>();
          hour_data.felttemperature = felt_array[i].as<float>();
          hour_data.relativehumidity = humidity_array[i].as<int>();
          hour_data.windspeed = windspeed_array[i].as<float>();
          hour_data.winddirection = winddir_array[i].as<int>();
          hour_data.precipitation = precip_array[i].as<float>();
          hour_data.precipitation_probability = precip_prob_array[i].as<int>();
          hour_data.snowfraction = snow_array[i].as<float>();
          hour_data.pictocode = picto_array[i].as<int>();
          hour_data.convective_precipitation = conv_precip_array[i].as<float>();
          hour_data.uvindex = uv_array[i].as<int>();
          hour_data.sealevelpressure = pressure_array[i].as<float>();
          hour_data.isdaylight = daylight_array[i].as<int>();
          hour_data.rainspot = rainspot_array[i].as<const char *>();

          mb_raw.hourly_data.push_back(hour_data);
        }
        
        Serial.println("[debug] Extracted " + String(mb_raw.hourly_data.size()) + " hours");
        
        // First convert MB data to OWM format (initializes current + hourly + basic daily)
        convertMBtoOWM(mb_raw, owm_onecall, owm_air);
        Serial.println("[debug] Basic conversion complete, now aggregating extended daily forecast...");
        
        // Now aggregate daily data directly from JSON arrays (up to 192 hours for 8 days)
        // This overwrites the daily[] array with more accurate aggregations from full data
        int daily_hours = (total_hours > 192) ? 192 : total_hours;
        
        for (int day = 0; day < 8 && (day * 24) < daily_hours; ++day)
        {
          int start_hour = day * 24;
          int end_hour = start_hour + 24;
          if (end_hour > daily_hours) end_hour = daily_hours;
          
          float temp_min = 1000.0f;
          float temp_max = -1000.0f;
          float precip_sum = 0.0f;
          int pop_max = 0;
          int pictocode_noon = 1;
          
          for (int h = start_hour; h < end_hour; ++h)
          {
            float temp = temp_array[h].as<float>();
            if (temp < temp_min) temp_min = temp;
            if (temp > temp_max) temp_max = temp;
            precip_sum += precip_array[h].as<float>();
            int pop = precip_prob_array[h].as<int>();
            if (pop > pop_max) pop_max = pop;
            
            // Get pictocode around noon
            if (h == start_hour + 12) {
              pictocode_noon = picto_array[h].as<int>();
            }
          }
          
          // Populate owm_onecall.daily[day] directly
          int64_t day_timestamp = parseTimeString(time_array[start_hour].as<const char *>());
          
          owm_onecall.daily[day].dt = day_timestamp;
          owm_onecall.daily[day].temp.min = temp_min + 273.15f;
          owm_onecall.daily[day].temp.max = temp_max + 273.15f;
          owm_onecall.daily[day].temp.day = (temp_min + temp_max) / 2.0f + 273.15f;
          owm_onecall.daily[day].temp.night = temp_min + 273.15f;
          owm_onecall.daily[day].temp.eve = temp_max + 273.15f;
          owm_onecall.daily[day].temp.morn = temp_min + 273.15f;
          
          owm_onecall.daily[day].feels_like.day = owm_onecall.daily[day].temp.day;
          owm_onecall.daily[day].feels_like.night = owm_onecall.daily[day].temp.night;
          owm_onecall.daily[day].feels_like.eve = owm_onecall.daily[day].temp.eve;
          owm_onecall.daily[day].feels_like.morn = owm_onecall.daily[day].temp.morn;
          
          owm_onecall.daily[day].pressure = static_cast<int>(pressure_array[start_hour].as<float>());
          owm_onecall.daily[day].humidity = humidity_array[start_hour].as<int>();
          owm_onecall.daily[day].wind_speed = windspeed_array[start_hour].as<float>();
          owm_onecall.daily[day].wind_deg = winddir_array[start_hour].as<int>();
          owm_onecall.daily[day].pop = pop_max / 100.0f;
          owm_onecall.daily[day].rain = precip_sum;
          owm_onecall.daily[day].uvi = uv_array[start_hour + 12 < daily_hours ? start_hour + 12 : start_hour].as<int>();
          
          owm_onecall.daily[day].weather.id = pictocodeToOWMId(pictocode_noon);
          owm_onecall.daily[day].weather.main = "Unknown";
          owm_onecall.daily[day].weather.description = "MB daily";
          owm_onecall.daily[day].weather.icon = "01d";
          
          owm_onecall.daily[day].sunrise = owm_onecall.current.sunrise + (day * 86400);
          owm_onecall.daily[day].sunset = owm_onecall.current.sunset + (day * 86400);
          owm_onecall.daily[day].dew_point = 0.0f;
          owm_onecall.daily[day].clouds = 0;
          owm_onecall.daily[day].visibility = 10000;
          owm_onecall.daily[day].wind_gust = 0.0f;
          owm_onecall.daily[day].snow = 0.0f;
          owm_onecall.daily[day].moonrise = 0;
          owm_onecall.daily[day].moonset = 0;
          owm_onecall.daily[day].moon_phase = 0.0f;
        }
        
        Serial.println("[debug] Aggregated " + String(8) + " days of forecast");
        
        Serial.println("[debug] MB to OWM conversion complete!");
        rxSuccess = true;
      }
#else
      jsonErr = deserializeMeteoblue(http.getStream(), mb_raw);
      if (jsonErr)
      {
        httpResponse = -256 - static_cast<int>(jsonErr.code());
      }
      else
      {
        convertMBtoOWM(mb_raw, owm_onecall, owm_air);
        rxSuccess = true;
      }
#endif
    }
    
    client.stop();
    http.end();
    Serial.println("  " + String(httpResponse, DEC) + " "
                   + getHttpResponsePhrase(httpResponse));
    ++attempts;
  }

  return httpResponse;
} // getMBweather

/* Prints debug information about heap usage.
 */
void printHeapUsage() {
  Serial.println("[debug] Heap Size       : "
                 + String(ESP.getHeapSize()) + " B");
  Serial.println("[debug] Available Heap  : "
                 + String(ESP.getFreeHeap()) + " B");
  Serial.println("[debug] Min Free Heap   : "
                 + String(ESP.getMinFreeHeap()) + " B");
  Serial.println("[debug] Max Allocatable : "
                 + String(ESP.getMaxAllocHeap()) + " B");
  return;
}

