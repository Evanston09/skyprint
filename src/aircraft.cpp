#include "aircraft.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "settings.h"

namespace skyprint {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;
constexpr double kLevelVerticalRateThresholdFpm = 200.0;

double toRadians(double degrees) { return degrees * kPi / 180.0; }

std::string integerWithCommas(long value) {
  const bool negative = value < 0;
  unsigned long magnitude =
      negative ? static_cast<unsigned long>(-(value + 1)) + 1UL
               : static_cast<unsigned long>(value);
  std::string digits = std::to_string(magnitude);
  for (int index = static_cast<int>(digits.size()) - 3; index > 0;
       index -= 3) {
    digits.insert(static_cast<size_t>(index), ",");
  }
  return negative ? "-" + digits : digits;
}

}  // namespace

double normalizeDegrees(double degrees) {
  double normalized = std::fmod(degrees, 360.0);
  if (normalized < 0.0) normalized += 360.0;
  return normalized;
}

double distanceNm(double fromLatitude, double fromLongitude,
                  double toLatitude, double toLongitude) {
  const double lat1 = toRadians(fromLatitude);
  const double lat2 = toRadians(toLatitude);
  const double deltaLat = toRadians(toLatitude - fromLatitude);
  const double deltaLon = toRadians(toLongitude - fromLongitude);
  const double sinLat = std::sin(deltaLat / 2.0);
  const double sinLon = std::sin(deltaLon / 2.0);
  const double a = sinLat * sinLat +
                   std::cos(lat1) * std::cos(lat2) * sinLon * sinLon;
  const double clamped = std::min(1.0, std::max(0.0, a));
  return kEarthRadiusNm * 2.0 * std::atan2(std::sqrt(clamped),
                                          std::sqrt(1.0 - clamped));
}

double initialBearingDeg(double fromLatitude, double fromLongitude,
                         double toLatitude, double toLongitude) {
  const double lat1 = toRadians(fromLatitude);
  const double lat2 = toRadians(toLatitude);
  const double deltaLon = toRadians(toLongitude - fromLongitude);
  const double y = std::sin(deltaLon) * std::cos(lat2);
  const double x = std::cos(lat1) * std::sin(lat2) -
                   std::sin(lat1) * std::cos(lat2) * std::cos(deltaLon);
  return normalizeDegrees(std::atan2(y, x) * 180.0 / kPi);
}

double relativeDirectionDeg(double bearingDegrees,
                            double displayHeadingDegrees) {
  return normalizeDegrees(bearingDegrees - displayHeadingDegrees);
}

const char* cardinalDirection(double bearingDegrees) {
  static const char* kDirections[] = {"N",   "NNE", "NE",  "ENE",
                                      "E",   "ESE", "SE",  "SSE",
                                      "S",   "SSW", "SW",  "WSW",
                                      "W",   "WNW", "NW",  "NNW"};
  const int index = static_cast<int>((normalizeDegrees(bearingDegrees) + 11.25) /
                                     22.5) %
                    16;
  return kDirections[index];
}

std::string formatAltitude(double feet) {
  if (!std::isfinite(feet)) return "-- FT";
  return integerWithCommas(std::lround(feet)) + " FT";
}

std::string formatVerticalRate(const Aircraft& aircraft) {
  if (!aircraft.hasVerticalRate ||
      !std::isfinite(aircraft.verticalRateFpm)) {
    return "--";
  }
  if (std::abs(aircraft.verticalRateFpm) <
      kLevelVerticalRateThresholdFpm) {
    return "0";
  }
  const long roundedHundreds =
      std::lround(std::abs(aircraft.verticalRateFpm) / 100.0) * 100L;
  return std::string(aircraft.verticalRateFpm > 0.0 ? "+" : "-") +
         integerWithCommas(roundedHundreds);
}

std::string formatGroundspeed(const Aircraft& aircraft) {
  if (!aircraft.hasGroundspeed || !std::isfinite(aircraft.groundspeedKt)) {
    return "-- KT";
  }
  return std::to_string(std::lround(aircraft.groundspeedKt)) + " KT";
}

std::string formatDistance(double nauticalMiles) {
  if (!std::isfinite(nauticalMiles)) return "-- NM";
  char buffer[20];
  std::snprintf(buffer, sizeof(buffer), "%.1f NM", nauticalMiles);
  return buffer;
}

std::string formatBearing(double bearingDegrees) {
  const int degrees =
      static_cast<int>(std::lround(normalizeDegrees(bearingDegrees))) % 360;
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%03d %s", degrees,
                cardinalDirection(bearingDegrees));
  return buffer;
}

