#include <Arduino.h>
#include <WiFi.h>

#include <ctime>

#if __has_include("config.h")
#include "config.h"
#else
#error "Missing include/config.h. Copy include/config.example.h and fill in your Wi-Fi and office coordinates."
#endif

#if !defined(SKYPRINT_CONFIGURED) || SKYPRINT_CONFIGURED != 1
#error "Set SKYPRINT_CONFIGURED to 1 in include/config.h after filling in its values."
#endif

#ifndef SKYPRINT_DISPLAY_HEADING_DEGREES
#error "Set SKYPRINT_DISPLAY_HEADING_DEGREES in include/config.h; see config.example.h."
#endif

#ifndef SKYPRINT_INVERT_DISPLAY
#error "Set SKYPRINT_INVERT_DISPLAY in include/config.h; see config.example.h."
#endif

#include "aircraft.h"
#include "aircraft_client.h"
#include "display_ui.h"
#include "route_client.h"
#include "settings.h"

namespace {

static_assert(SKYPRINT_DISPLAY_HEADING_DEGREES >= 0 &&
                  SKYPRINT_DISPLAY_HEADING_DEGREES < 360,
              "SKYPRINT_DISPLAY_HEADING_DEGREES must be from 0 through 359");
static_assert(SKYPRINT_INVERT_DISPLAY == 0 || SKYPRINT_INVERT_DISPLAY == 1,
              "SKYPRINT_INVERT_DISPLAY must be 0 or 1");

skyprint::AdsbLolClient aircraftClient;
skyprint::AircraftSelector aircraftSelector;
skyprint::DisplayUi displayUi;
skyprint::AviationMetadataClient metadataClient;
skyprint::RouteInfo currentRoute;
std::string currentAircraftName;

uint32_t lastPollAt = 0;
uint32_t lastSuccessfulPollAt = 0;
uint32_t lastPortraitRenderAt = 0;
uint32_t lastWifiAttemptAt = 0;
uint32_t wifiRetryIntervalMs = 5000;
uint32_t lastRouteAttemptAt = 0;
uint32_t lastAircraftNameAttemptAt = 0;
time_t lastSuccessfulPollUtc = 0;
uint8_t consecutiveNetworkFailures = 0;
bool hasSuccessfulPoll = false;
bool wifiConnectionReported = false;
bool routeLookupCompleted = false;
std::string routeCallsign;
bool aircraftNameLookupCompleted = false;
std::string aircraftNameHex;

bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return static_cast<uint32_t>(now - since) >= interval;
}

void beginWifi(uint32_t now) {
  Serial.printf("[wifi] connecting to %s\n", SKYPRINT_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  // Keep DHCP addressing while using resolvers that are independent of the
  // router's frequently unreliable DNS proxy.
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, IPAddress(1, 1, 1, 1),
              IPAddress(8, 8, 8, 8));
  WiFi.begin(SKYPRINT_WIFI_SSID, SKYPRINT_WIFI_PASSWORD);
  lastWifiAttemptAt = now;
}

void maintainWifi(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    wifiRetryIntervalMs = 5000;
    if (!wifiConnectionReported) {
      const String localIp = WiFi.localIP().toString();
      const String dnsIp = WiFi.dnsIP().toString();
      Serial.printf("[wifi] connected ip=%s rssi=%d dBm dns=%s\n",
                    localIp.c_str(), WiFi.RSSI(), dnsIp.c_str());
      wifiConnectionReported = true;
    }
    return;
  }
  wifiConnectionReported = false;
  if (!elapsed(now, lastWifiAttemptAt, wifiRetryIntervalMs)) return;

  Serial.println("[wifi] reconnecting");
  WiFi.disconnect(false, false);
  WiFi.begin(SKYPRINT_WIFI_SSID, SKYPRINT_WIFI_PASSWORD);
  lastWifiAttemptAt = now;
  wifiRetryIntervalMs = min<uint32_t>(wifiRetryIntervalMs * 2UL, 60000UL);
}

void renderCurrentAircraft(bool forceFull, uint32_t now) {
  const skyprint::Aircraft* current = aircraftSelector.current();
  if (current == nullptr) return;
  skyprint::Aircraft portrait = *current;
  if (!currentAircraftName.empty()) {
    portrait.description = currentAircraftName;
  }
  displayUi.showAircraft(portrait, currentRoute,
                         SKYPRINT_DISPLAY_HEADING_DEGREES,
                         lastSuccessfulPollUtc, forceFull);
  lastPortraitRenderAt = now;
}

