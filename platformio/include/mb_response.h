/* Meteoblue API response deserialization declarations for esp32-weather-epd.
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

#ifndef __MB_RESPONSE_H__
#define __MB_RESPONSE_H__

#include <cstdint>
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "api_response.h"

/*
 * Meteoblue raw response structure
 * This holds the raw data from the Meteoblue API which will be converted
 * to OWM-compatible structures for rendering
 */
typedef struct mb_raw_hourly
{
  String time;              // Time string "YYYY-MM-DD hh:mm"
  float temperature;        // Temperature in C
  float felttemperature;    // Felt temperature in C
  int relativehumidity;     // Relative humidity in %
  float windspeed;          // Wind speed in m/s
  int winddirection;        // Wind direction in degrees
  float precipitation;      // Precipitation in mm
  int precipitation_probability; // Precipitation probability in %
  float snowfraction;       // Snow fraction (0.0-1.0)
  int pictocode;            // Weather icon code
  float convective_precipitation; // Convective precipitation in mm
  int uvindex;              // UV index
  float sealevelpressure;   // Sea level pressure in hPa
  int isdaylight;           // Is daylight (0 or 1)
  String rainspot;          // Rainspot string (proprietary format)
} mb_raw_hourly_t;

typedef struct mb_raw_response
{
  float latitude;
  float longitude;
  String timezone_abbr;
  int utc_timeoffset;       // UTC offset in hours
  int64_t sunrise;          // Sunrise timestamp for today
  int64_t sunset;           // Sunset timestamp for today
  std::vector<mb_raw_hourly_t> hourly_data;
} mb_raw_response_t;

// Function declarations
DeserializationError deserializeMeteoblue(WiFiClient &json, 
                                          mb_raw_response_t &raw);

void convertMBtoOWM(const mb_raw_response_t &mb_raw,
                    owm_resp_onecall_t &owm_resp,
                    owm_resp_air_pollution_t &owm_air);

// Helper functions
int64_t parseTimeString(const String &timeStr);
int64_t parseTimeString(const char *time_str);
int pictocodeToOWMId(int pictocode);

#endif
