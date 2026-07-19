#pragma once

#include <string>

#include "route.h"

namespace skyprint {

struct RouteFetchResult {
  // True when both route plausibility and airline identity have definitive
  // results. False means at least one temporary failure may be retried; any
  // successfully fetched subset remains available in route.
  bool completed = false;
  RouteInfo route;
  std::string error;
};

struct AircraftNameFetchResult {
  // True for both a found name and a definitive "aircraft unavailable" reply.
  // False means a temporary network/server/parser failure may be retried.
  bool completed = false;
  std::string fullName;
  std::string error;
};

class AviationMetadataClient {
 public:
  RouteFetchResult fetchRoute(const std::string& callsign, double latitude,
                              double longitude) const;
  AircraftNameFetchResult fetchAircraftName(const std::string& hex) const;

 private:
  RouteFetchResult fetchAdsbLolRoute(const std::string& callsign,
                                     double latitude, double longitude) const;
  RouteFetchResult fetchAdsbDbAirlineIdentity(
      const std::string& callsign) const;
};

}  // namespace skyprint
