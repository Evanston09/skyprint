#pragma once

#include <string>

namespace skyprint {

struct RouteInfo {
  std::string origin;
  std::string midpoint;
  std::string destination;
  std::string airlineName;
  std::string airlineCallsign;

  bool available() const { return !origin.empty() && !destination.empty(); }
  bool hasAirlineIdentity() const {
    return !airlineName.empty() || !airlineCallsign.empty();
  }
};

}  // namespace skyprint
