/* Meteoblue API response deserialization for esp32-weather-epd.
 * Copyright (C) 2025
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

#include <vector>
#include <ctime>
#include <ArduinoJson.h>
#include "mb_response.h"
#include "api_response.h"
#include "config.h"

/* Parse ISO 8601 time string to Unix timestamp
 * Format: "YYYY-MM-DD hh:mm"
 */
int64_t parseTimeString(const String &timeStr)
{
  struct tm tm = {};
  int year, month, day, hour, minute;
  
  if (sscanf(timeStr.c_str(), "%d-%d-%d %d:%d", 
             &year, &month, &day, &hour, &minute) == 5)
  {
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    
    return mktime(&tm);
  }
  
  return 0;
}

/* Overload for const char* */
int64_t parseTimeString(const char *time_str)
{
  if (time_str == nullptr) return 0;
  return parseTimeString(String(time_str));
}

/* Map Meteoblue pictocode to OpenWeatherMap weather ID
 * This is an approximation based on typical weather conditions
 */
int pictocodeToOWMId(int pictocode)
{
  // Meteoblue pictocodes mapping to OWM IDs
  // Reference: https://content.meteoblue.com/en/help/standards/symbols-and-pictograms
  switch (pictocode)
  {
    case 1:  return 800;  // Clear, cloudless sky
    case 2:  return 801;  // Clear, few cirrus
    case 3:  return 801;  // Clear with cirrus
    case 4:  return 802;  // Clear with few low clouds
    case 5:  return 802;  // Clear with few low clouds and cirrus
    case 6:  return 803;  // Partly cloudy
    case 7:  return 804;  // Cloudy
    case 8:  return 804;  // Overcast
    case 9:  return 804;  // Overcast with rain
    case 10: return 500;  // Light rain
    case 11: return 501;  // Rain
    case 12: return 502;  // Heavy rain
    case 13: return 600;  // Light snow
    case 14: return 601;  // Snow
    case 15: return 602;  // Heavy snow
    case 16: return 611;  // Sleet
    case 17: return 300;  // Light rain shower
    case 18: return 520;  // Rain shower
    case 19: return 615;  // Snow shower
    case 20: return 616;  // Sleet shower
    case 21: return 500;  // Overcast with light rain
    case 22: return 501;  // Overcast with rain
    case 23: return 600;  // Overcast with snow
    case 24: return 611;  // Overcast with sleet
    case 25: return 300;  // Overcast with light rain shower
    case 26: return 520;  // Overcast with rain shower
    case 27: return 615;  // Overcast with snow shower
    case 28: return 616;  // Overcast with sleet shower
    case 29: return 502;  // Heavy rain
    case 30: return 602;  // Heavy snow
    case 31: return 611;  // Heavy sleet
    case 32: return 521;  // Rain shower and thunderstorm
    case 33: return 621;  // Snow shower and thunderstorm
    case 34: return 611;  // Sleet shower and thunderstorm
    default: return 800;  // Default to clear
  }
}

/* Deserialize Meteoblue JSON response
 */
DeserializationError deserializeMeteoblue(WiFiClient &json, 
                                          mb_raw_response_t &raw)
{
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, json);
  
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] Meteoblue doc.overflowed() : "
                 + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif

  if (error) {
    return error;
  }

  // Parse metadata
  JsonObject metadata = doc["metadata"];
  raw.latitude = metadata["latitude"].as<float>();
  raw.longitude = metadata["longitude"].as<float>();
  raw.timezone_abbr = metadata["timezone_abbrevation"].as<const char *>();
  raw.utc_timeoffset = metadata["utc_timeoffset"].as<int>();

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

  // Iterate through time entries - limit to what we need to avoid memory issues
  // We need 48 hours for hourly display. Daily data will be aggregated from these.
  int total_hours = time_array.size();
  int max_hours_needed = 48; // Match OWM_NUM_HOURLY
  int num_hours = (total_hours > max_hours_needed) ? max_hours_needed : total_hours;
  
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

    raw.hourly_data.push_back(hour_data);
  }

  return error;
}

/* Convert Meteoblue raw response to OWM-compatible structures
 * This allows the existing rendering code to work unchanged
 */
