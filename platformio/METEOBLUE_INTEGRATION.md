# Meteoblue API Integration

## Overview
This integration adds support for the Meteoblue weather API as an alternative to OpenWeatherMap. The implementation maintains compatibility with existing rendering code by converting Meteoblue data structures to OpenWeatherMap-compatible formats.

## Files Added/Modified

### New Files Created:
1. **include/mb_response.h** - Meteoblue API response structures and function declarations
2. **src/mb_response.cpp** - JSON deserialization and data conversion logic

### Modified Files:
1. **include/config.h** - Added USE_OWM_API/USE_MB_API macros and MB_API_URL extern declaration
2. **src/config.cpp** - Added MB_API_URL constant definition
3. **include/client_utils.h** - Added getMBweather() function declaration
4. **src/client_utils.cpp** - Implemented getMBweather() function and added mb_response.h include
5. **src/main.cpp** - Added conditional compilation blocks for API selection

## Configuration

### Step 1: Choose Your API
In **include/config.h**, uncomment exactly ONE of these lines:

```cpp
#define USE_OWM_API   // Use OpenWeatherMap API
// #define USE_MB_API  // Use Meteoblue API
```

### Step 2: Configure API Credentials

#### For OpenWeatherMap (existing):
In **src/config.cpp**, set your OWM API key:
```cpp
const String OWM_APIKEY = "your_openweathermap_api_key";
```

#### For Meteoblue (new):
In **src/config.cpp**, set your complete Meteoblue API URL:
```cpp
const String MB_API_URL = "https://my.meteoblue.com/packages/basic-1h_clouds-1h?apikey=YOUR_API_KEY&lat=48.5&lon=9.0&format=json";
```

