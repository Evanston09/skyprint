#include "route_selection.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "aircraft.h"

namespace skyprint {
namespace {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr double kHeadingPenaltyNmPerDegree = 0.5;
constexpr double kCandidateDistanceToleranceNm = 10.0;

struct LegCandidate {
  size_t index = 0;
  double distanceNm = std::numeric_limits<double>::infinity();
  double lengthNm = 0.0;
};

double angularDifference(double first, double second) {
  double difference =
      std::fabs(normalizeDegrees(first) - normalizeDegrees(second));
  return difference > 180.0 ? 360.0 - difference : difference;
}

double distanceToLegNm(double latitude, double longitude,
                       const PublishedAirport& origin,
                       const PublishedAirport& destination) {
  const double legLength =
      skyprint::distanceNm(origin.latitude, origin.longitude,
                           destination.latitude, destination.longitude);
  if (legLength < 0.01) {
    return skyprint::distanceNm(latitude, longitude, origin.latitude,
                               origin.longitude);
  }

  const double fromOrigin =
      skyprint::distanceNm(origin.latitude, origin.longitude, latitude,
                           longitude) /
      kEarthRadiusNm;
  const double routeBearing =
      initialBearingDeg(origin.latitude, origin.longitude,
                        destination.latitude, destination.longitude) *
      kDegreesToRadians;
  const double positionBearing =
      initialBearingDeg(origin.latitude, origin.longitude, latitude,
                        longitude) *
      kDegreesToRadians;
  const double crossTrackInput =
      std::sin(fromOrigin) * std::sin(positionBearing - routeBearing);
  const double crossTrack =
      std::asin(std::max(-1.0, std::min(1.0, crossTrackInput)));
  const double alongTrack = std::atan2(
      std::sin(fromOrigin) * std::cos(positionBearing - routeBearing),
      std::cos(fromOrigin));
  const double legAngle = legLength / kEarthRadiusNm;

  if (alongTrack < 0.0) {
    return skyprint::distanceNm(latitude, longitude, origin.latitude,
                               origin.longitude);
  }
  if (alongTrack > legAngle) {
    return skyprint::distanceNm(latitude, longitude, destination.latitude,
                               destination.longitude);
  }
  return std::fabs(crossTrack) * kEarthRadiusNm;
}

}  // namespace

bool selectPublishedRouteLeg(const std::vector<PublishedAirport>& airports,
                             double latitude, double longitude,
                             bool hasHeading, double headingDegrees,
                             RouteInfo& route) {
  route.origin.clear();
  route.midpoint.clear();
  route.destination.clear();
  if (airports.size() < 2) return false;

  std::vector<LegCandidate> candidates;
  candidates.reserve(airports.size() - 1);
  double closestDistance = std::numeric_limits<double>::infinity();
  for (size_t index = 0; index + 1 < airports.size(); ++index) {
    const PublishedAirport& origin = airports[index];
    const PublishedAirport& destination = airports[index + 1];
    if (origin.code.empty() || destination.code.empty() ||
        origin.code == destination.code) {
      continue;
    }

    LegCandidate candidate;
    candidate.index = index;
    candidate.lengthNm =
        skyprint::distanceNm(origin.latitude, origin.longitude,
                             destination.latitude, destination.longitude);
    candidate.distanceNm =
        distanceToLegNm(latitude, longitude, origin, destination);
    closestDistance = std::min(closestDistance, candidate.distanceNm);
    candidates.push_back(candidate);
  }
  if (candidates.empty()) return false;

  const LegCandidate* selected = nullptr;
  double bestScore = std::numeric_limits<double>::infinity();
  for (const LegCandidate& candidate : candidates) {
    if (candidate.distanceNm >
        closestDistance + kCandidateDistanceToleranceNm) {
      continue;
    }

    double score = candidate.distanceNm;
    if (hasHeading) {
      const PublishedAirport& destination = airports[candidate.index + 1];
      const double bearingToDestination =
          initialBearingDeg(latitude, longitude, destination.latitude,
                            destination.longitude);
      score += angularDifference(headingDegrees, bearingToDestination) *
               kHeadingPenaltyNmPerDegree;
    }
    if (score < bestScore) {
      selected = &candidate;
      bestScore = score;
    }
  }
  if (selected == nullptr) return false;

  // Independently validate the response instead of relying solely on the
  // service's callsign-only plausibility cache. Use ADSB.lol's own
  // tolerance, but measure distance to the bounded leg rather than its
  // infinite great-circle extension.
  const double plausibilityLimit = std::max(50.0, selected->lengthNm * 0.20);
  if (selected->distanceNm > plausibilityLimit) return false;

  route.origin = airports[selected->index].code;
  route.destination = airports[selected->index + 1].code;
  return route.available();
}

}  // namespace skyprint
