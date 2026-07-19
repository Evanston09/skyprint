#pragma once

#include <stdint.h>

#include <string>

namespace skyprint {

constexpr int16_t kAirlineLogoWidth = 16;
constexpr int16_t kAirlineLogoHeight = 16;

// Uses the first three callsign characters as the ICAO airline designator.
// Returns the generated default emblem when no matching logo is available.
const uint8_t* airlineLogoForCallsign(const std::string& callsign);

}  // namespace skyprint