**Important:** The MB_API_URL must include:
- Full URL with protocol (https:// or http://)
- Your Meteoblue API key embedded in the URL
- Latitude and longitude parameters
- `format=json` parameter
- The package endpoint (e.g., `basic-1h_clouds-1h`)

### Step 3: Configure API Call Frequency (Optional)

To reduce API costs, you can control how often fresh data is fetched:

In **src/config.cpp**:
```cpp
const int SLEEP_DURATION = 30; // Display updates every 30 minutes
const int MB_API_CALL_SLEEP_DURATION = 120; // API called every 2 hours
```

With these settings:
- ESP32 wakes every 30 minutes to update sensors and display
- Fresh Meteoblue API data is fetched only every 2 hours
- Between API calls, cached data from NVS is used
- Reduces API calls by 75% while maintaining frequent display updates

**Note**: `MB_API_CALL_SLEEP_DURATION` must be ≥ `SLEEP_DURATION`. Set both to the same value if you want fresh data every wake.

### Step 4: Build and Flash
```bash
pio run -t upload
```

## How It Works

### Data Flow:
1. **Wake & Check Cache** - ESP32 wakes, checks time since last API call
2. **API Call (if needed)** - `getMBweather()` fetches JSON if cache expired or first run
3. **Parsing** - In DEBUG mode (DEBUG_LEVEL>=2), JSON arrays are accessed directly; otherwise `deserializeMeteoblue()` parses into `mb_raw_response_t`
4. **Conversion** - `convertMBtoOWM()` transforms MB data to OWM-compatible structures (current + hourly)
5. **Daily Aggregation** - Up to 192 hours processed directly from JSON arrays, aggregating per calendar day (midnight to midnight)
6. **Caching** - Weather data cached in NVS for use between API calls
7. **Rendering** - Existing display code renders using OWM-compatible data
8. **Deep Sleep** - ESP32 sleeps until next `SLEEP_DURATION` interval

### Key Components:

#### mb_response.h/cpp
- **mb_raw_hourly_t**: Raw hourly data from Meteoblue (temperature, wind, precipitation, etc.)
- **mb_raw_response_t**: Container for all Meteoblue response data
- **deserializeMeteoblue()**: Parses Meteoblue JSON response
- **convertMBtoOWM()**: Converts MB structures to OWM structures
- **pictocodeToOWMId()**: Maps Meteoblue weather pictocodes to OWM weather IDs

#### client_utils.cpp
- **getMBweather()**: Performs HTTP(S) request to Meteoblue API, deserializes and converts data

## Meteoblue Data Mapping

### Temperature
- MB provides temperature in Celsius, converted to Kelvin for OWM compatibility
- Felt temperature mapped to `feels_like`

### Hourly Data
- Direct 1:1 mapping for up to 48 hours
- Precipitation probability converted from percent (0-100) to ratio (0.0-1.0)
- Snow/rain determined by `snowfraction` field

### Daily Data
- Aggregated from up to 192 hours (8 days) directly from JSON arrays
- Only 48 hours stored in memory for hourly display, daily aggregation processed on-the-fly
- Min/max temperatures calculated across each 24-hour period
- Total precipitation summed for the day
- **Weather icon selection**: Most frequent pictocode during daytime hours (6am-6pm) is used to represent the entire day, providing a more accurate representation than a single snapshot
- Days align with calendar days (midnight to midnight) since Meteoblue API returns data starting at 00:00

### Air Quality
- Not available in Meteoblue basic API
- Populated with default values (AQI = 1, all pollutants = 0)

### Weather Alerts
- Not available in Meteoblue basic API
- Alert vector remains empty

### Sunrise/Sunset
- Estimated from `isdaylight` field transitions
- First `isdaylight=1` → sunrise
- Last `isdaylight=1` → sunset

## Meteoblue Pictocode Mapping

The integration maps Meteoblue pictocodes (1-34) to OpenWeatherMap weather IDs with a conservative approach to rain:
- 1-8: Clear to overcast conditions (no precipitation)
- 9: Overcast with possible rain (mapped to overcast, not rain)
- 10-15: Actual rain and snow
- 16-20: Sleet and showers
- 21: Light rain with clouds (mapped to mostly cloudy)
- 22-24: Rain/snow/sleet with clouds
- 25: Light rain shower with clouds (mapped to mostly cloudy)
- 26-28: Rain/snow/sleet showers with clouds
- 29-34: Heavy precipitation and thunderstorms

**Strategy**: Only pictocodes indicating actual precipitation (10+) show rain/snow icons. Codes with "slight" or "possible" rain show cloud icons to avoid over-predicting precipitation.

See `pictocodeToOWMId()` in mb_response.cpp for complete mapping.

## Limitations & Notes

1. **Air Quality Data**: Meteoblue basic API doesn't provide air pollution data. The air quality section will show default/empty values.

2. **Weather Alerts**: Not available in basic Meteoblue API. Alert display will be empty.

3. **Time Zone**: Meteoblue provides UTC offset in hours. The code converts this to seconds for OWM compatibility.

4. **Data Accuracy**: Some fields not directly provided by Meteoblue (dew point, cloud coverage percentage) are set to default values or estimated.

5. **API URL Format**: The complete API URL including credentials must be stored in MB_API_URL. This differs from OWM which separates endpoint and API key.

6. **Memory Optimization**: To avoid ESP32 heap overflow, only 48 hours of data are stored in memory. Daily forecast aggregation (up to 8 days) is performed directly on JSON arrays during parsing without intermediate storage. This prevents the ~169 hours from Meteoblue API from exhausting available RAM.

7. **Data Caching**: Weather data is cached in NVS (non-volatile storage) between API calls to reduce costs. The cache survives deep sleep and allows the display to update more frequently than API calls are made. Cache is automatically managed with old data removed before new data is stored.

8. **Time Alignment**: Meteoblue API returns hourly data starting at midnight (00:00), so daily aggregations align perfectly with calendar days. Each day represents a full 24-hour period from midnight to midnight.

## Testing Recommendations

1. **Serial Monitor**: Set `DEBUG_LEVEL` in config.h to 1 or 2 to view API responses and parsing details
2. **Verify Display**: Check that temperature, wind, precipitation display correctly
3. **Time Alignment**: Confirm hourly forecast times align with local timezone
4. **Daily Aggregation**: Verify daily min/max temperatures are reasonable

## Troubleshooting

### "Meteoblue API" Error on Display
- Check MB_API_URL is correct and includes your API key
- Verify internet connection and API subscription is active
- Check serial output for HTTP response codes

### Incorrect Temperature/Units
- MB data arrives in Celsius and m/s (metric)
- Conversion to Kelvin is automatic
- Unit display controlled by UNITS_* macros in config.h

### Missing Data
- Increase `HTTP_CLIENT_TCP_TIMEOUT` in config.cpp if seeing timeout errors
- Some fields (air quality, alerts) are expected to be empty with MB basic API

### NVS Storage Errors
- If you see "NOT_ENOUGH_SPACE" errors, the NVS partition may be full
- The code automatically clears old cached data before writing new data
- If persistent, consider increasing `SLEEP_DURATION` to reduce write frequency
- Check serial output for actual bytes written/read from cache

### Weather Icons Don't Match Meteoblue Website
- Ensure you're comparing the same day (cache may show older data)
- Check serial monitor for API call timestamps
- Daily icons represent the most common daytime condition (6am-6pm), not a single hour
- Force fresh API call by setting `MB_API_CALL_SLEEP_DURATION` = `SLEEP_DURATION`

## Future Enhancements

Possible improvements:
- Support for Meteoblue advanced packages with air quality data
- Better daily weather icon selection from hourly data
- Cloud coverage estimation from pictocode
- More accurate sunrise/sunset from Meteoblue astronomy package
- Support for Meteoblue alert data if package includes it

## Compile-Time Validation

The code includes validation to ensure exactly one API is selected:
```cpp
#if !(  defined(USE_OWM_API)    \
      ^ defined(USE_MB_API))
  #error Invalid configuration. Exactly one API must be selected.
#endif
```

This prevents accidental dual-API compilation or missing API selection.
