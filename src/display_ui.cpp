#include "display_ui.h"

#include <Arduino.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <SPI.h>

#include <cmath>
#include <cstring>

#include "airline_logos.h"
#include "config.h"
#include "settings.h"

namespace skyprint {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint16_t kForegroundColor =
    SKYPRINT_INVERT_DISPLAY ? GxEPD_WHITE : GxEPD_BLACK;
constexpr uint16_t kBackgroundColor =
    SKYPRINT_INVERT_DISPLAY ? GxEPD_BLACK : GxEPD_WHITE;

struct Point {
  int16_t x;
  int16_t y;
};

Point rotatePoint(int16_t centerX, int16_t centerY, double headingDegrees,
                  double localX, double localY) {
  const double radians = headingDegrees * kPi / 180.0;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  return {static_cast<int16_t>(std::lround(centerX + localX * cosine -
                                          localY * sine)),
          static_cast<int16_t>(std::lround(centerY + localX * sine +
                                          localY * cosine))};
}

std::string airlineIdentity(const RouteInfo& route) {
  if (route.airlineCallsign.empty()) {
    return route.airlineName.empty() ? "NEAREST AIRBORNE" : route.airlineName;
  }
  if (route.airlineName.empty()) return route.airlineCallsign;

  // Avoid captions such as "AMERICAN / AMERICAN AIRLINES", while retaining
  // distinctive radio names such as "BLUE STREAK / PSA AIRLINES".
  if (route.airlineName.rfind(route.airlineCallsign, 0) == 0 ||
      route.airlineCallsign.rfind(route.airlineName, 0) == 0) {
    return route.airlineName.size() >= route.airlineCallsign.size()
               ? route.airlineName
               : route.airlineCallsign;
  }
  return route.airlineCallsign + " / " + route.airlineName;
}

}  // namespace

DisplayUi::DisplayUi()
    : display_(GxEPD2_213_B74(settings::kChipSelectPin,
                              settings::kDataCommandPin, settings::kResetPin,
                              settings::kBusyPin)) {}

void DisplayUi::begin() {
  SPI.begin(settings::kClockPin, -1, settings::kMosiPin,
            settings::kChipSelectPin);
  display_.init(115200, true, 2, false);
  display_.setRotation(3);
  display_.setTextColor(kForegroundColor);
  display_.setTextWrap(false);
}

bool DisplayUi::chooseFullRefresh(bool requestedFull) const {
  return requestedFull || partialRefreshCount_ >= 5;
}

void DisplayUi::finishRefresh(bool fullRefresh) {
  if (fullRefresh) {
    partialRefreshCount_ = 0;
    lastFullRefreshAt_ = millis();
  } else {
    ++partialRefreshCount_;
  }
  display_.hibernate();
}

bool DisplayUi::needsDailyFullRefresh(uint32_t now) const {
  return static_cast<uint32_t>(now - lastFullRefreshAt_) >=
         settings::kDailyFullRefreshMs;
}

void DisplayUi::showScanning(bool forceFull) {
  const bool full = chooseFullRefresh(forceFull);
  full ? display_.setFullWindow()
       : display_.setPartialWindow(0, 0, settings::kVisibleWidth,
                                   settings::kVisibleHeight);
  display_.firstPage();
  do {
    drawStatusScreen("SCANNING", "WAITING FOR LIVE TRAFFIC");
  } while (display_.nextPage());
  screenKind_ = ScreenKind::kScanning;
  finishRefresh(full);
}

void DisplayUi::showClearSky(bool forceFull) {
  const bool full = chooseFullRefresh(forceFull);
  full ? display_.setFullWindow()
       : display_.setPartialWindow(0, 0, settings::kVisibleWidth,
                                   settings::kVisibleHeight);
  display_.firstPage();
  do {
    drawStatusScreen("CLEAR SKY", "NO AIRCRAFT WITHIN 25 NM");
  } while (display_.nextPage());
  screenKind_ = ScreenKind::kClearSky;
  finishRefresh(full);
}

void DisplayUi::showAircraft(const Aircraft& aircraft, const RouteInfo& route,
                             double displayHeadingDegrees, time_t updatedAtUtc,
                             bool forceFull) {
  const bool full = chooseFullRefresh(forceFull);
  full ? display_.setFullWindow()
       : display_.setPartialWindow(0, 0, settings::kVisibleWidth,
                                   settings::kVisibleHeight);
  display_.firstPage();
  do {
    drawAircraftPlaque(aircraft, route, displayHeadingDegrees, updatedAtUtc);
  } while (display_.nextPage());
  screenKind_ = ScreenKind::kAircraft;
  finishRefresh(full);
}

