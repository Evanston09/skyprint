#pragma once

#include <string>
#include <vector>

#include "route.h"

namespace skyprint {

struct PublishedAirport {
  PublishedAirport() = default;
  PublishedAirport(const std::string& airportCode, double airportLatitude,
                   double airportLongitude)
      : code(airportCode),
        latitude(airportLatitude),
        longitude(airportLongitude) {}

  std::string code;
  double latitude = 0.0;
  double longitude = 0.0;
};

// Selects the currently flown leg from an ADSB.lol itinerary. Standing-data
// routes can contain round trips such as CLT-LGA-CLT, so treating the first and
// last airports as a single flight would incorrectly display CLT>CLT.
bool selectPublishedRouteLeg(const std::vector<PublishedAirport>& airports,
                             double latitude, double longitude,
                             bool hasHeading, double headingDegrees,
                             RouteInfo& route);

}  // namespace skyprint
