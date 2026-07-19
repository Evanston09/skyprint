#pragma once

#include <string>
#include <vector>

#include "aircraft.h"

namespace skyprint {

enum class FetchFailure {
  kNone,
  kClock,
  kDns,
  kConnection,
  kHttp,
  kJson,
};

struct FetchResult {
  bool success = false;
  int httpStatus = 0;
  FetchFailure failure = FetchFailure::kNone;
  std::string error;
  std::vector<Aircraft> aircraft;
};

class AdsbLolClient {
 public:
  FetchResult fetchNearby(double latitude, double longitude,
                          double radiusNm) const;
};

}  // namespace skyprint