bool fetchAircraftNameForCurrent(uint32_t now, bool newAircraft) {
  const skyprint::Aircraft* current = aircraftSelector.current();
  if (current == nullptr) return false;
  const std::string previousName = currentAircraftName;
  const bool identityChanged = newAircraft || aircraftNameHex != current->hex;
  if (identityChanged) {
    currentAircraftName.clear();
    aircraftNameHex = current->hex;
    aircraftNameLookupCompleted = false;
    lastAircraftNameAttemptAt = 0;
  }

  if (current->description != "TYPE UNKNOWN" &&
      current->description != current->typeCode) {
    currentAircraftName = current->description;
    aircraftNameLookupCompleted = true;
    return currentAircraftName != previousName;
  }

  if (aircraftNameLookupCompleted ||
      (!identityChanged && lastAircraftNameAttemptAt != 0 &&
       !elapsed(now, lastAircraftNameAttemptAt,
                skyprint::settings::kMetadataRetryIntervalMs))) {
    return currentAircraftName != previousName;
  }

  lastAircraftNameAttemptAt = now;
  const skyprint::AircraftNameFetchResult nameFetch =
      metadataClient.fetchAircraftName(current->hex);
  aircraftNameLookupCompleted = nameFetch.completed;
  if (!nameFetch.fullName.empty()) {
    currentAircraftName = nameFetch.fullName;
    Serial.printf("[aircraft] %s / %s\n", current->typeCode.c_str(),
                  currentAircraftName.c_str());
  } else {
    Serial.printf("[aircraft] %s\n", nameFetch.error.c_str());
  }
  return currentAircraftName != previousName;
}

void fetchRouteForCurrent(uint32_t now, bool newAircraft) {
  const skyprint::Aircraft* current = aircraftSelector.current();
  if (current == nullptr) return;
  const bool routeIdentityChanged =
      newAircraft || routeCallsign != current->callsign;
  if (routeIdentityChanged) {
    currentRoute = {};
    routeCallsign = current->callsign;
    routeLookupCompleted = false;
  }
  if (routeLookupCompleted ||
      (!routeIdentityChanged && lastRouteAttemptAt != 0 &&
       !elapsed(now, lastRouteAttemptAt,
                skyprint::settings::kMetadataRetryIntervalMs))) {
    return;
  }

  lastRouteAttemptAt = now;
  const skyprint::RouteFetchResult routeFetch =
      metadataClient.fetchRoute(current->callsign, current->latitude,
                                current->longitude);
  routeLookupCompleted = routeFetch.completed;
  if (routeFetch.completed || routeFetch.route.available() ||
      routeFetch.route.hasAirlineIdentity()) {
    currentRoute = routeFetch.route;
  }
  if (routeFetch.route.available()) {
    Serial.printf("[route] %s > ", currentRoute.origin.c_str());
    if (!currentRoute.midpoint.empty()) {
      Serial.printf("%s > ", currentRoute.midpoint.c_str());
    }
    Serial.println(currentRoute.destination.c_str());
  } else {
    Serial.printf("[route] %s\n", routeFetch.error.c_str());
  }
  if (routeFetch.route.hasAirlineIdentity()) {
    Serial.printf("[airline] %s / %s\n",
                  routeFetch.route.airlineCallsign.c_str(),
                  routeFetch.route.airlineName.c_str());
  }
}