void DisplayUi::drawStatusScreen(const char* title, const char* subtitle) {
  display_.fillScreen(kBackgroundColor);
  drawAircraftSymbol(125, 29, 90.0, false);
  display_.setFont(&FreeSansBold12pt7b);
  int16_t x1;
  int16_t y1;
  uint16_t width;
  uint16_t height;
  display_.getTextBounds(title, 0, 0, &x1, &y1, &width, &height);
  display_.setCursor((settings::kVisibleWidth - static_cast<int16_t>(width)) / 2,
                     72);
  display_.print(title);
  display_.setFont(nullptr);
  display_.getTextBounds(subtitle, 0, 0, &x1, &y1, &width, &height);
  display_.setCursor((settings::kVisibleWidth - static_cast<int16_t>(width)) / 2,
                     91);
  display_.print(subtitle);
  display_.drawFastHLine(89, 105, 72, kForegroundColor);
}

void DisplayUi::drawAircraftPlaque(const Aircraft& aircraft,
                                   const RouteInfo& route,
                                   double displayHeadingDegrees,
                                   time_t updatedAtUtc) {
  display_.fillScreen(kBackgroundColor);
  drawClippedText(airlineIdentity(route), 6, 5, 160);
  drawCallsignWithLogoAndRoute(aircraft.callsign, route, aircraft.squawk, 5,
                               34, 179);

  display_.setFont(nullptr);
  std::string typeLine = aircraft.typeCode;
  if (aircraft.description != aircraft.typeCode &&
      aircraft.description != "TYPE UNKNOWN") {
    typeLine += " / ";
    typeLine += aircraft.description;
  }
  drawClippedText(typeLine, 6, 45, 132);
  std::string registration = aircraft.registration;
  const size_t maximumRegistrationCharacters = aircraft.hasHeading ? 9 : 18;
  if (registration.size() > maximumRegistrationCharacters) {
    registration.resize(maximumRegistrationCharacters);
  }
  drawBoldClassicText("REG", 6, 76);
  display_.setFont(nullptr);
  display_.setCursor(30, 76);
  display_.print(registration.c_str());
  int16_t identityEndX =
      30 + static_cast<int16_t>(registration.size()) * 6;
  if (aircraft.hasHeading) {
    const int16_t headingLabelX =
        30 + static_cast<int16_t>(registration.size()) * 6 + 12;
    const char* headingLabel =
        aircraft.headingSource == HeadingSource::kTrack ? "TRK" : "HDG";
    drawBoldClassicText(headingLabel, headingLabelX, 76);
    display_.setFont(nullptr);
    display_.setCursor(headingLabelX + 24, 76);
    display_.print(formatHeading(aircraft.headingDeg).c_str());
    identityEndX = headingLabelX + 24 + 18;
  }
  const char* motion = distanceTrendLabel(aircraft.distanceTrend);
  constexpr int16_t kMotionRightEdge = 193;
  const int16_t motionWidth = static_cast<int16_t>(std::strlen(motion) * 6);
  const int16_t desiredMotionX = identityEndX + 7;
  const int16_t maximumMotionX = kMotionRightEdge - motionWidth;
  drawBoldClassicText(motion,
                      desiredMotionX < maximumMotionX ? desiredMotionX
                                                      : maximumMotionX,
                      76);
  // Keep the direction roundel and its readout together in their own quiet
  // panel, with clear separation from the date above and divider below.
  drawCompass(aircraft, 218, 42, 20, displayHeadingDegrees);
  const std::string bearing = formatBearing(aircraft.bearingDeg);
  const int16_t bearingX =
      218 - static_cast<int16_t>(bearing.size() * 6) / 2;
  display_.setFont(nullptr);
  display_.setCursor(bearingX, 66);
  display_.print(bearing.c_str());

  char updatedTime[8] = "--:--Z";
  char updatedDate[6] = "-----";
  if (updatedAtUtc > 0) {
    tm utcTime = {};
    gmtime_r(&updatedAtUtc, &utcTime);
    std::snprintf(updatedTime, sizeof(updatedTime), "%02d:%02dZ",
                  utcTime.tm_hour, utcTime.tm_min);
    static const char* kMonths[] = {"JAN", "FEB", "MAR", "APR",
                                    "MAY", "JUN", "JUL", "AUG",
                                    "SEP", "OCT", "NOV", "DEC"};
    std::snprintf(updatedDate, sizeof(updatedDate), "%02d%s",
                  utcTime.tm_mday, kMonths[utcTime.tm_mon]);
  }
  display_.setFont(nullptr);
  display_.setCursor(173, 5);
  display_.print(updatedDate);
  display_.print(" ");
  display_.print(updatedTime);

  display_.drawFastHLine(5, 86, 240, kForegroundColor);
  drawMetric(0, "SPD", formatGroundspeed(aircraft));
  drawMetric(1, "DIST", formatDistance(aircraft.distanceNm));
  drawMetric(2, "ALT", formatAltitude(aircraft.altitudeFt));
  drawMetric(3, "FPM", formatVerticalRate(aircraft));
}