void convertMBtoOWM(const mb_raw_response_t &mb_raw,
                    owm_resp_onecall_t &owm_resp,
                    owm_resp_air_pollution_t &owm_air)
{
  if (mb_raw.hourly_data.empty())
  {
    return;
  }

  // Set location data
  owm_resp.lat = mb_raw.latitude;
  owm_resp.lon = mb_raw.longitude;
  owm_resp.timezone = mb_raw.timezone_abbr;
  owm_resp.timezone_offset = mb_raw.utc_timeoffset * 3600; // Convert hours to seconds

  // Convert first hour to "current" data
  const mb_raw_hourly_t &first_hour = mb_raw.hourly_data[0];
  int64_t current_time = parseTimeString(first_hour.time);
  
  owm_resp.current.dt = current_time;
  owm_resp.current.temp = first_hour.temperature + 273.15f; // C to K
  owm_resp.current.feels_like = first_hour.felttemperature + 273.15f;
  owm_resp.current.pressure = static_cast<int>(first_hour.sealevelpressure);
  owm_resp.current.humidity = first_hour.relativehumidity;
  owm_resp.current.dew_point = 0.0f; // Not provided by MB
  owm_resp.current.clouds = 0; // Estimate from pictocode if needed
  owm_resp.current.uvi = first_hour.uvindex;
  owm_resp.current.visibility = 10000; // Default 10km (MB doesn't provide this in basic)
  owm_resp.current.wind_speed = first_hour.windspeed;
  owm_resp.current.wind_gust = 0.0f; // Not provided
  owm_resp.current.wind_deg = first_hour.winddirection;
  owm_resp.current.rain_1h = (first_hour.snowfraction < 0.5f) ? first_hour.precipitation : 0.0f;
  owm_resp.current.snow_1h = (first_hour.snowfraction >= 0.5f) ? first_hour.precipitation : 0.0f;
  
  // Map pictocode to OWM weather ID
  owm_resp.current.weather.id = pictocodeToOWMId(first_hour.pictocode);
  owm_resp.current.weather.main = "Unknown";
  owm_resp.current.weather.description = "Weather data from Meteoblue";
  owm_resp.current.weather.icon = first_hour.isdaylight ? "01d" : "01n";

  // Find sunrise/sunset from daylight transitions
  // Simple heuristic: find first daylight=1 for sunrise, last daylight=1 for sunset
  owm_resp.current.sunrise = current_time; // Default
  owm_resp.current.sunset = current_time + 12 * 3600; // Default
  
  for (size_t i = 0; i < mb_raw.hourly_data.size(); ++i)
  {
    if (mb_raw.hourly_data[i].isdaylight == 1)
    {
      owm_resp.current.sunrise = parseTimeString(mb_raw.hourly_data[i].time);
      break;
    }
  }
  
  for (int i = mb_raw.hourly_data.size() - 1; i >= 0; --i)
  {
    if (mb_raw.hourly_data[i].isdaylight == 1)
    {
      owm_resp.current.sunset = parseTimeString(mb_raw.hourly_data[i].time) + 3600;
      break;
    }
  }

  // Convert hourly data (up to OWM_NUM_HOURLY entries)
  for (int i = 0; i < OWM_NUM_HOURLY && i < static_cast<int>(mb_raw.hourly_data.size()); ++i)
  {
    const mb_raw_hourly_t &mb_hour = mb_raw.hourly_data[i];
    
    owm_resp.hourly[i].dt = parseTimeString(mb_hour.time);
    owm_resp.hourly[i].temp = mb_hour.temperature + 273.15f;
    owm_resp.hourly[i].feels_like = mb_hour.felttemperature + 273.15f;
    owm_resp.hourly[i].pressure = static_cast<int>(mb_hour.sealevelpressure);
    owm_resp.hourly[i].humidity = mb_hour.relativehumidity;
    owm_resp.hourly[i].dew_point = 0.0f;
    owm_resp.hourly[i].clouds = 0;
    owm_resp.hourly[i].uvi = mb_hour.uvindex;
    owm_resp.hourly[i].visibility = 10000;
    owm_resp.hourly[i].wind_speed = mb_hour.windspeed;
    owm_resp.hourly[i].wind_gust = 0.0f;
    owm_resp.hourly[i].wind_deg = mb_hour.winddirection;
    owm_resp.hourly[i].pop = mb_hour.precipitation_probability / 100.0f;
    owm_resp.hourly[i].rain_1h = (mb_hour.snowfraction < 0.5f) ? mb_hour.precipitation : 0.0f;
    owm_resp.hourly[i].snow_1h = (mb_hour.snowfraction >= 0.5f) ? mb_hour.precipitation : 0.0f;
    
    owm_resp.hourly[i].weather.id = pictocodeToOWMId(mb_hour.pictocode);
    owm_resp.hourly[i].weather.main = "Unknown";
    owm_resp.hourly[i].weather.description = "MB data";
    owm_resp.hourly[i].weather.icon = mb_hour.isdaylight ? "01d" : "01n";
  }

  // Note: Daily data aggregation now happens in client_utils.cpp during JSON parsing
  // to avoid memory issues with storing 192 hours. The owm_resp.daily[] array
  // is populated directly there from the JSON arrays. This function only handles
  // current and hourly data conversion from the stored 48 hours in mb_raw.

  // Initialize air pollution data with dummy values (MB basic doesn't provide this)
  owm_air.coord.lat = mb_raw.latitude;
  owm_air.coord.lon = mb_raw.longitude;
  
  for (int i = 0; i < OWM_NUM_AIR_POLLUTION; ++i)
  {
    owm_air.main_aqi[i] = 1; // Good quality (default)
    owm_air.dt[i] = current_time + (i * 3600);
    
    // Set all pollutant concentrations to 0 (not available)
    owm_air.components.co[i] = 0.0f;
    owm_air.components.no[i] = 0.0f;
    owm_air.components.no2[i] = 0.0f;
    owm_air.components.o3[i] = 0.0f;
    owm_air.components.so2[i] = 0.0f;
    owm_air.components.pm2_5[i] = 0.0f;
    owm_air.components.pm10[i] = 0.0f;
    owm_air.components.nh3[i] = 0.0f;
  }

  // No alerts in basic MB API
  owm_resp.alerts.clear();
}
