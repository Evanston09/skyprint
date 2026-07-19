#pragma once

#include <stdint.h>
#include <time.h>

#include <GxEPD2_BW.h>
#include <epd/GxEPD2_213_B74.h>

#include "aircraft.h"
#include "route.h"

namespace skyprint {

enum class ScreenKind {
  kScanning,
  kClearSky,
  kAircraft,
};

class DisplayUi {
 public:
  DisplayUi();

  void begin();
  void showScanning(bool forceFull = true);
  void showClearSky(bool forceFull = true);
  void showAircraft(const Aircraft& aircraft, const RouteInfo& route,
                    double displayHeadingDegrees, time_t updatedAtUtc,
                    bool forceFull);
  bool needsDailyFullRefresh(uint32_t now) const;

  ScreenKind screenKind() const { return screenKind_; }

 private:
  using Display =
      GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT>;

  bool chooseFullRefresh(bool requestedFull) const;
  void finishRefresh(bool fullRefresh);
  void drawStatusScreen(const char* title, const char* subtitle);
  void drawAircraftPlaque(const Aircraft& aircraft, const RouteInfo& route,
                          double displayHeadingDegrees, time_t updatedAtUtc);
  void drawCompass(const Aircraft& aircraft, int16_t centerX,
                   int16_t centerY, int16_t radius,
                   double displayHeadingDegrees);
  void drawLookArrow(int16_t centerX, int16_t centerY,
                     double relativeDirectionDegrees, int16_t length);
  void drawAircraftSymbol(int16_t centerX, int16_t centerY,
                          double headingDegrees, bool filled);
  void drawBoldClassicText(const char* text, int16_t x, int16_t y);
  void drawCallsignWithLogoAndRoute(const std::string& callsign,
                                    const RouteInfo& route,
                                    const std::string& squawk, int16_t x,
                                    int16_t baseline, int16_t maximumWidth);
  void drawClippedText(const std::string& text, int16_t x, int16_t baseline,
                       int16_t maximumWidth);
  void drawMetric(int16_t column, const char* label,
                  const std::string& value);

  Display display_;
  ScreenKind screenKind_ = ScreenKind::kScanning;
  uint8_t partialRefreshCount_ = 0;
  uint32_t lastFullRefreshAt_ = 0;
};

}  // namespace skyprint