void DisplayUi::drawCompass(const Aircraft& aircraft, int16_t centerX,
                            int16_t centerY, int16_t radius,
                            double displayHeadingDegrees) {
  display_.drawCircle(centerX, centerY, radius, kForegroundColor);
  drawLookArrow(centerX, centerY,
                relativeDirectionDeg(aircraft.bearingDeg,
                                     displayHeadingDegrees),
                radius - 6);
}

void DisplayUi::drawLookArrow(int16_t centerX, int16_t centerY,
                              double relativeDirectionDegrees,
                              int16_t length) {
  // Build every point in arrow-local coordinates before rotating it. The old
  // implementation thickened the shaft with a fixed screen-x offset, which
  // made it appear off-center except when pointing vertically.
  const Point tip = rotatePoint(centerX, centerY, relativeDirectionDegrees, 0,
                                -length);
  const int16_t headBaseY = -length + 9;
  const Point headLeft = rotatePoint(centerX, centerY,
                                     relativeDirectionDegrees, -5, headBaseY);
  const Point headRight = rotatePoint(centerX, centerY,
                                      relativeDirectionDegrees, 5, headBaseY);
  const Point shaftTopLeft = rotatePoint(
      centerX, centerY, relativeDirectionDegrees, -1, headBaseY);
  const Point shaftTopRight = rotatePoint(
      centerX, centerY, relativeDirectionDegrees, 1, headBaseY);
  const Point tailLeft = rotatePoint(centerX, centerY,
                                     relativeDirectionDegrees, -1, length);
  const Point tailRight = rotatePoint(centerX, centerY,
                                      relativeDirectionDegrees, 1, length);

  display_.fillTriangle(tip.x, tip.y, headLeft.x, headLeft.y, headRight.x,
                        headRight.y, kForegroundColor);
  display_.fillTriangle(shaftTopLeft.x, shaftTopLeft.y, shaftTopRight.x,
                        shaftTopRight.y, tailLeft.x, tailLeft.y,
                        kForegroundColor);
  display_.fillTriangle(shaftTopRight.x, shaftTopRight.y, tailLeft.x,
                        tailLeft.y, tailRight.x, tailRight.y,
                        kForegroundColor);
}

void DisplayUi::drawAircraftSymbol(int16_t centerX, int16_t centerY,
                                   double headingDegrees, bool filled) {
  const Point nose = rotatePoint(centerX, centerY, headingDegrees, 0, -13);
  const Point leftWing = rotatePoint(centerX, centerY, headingDegrees, -12, 3);
  const Point rightWing = rotatePoint(centerX, centerY, headingDegrees, 12, 3);
  const Point tail = rotatePoint(centerX, centerY, headingDegrees, 0, 12);
  const Point leftTail = rotatePoint(centerX, centerY, headingDegrees, -5, 9);
  const Point rightTail = rotatePoint(centerX, centerY, headingDegrees, 5, 9);

  if (filled) {
    const Point bodyLeft = rotatePoint(centerX, centerY, headingDegrees, -2, 7);
    const Point bodyRight = rotatePoint(centerX, centerY, headingDegrees, 2, 7);
    display_.fillTriangle(nose.x, nose.y, bodyLeft.x, bodyLeft.y, bodyRight.x,
                          bodyRight.y, kForegroundColor);
    display_.fillTriangle(leftWing.x, leftWing.y, rightWing.x, rightWing.y,
                          tail.x, tail.y, kForegroundColor);
    display_.fillTriangle(leftTail.x, leftTail.y, rightTail.x, rightTail.y,
                          tail.x, tail.y, kForegroundColor);
  } else {
    display_.drawLine(nose.x, nose.y, tail.x, tail.y, kForegroundColor);
    display_.drawLine(leftWing.x, leftWing.y, rightWing.x, rightWing.y,
                      kForegroundColor);
    display_.drawLine(leftTail.x, leftTail.y, rightTail.x, rightTail.y,
                      kForegroundColor);
  }
}

void DisplayUi::drawBoldClassicText(const char* text, int16_t x, int16_t y) {
  display_.setFont(nullptr);
  display_.setCursor(x, y);
  display_.print(text);
  display_.setCursor(x + 1, y);
  display_.print(text);
}

