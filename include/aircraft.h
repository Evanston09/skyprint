#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace skyprint {

enum class HeadingSource {
  kUnknown,
  kTrueHeading,
  kMagneticHeading,
  kTrack,
};

enum class DistanceTrend {
  kUnknown,
  kClosing,
  kClosestPoint,
  kReceding,
};

struct Aircraft {
  std::string hex;
  std::string callsign;
  std::string registration;
  std::string typeCode;
  std::string description;
  std::string squawk;

  double latitude = 0.0;
  double longitude = 0.0;
  double altitudeFt = 0.0;
  double verticalRateFpm = 0.0;
  bool hasVerticalRate = false;
  double groundspeedKt = 0.0;
  bool hasGroundspeed = false;

  double headingDeg = 0.0;
  bool hasHeading = false;
  HeadingSource headingSource = HeadingSource::kUnknown;

  double positionAgeSeconds = 0.0;
  double distanceNm = 0.0;
  double bearingDeg = 0.0;
  DistanceTrend distanceTrend = DistanceTrend::kUnknown;
};

double normalizeDegrees(double degrees);
double distanceNm(double fromLatitude, double fromLongitude,
                  double toLatitude, double toLongitude);
double initialBearingDeg(double fromLatitude, double fromLongitude,
                         double toLatitude, double toLongitude);
double relativeDirectionDeg(double bearingDegrees, double displayHeadingDegrees);
const char* cardinalDirection(double bearingDegrees);

std::string formatAltitude(double feet);
std::string formatVerticalRate(const Aircraft& aircraft);
std::string formatGroundspeed(const Aircraft& aircraft);
std::string formatDistance(double nauticalMiles);
std::string formatBearing(double bearingDegrees);
std::string formatHeading(double headingDegrees);
const char* distanceTrendLabel(DistanceTrend trend);

enum class SelectionEvent {
  kNone,
  kSelected,
  kSwitched,
  kUpdated,
  kCleared,
};

class AircraftSelector {
 public:
  SelectionEvent ingestSuccessfulScan(const std::vector<Aircraft>& aircraft);

  bool hasCurrent() const { return hasCurrent_; }
  const Aircraft* current() const { return hasCurrent_ ? &current_ : nullptr; }
  uint8_t missingCount() const { return missingCount_; }

 private:
  const Aircraft* findClosest(const std::vector<Aircraft>& aircraft) const;
  const Aircraft* findByHex(const std::vector<Aircraft>& aircraft,
                            const std::string& hex) const;
  void select(const Aircraft& aircraft);
  void resetChallenger();

  bool hasCurrent_ = false;
  Aircraft current_;
  uint8_t missingCount_ = 0;
  std::string challengerHex_;
  uint8_t challengerCount_ = 0;
  uint8_t closestPointHoldPolls_ = 0;
};

}  // namespace skyprint
