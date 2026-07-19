#pragma once

#include <stddef.h>

#include <string>
#include <vector>

#include "aircraft.h"

#ifdef ARDUINO
class Stream;
#endif

namespace skyprint {

bool parseAircraftJson(const char* json, size_t length, double observerLatitude,
                       double observerLongitude, double radiusNm,
                       std::vector<Aircraft>& aircraft, std::string& error);

#ifdef ARDUINO
bool parseAircraftJson(Stream& stream, double observerLatitude,
                       double observerLongitude, double radiusNm,
                       std::vector<Aircraft>& aircraft, std::string& error);
#endif

}  // namespace skyprint