void handleSuccessfulPoll(const skyprint::FetchResult& fetch, uint32_t now) {
  hasSuccessfulPoll = true;
  lastSuccessfulPollAt = now;
  lastSuccessfulPollUtc = std::time(nullptr);
  const skyprint::DistanceTrend previousDistanceTrend =
      aircraftSelector.current() == nullptr
          ? skyprint::DistanceTrend::kUnknown
          : aircraftSelector.current()->distanceTrend;
  const skyprint::SelectionEvent event =
      aircraftSelector.ingestSuccessfulScan(fetch.aircraft);
  const bool distanceTrendChanged =
      aircraftSelector.current() != nullptr &&
      aircraftSelector.current()->distanceTrend != previousDistanceTrend;

  Serial.printf("[adsb] valid aircraft: %u\n",
                static_cast<unsigned>(fetch.aircraft.size()));
  switch (event) {
    case skyprint::SelectionEvent::kSelected:
    case skyprint::SelectionEvent::kSwitched:
      Serial.printf("[select] portrait %s\n",
                    aircraftSelector.current()->hex.c_str());
      fetchAircraftNameForCurrent(now, true);
      fetchRouteForCurrent(now, true);
      renderCurrentAircraft(true, now);
      break;
    case skyprint::SelectionEvent::kUpdated: {
      const bool aircraftNameChanged =
          fetchAircraftNameForCurrent(now, false);
      fetchRouteForCurrent(now, false);
      if (displayUi.screenKind() != skyprint::ScreenKind::kAircraft) {
        renderCurrentAircraft(true, now);
      } else if (aircraftNameChanged || distanceTrendChanged) {
        renderCurrentAircraft(false, now);
      } else if (elapsed(now, lastPortraitRenderAt,
                         skyprint::settings::kPortraitRefreshIntervalMs)) {
        renderCurrentAircraft(false, now);
      }
      break;
    }
    case skyprint::SelectionEvent::kCleared:
      if (displayUi.screenKind() != skyprint::ScreenKind::kClearSky) {
        Serial.println("[select] clear sky");
        displayUi.showClearSky(true);
      }
      break;
    case skyprint::SelectionEvent::kNone:
      Serial.printf("[select] current aircraft missing (%u/%u)\n",
                    aircraftSelector.missingCount(),
                    skyprint::settings::kMissingConfirmations);
      break;
  }
}

void pollAircraft(uint32_t now) {
  lastPollAt = now;
  const skyprint::FetchResult fetch = aircraftClient.fetchNearby(
      SKYPRINT_OFFICE_LATITUDE, SKYPRINT_OFFICE_LONGITUDE,
      skyprint::settings::kSearchRadiusNm);
  if (!fetch.success) {
    Serial.printf("[adsb] %s\n", fetch.error.c_str());
    const bool networkFailure =
        fetch.failure == skyprint::FetchFailure::kDns ||
        fetch.failure == skyprint::FetchFailure::kConnection;
    if (networkFailure) {
      ++consecutiveNetworkFailures;
      if (consecutiveNetworkFailures >=
          skyprint::settings::kNetworkFailuresBeforeReconnect) {
        Serial.println("[wifi] repeated network failures; resetting link");
        WiFi.disconnect(false, false);
        wifiConnectionReported = false;
        lastWifiAttemptAt = now - wifiRetryIntervalMs;
        consecutiveNetworkFailures = 0;
      }
    } else {
      consecutiveNetworkFailures = 0;
    }
    return;
  }
  consecutiveNetworkFailures = 0;
  handleSuccessfulPoll(fetch, now);
}

void redrawCurrentScreenFull(uint32_t now) {
  switch (displayUi.screenKind()) {
    case skyprint::ScreenKind::kAircraft:
      renderCurrentAircraft(true, now);
      break;
    case skyprint::ScreenKind::kClearSky:
      displayUi.showClearSky(true);
      break;
    case skyprint::ScreenKind::kScanning:
      displayUi.showScanning(true);
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\nSkyprint closest-aircraft display");

  displayUi.begin();
  displayUi.showScanning(true);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  beginWifi(millis());
}

void loop() {
  const uint32_t now = millis();
  maintainWifi(now);

  if (WiFi.status() == WL_CONNECTED &&
      (lastPollAt == 0 ||
       elapsed(now, lastPollAt, skyprint::settings::kPollIntervalMs))) {
    pollAircraft(now);
  }

  if (hasSuccessfulPoll &&
      elapsed(now, lastSuccessfulPollAt,
              skyprint::settings::kDataStaleAfterMs) &&
      displayUi.screenKind() != skyprint::ScreenKind::kScanning) {
    Serial.println("[state] live data stale; scanning");
    displayUi.showScanning(true);
  }

  if (displayUi.needsDailyFullRefresh(now)) {
    Serial.println("[display] scheduled full refresh");
    redrawCurrentScreenFull(now);
  }

  delay(25);
}