void DisplayUi::drawCallsignWithLogoAndRoute(const std::string& callsign,
                                             const RouteInfo& route,
                                             const std::string& squawk,
                                             int16_t x,
                                             int16_t baseline,
                                             int16_t maximumWidth) {
  constexpr int16_t kItemGap = 4;
  constexpr int16_t kRouteLabelWidth = 31;
  constexpr int16_t kSquawkLabelWidth = 37;
  constexpr int16_t kRouteBlockMinimumWidth = kSquawkLabelWidth;
  std::string routeText = "N/A";
  if (route.available()) {
    routeText = route.origin + ">" + route.destination;
  }

  display_.setFont(nullptr);
  int16_t routeX1;
  int16_t routeY1;
  uint16_t routeWidth;
  uint16_t routeHeight;
  display_.getTextBounds(routeText.c_str(), 0, 0, &routeX1, &routeY1,
                         &routeWidth, &routeHeight);

  const int16_t routeBlockWidth =
      static_cast<int16_t>(routeWidth) > kRouteBlockMinimumWidth
          ? static_cast<int16_t>(routeWidth)
          : kRouteBlockMinimumWidth;
  const int16_t routeReservation =
      kItemGap + routeBlockWidth;
  const int16_t maximumTextWidth = maximumWidth - kItemGap -
                                   kAirlineLogoWidth - routeReservation;
  display_.setFont(&FreeSansBold12pt7b);
  int16_t x1;
  int16_t y1;
  uint16_t width;
  uint16_t height;
  display_.getTextBounds(callsign.c_str(), x, baseline, &x1, &y1, &width,
                         &height);
  if (static_cast<int16_t>(width) > maximumTextWidth) {
    display_.setFont(&FreeSansBold9pt7b);
    display_.getTextBounds(callsign.c_str(), x, baseline, &x1, &y1, &width,
                           &height);
  }
  display_.setCursor(x, baseline);
  display_.print(callsign.c_str());

  const int16_t routeBlockX = x + maximumWidth - routeBlockWidth;
  const int16_t routeValueX =
      routeBlockX +
      (routeBlockWidth - static_cast<int16_t>(routeWidth)) / 2;
  const int16_t desiredLogoX =
      x1 + static_cast<int16_t>(width) + kItemGap;
  const int16_t maximumLogoX =
      routeBlockX - kItemGap - kAirlineLogoWidth;
  const int16_t logoX =
      desiredLogoX < maximumLogoX ? desiredLogoX : maximumLogoX;
  const int16_t logoY =
      y1 + (static_cast<int16_t>(height) - kAirlineLogoHeight) / 2;
  display_.drawBitmap(logoX, logoY, airlineLogoForCallsign(callsign),
                      kAirlineLogoWidth, kAirlineLogoHeight,
                      kForegroundColor);

  display_.setFont(nullptr);
  const int16_t routeLabelX =
      routeBlockX + (routeBlockWidth - kRouteLabelWidth) / 2;
  drawBoldClassicText("ROUTE", routeLabelX, 19);
  display_.setFont(nullptr);
  display_.setCursor(routeValueX, 30);
  display_.print(routeText.c_str());

  const int16_t squawkLabelX =
      routeBlockX + (routeBlockWidth - kSquawkLabelWidth) / 2;
  const int16_t squawkValueWidth =
      static_cast<int16_t>(squawk.size()) * 6;
  const int16_t squawkValueX =
      routeBlockX + (routeBlockWidth - squawkValueWidth) / 2;
  drawBoldClassicText("SQUAWK", squawkLabelX, 43);
  display_.setFont(nullptr);
  display_.setCursor(squawkValueX, 55);
  display_.print(squawk.c_str());
}

void DisplayUi::drawClippedText(const std::string& text, int16_t x,
                                int16_t baseline, int16_t maximumWidth) {
  display_.setFont(nullptr);
  const size_t maximumCharacters = static_cast<size_t>(maximumWidth / 6);
  std::string clipped = text;
  if (clipped.size() > maximumCharacters && maximumCharacters > 3) {
    clipped.resize(maximumCharacters - 3);
    clipped += "...";
  }
  display_.setCursor(x, baseline);
  display_.print(clipped.c_str());
}

void DisplayUi::drawMetric(int16_t column, const char* label,
                           const std::string& value) {
  constexpr int16_t kColumnWidth = 62;
  const int16_t left = column * kColumnWidth;
  if (column > 0) {
    display_.drawFastVLine(left, 91, 29, kForegroundColor);
  }
  drawBoldClassicText(label, left + 5, 93);
  drawClippedText(value, left + 5, 110, kColumnWidth - 8);
}

}  // namespace skyprint
