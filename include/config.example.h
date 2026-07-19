#pragma once

// Copy this file to include/config.h, fill in the values, and set
// SKYPRINT_CONFIGURED to 1. include/config.h is intentionally ignored by git.
#define SKYPRINT_CONFIGURED 0

#define SKYPRINT_WIFI_SSID "your-2.4-ghz-network"
#define SKYPRINT_WIFI_PASSWORD "your-password"

// Decimal WGS84 coordinates for the point aircraft are measured from.
#define SKYPRINT_OFFICE_LATITUDE 40.000000
#define SKYPRINT_OFFICE_LONGITUDE -75.000000

// Compass direction represented by an UP arrow on the display. Set this to
// the direction you face while looking straight ahead over the display:
// north=0, east=90, south=180, west=270.
#define SKYPRINT_DISPLAY_HEADING_DEGREES 0

// Set to 1 for a black background with white text and graphics. Keep at 0 for
// the standard white background with black text.
#define SKYPRINT_INVERT_DISPLAY 0