std::string formatHeading(double headingDegrees) {
  const int degrees =
      static_cast<int>(std::lround(normalizeDegrees(headingDegrees))) % 360;
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%03d", degrees);
  return buffer;
}

const char* distanceTrendLabel(DistanceTrend trend) {
  switch (trend) {
    case DistanceTrend::kClosing:
      return "CLOSING";
    case DistanceTrend::kClosestPoint:
      return "CLOSEST";
    case DistanceTrend::kReceding:
      return "RECEDING";
    case DistanceTrend::kUnknown:
    default:
      return "TRACKING";
  }
}

const Aircraft* AircraftSelector::findClosest(
    const std::vector<Aircraft>& aircraft) const {
  if (aircraft.empty()) return nullptr;
  return &*std::min_element(
      aircraft.begin(), aircraft.end(),
      [](const Aircraft& left, const Aircraft& right) {
        return left.distanceNm < right.distanceNm;
      });
}

const Aircraft* AircraftSelector::findByHex(
    const std::vector<Aircraft>& aircraft, const std::string& hex) const {
  for (const Aircraft& item : aircraft) {
    if (item.hex == hex) return &item;
  }
  return nullptr;
}

void AircraftSelector::select(const Aircraft& aircraft) {
  current_ = aircraft;
  current_.distanceTrend = DistanceTrend::kUnknown;
  hasCurrent_ = true;
  missingCount_ = 0;
  closestPointHoldPolls_ = 0;
  resetChallenger();
}

void AircraftSelector::resetChallenger() {
  challengerHex_.clear();
  challengerCount_ = 0;
}

SelectionEvent AircraftSelector::ingestSuccessfulScan(
    const std::vector<Aircraft>& aircraft) {
  const Aircraft* closest = findClosest(aircraft);

  if (!hasCurrent_) {
    if (closest == nullptr) return SelectionEvent::kCleared;
    select(*closest);
    return SelectionEvent::kSelected;
  }

  const Aircraft* currentUpdate = findByHex(aircraft, current_.hex);
  if (currentUpdate == nullptr) {
    resetChallenger();
    ++missingCount_;
    if (missingCount_ < settings::kMissingConfirmations) {
      return SelectionEvent::kNone;
    }

    if (closest == nullptr) {
      hasCurrent_ = false;
      missingCount_ = 0;
      return SelectionEvent::kCleared;
    }

    select(*closest);
    return SelectionEvent::kSwitched;
  }

  const double distanceChangeNm =
      currentUpdate->distanceNm - current_.distanceNm;
  DistanceTrend observedTrend = DistanceTrend::kUnknown;
  if (distanceChangeNm <= -settings::kDistanceTrendDeadbandNm) {
    observedTrend = DistanceTrend::kClosing;
  } else if (distanceChangeNm >= settings::kDistanceTrendDeadbandNm) {
    observedTrend = DistanceTrend::kReceding;
  }

  DistanceTrend nextTrend = current_.distanceTrend;
  if (closestPointHoldPolls_ > 0) {
    --closestPointHoldPolls_;
    nextTrend = DistanceTrend::kClosestPoint;
  } else if (current_.distanceTrend == DistanceTrend::kClosing &&
             observedTrend == DistanceTrend::kReceding) {
    nextTrend = DistanceTrend::kClosestPoint;
    closestPointHoldPolls_ = settings::kClosestPointHoldPolls;
  } else if (current_.distanceTrend == DistanceTrend::kClosestPoint) {
    nextTrend = observedTrend == DistanceTrend::kClosing
                    ? DistanceTrend::kClosing
                    : DistanceTrend::kReceding;
  } else if (observedTrend != DistanceTrend::kUnknown) {
    nextTrend = observedTrend;
  }

  current_ = *currentUpdate;
  current_.distanceTrend = nextTrend;
  missingCount_ = 0;
  if (closest == nullptr || closest->hex == current_.hex) {
    resetChallenger();
    return SelectionEvent::kUpdated;
  }

  const double advantage = current_.distanceNm - closest->distanceNm;
  const bool meaningfullyCloser =
      closest->distanceNm <=
          current_.distanceNm * settings::kChallengerDistanceRatio &&
      advantage >= settings::kMinimumSwitchAdvantageNm;
  if (!meaningfullyCloser) {
    resetChallenger();
    return SelectionEvent::kUpdated;
  }

  if (challengerHex_ == closest->hex) {
    ++challengerCount_;
  } else {
    challengerHex_ = closest->hex;
    challengerCount_ = 1;
  }

  if (challengerCount_ >= settings::kChallengerConfirmations) {
    select(*closest);
    return SelectionEvent::kSwitched;
  }
  return SelectionEvent::kUpdated;
}

}  // namespace skyprint
