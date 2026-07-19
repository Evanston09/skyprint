#include "aircraft_json.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "settings.h"

namespace skyprint {
namespace {

std::string trim(const char* value) {
  if (value == nullptr) return {};
  std::string result(value);
  const auto first = std::find_if_not(
      result.begin(), result.end(),
      [](unsigned char character) { return std::isspace(character); });
  const auto last = std::find_if_not(
      result.rbegin(), result.rend(),
      [](unsigned char character) { return std::isspace(character); })
                        .base();
  if (first >= last) return {};
  return std::string(first, last);
}

std::string uppercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

bool numericValue(JsonVariantConst value, double& output) {
  if (value.isNull() || value.is<const char*>() || value.is<bool>()) {
    return false;
  }
  output = value.as<double>();
  return std::isfinite(output);
}

JsonDocument makeFilter() {
  JsonDocument filter;
  const char* fields[] = {"hex",          "flight",       "r",
                          "t",            "desc",         "squawk",
                          "lat",          "lon",          "alt_baro",
                          "alt_geom",     "baro_rate",    "geom_rate",
                          "gs",           "true_heading", "mag_heading",
                          "track",        "seen_pos",     "seen"};
  for (const char* field : fields) filter["ac"][0][field] = true;
  return filter;
}

bool parseDocument(JsonDocument& document, double observerLatitude,
                   double observerLongitude, double radiusNm,
                   std::vector<Aircraft>& aircraft, std::string& error) {
  aircraft.clear();
  JsonArrayConst array = document["ac"].as<JsonArrayConst>();
  if (array.isNull()) {
    error = "response did not contain an aircraft array";
    return false;
  }

  for (JsonObjectConst item : array) {
    Aircraft parsed;
    parsed.hex = uppercase(trim(item["hex"] | ""));
    if (parsed.hex.empty()) continue;

    double latitude = 0.0;
    double longitude = 0.0;
    if (!numericValue(item["lat"], latitude) ||
        !numericValue(item["lon"], longitude) || latitude < -90.0 ||
        latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
      continue;
    }

    double age = 0.0;
    if (!numericValue(item["seen_pos"], age) &&
        !numericValue(item["seen"], age)) {
      continue;
    }
    if (age < 0.0 || age > settings::kMaximumPositionAgeSeconds) continue;

    if (item["alt_baro"].is<const char*>()) {
      const char* barometricAltitudeText = item["alt_baro"].as<const char*>();
      if (uppercase(trim(barometricAltitudeText)) == "GROUND") continue;
    }

    double altitude = 0.0;
    if (!numericValue(item["alt_baro"], altitude) &&
        !numericValue(item["alt_geom"], altitude)) {
      continue;
    }

    parsed.latitude = latitude;
    parsed.longitude = longitude;
    parsed.altitudeFt = altitude;
    if (numericValue(item["baro_rate"], parsed.verticalRateFpm) ||
        numericValue(item["geom_rate"], parsed.verticalRateFpm)) {
      parsed.hasVerticalRate = true;
    }
    parsed.positionAgeSeconds = age;
    parsed.distanceNm = distanceNm(observerLatitude, observerLongitude,
                                   latitude, longitude);
    if (parsed.distanceNm > radiusNm) continue;
    parsed.bearingDeg = initialBearingDeg(observerLatitude, observerLongitude,
                                         latitude, longitude);

    parsed.callsign = uppercase(trim(item["flight"] | ""));
    if (parsed.callsign.empty()) parsed.callsign = "NO CALLSIGN";
    parsed.registration = uppercase(trim(item["r"] | ""));
    if (parsed.registration.empty()) parsed.registration = parsed.hex;
    parsed.typeCode = uppercase(trim(item["t"] | ""));
    parsed.description = uppercase(trim(item["desc"] | ""));
    parsed.squawk = uppercase(trim(item["squawk"] | ""));
    if (parsed.typeCode.empty()) parsed.typeCode = "TYPE UNKNOWN";
    if (parsed.description.empty()) parsed.description = "TYPE UNKNOWN";
    if (parsed.squawk.empty()) parsed.squawk = "----";

    parsed.hasGroundspeed = numericValue(item["gs"], parsed.groundspeedKt);

    if (numericValue(item["true_heading"], parsed.headingDeg)) {
      parsed.hasHeading = true;
      parsed.headingSource = HeadingSource::kTrueHeading;
    } else if (numericValue(item["mag_heading"], parsed.headingDeg)) {
      parsed.hasHeading = true;
      parsed.headingSource = HeadingSource::kMagneticHeading;
    } else if (numericValue(item["track"], parsed.headingDeg)) {
      parsed.hasHeading = true;
      parsed.headingSource = HeadingSource::kTrack;
    }
    if (parsed.hasHeading) {
      parsed.headingDeg = normalizeDegrees(parsed.headingDeg);
    }

    aircraft.push_back(parsed);
  }

  error.clear();
  return true;
}

template <typename Input>
bool parseInput(Input& input, double observerLatitude, double observerLongitude,
                double radiusNm, std::vector<Aircraft>& aircraft,
                std::string& error) {
  JsonDocument filter = makeFilter();
  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(
      document, input, DeserializationOption::Filter(filter));
  if (jsonError) {
    aircraft.clear();
    error = std::string("JSON parse failed: ") + jsonError.c_str();
    return false;
  }
  return parseDocument(document, observerLatitude, observerLongitude, radiusNm,
                       aircraft, error);
}

}  // namespace

bool parseAircraftJson(const char* json, size_t length, double observerLatitude,
                       double observerLongitude, double radiusNm,
                       std::vector<Aircraft>& aircraft, std::string& error) {
  JsonDocument filter = makeFilter();
  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(
      document, json, length, DeserializationOption::Filter(filter));
  if (jsonError) {
    aircraft.clear();
    error = std::string("JSON parse failed: ") + jsonError.c_str();
    return false;
  }
  return parseDocument(document, observerLatitude, observerLongitude, radiusNm,
                       aircraft, error);
}

#ifdef ARDUINO
bool parseAircraftJson(Stream& stream, double observerLatitude,
                       double observerLongitude, double radiusNm,
                       std::vector<Aircraft>& aircraft, std::string& error) {
  return parseInput(stream, observerLatitude, observerLongitude, radiusNm,
                    aircraft, error);
}
#endif

}  // namespace skyprint
