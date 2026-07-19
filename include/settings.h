#pragma once

#include <stdint.h>

namespace skyprint {
namespace settings {

constexpr int kMosiPin = 14;
constexpr int kClockPin = 27;
constexpr int kChipSelectPin = 26;
constexpr int kDataCommandPin = 25;
constexpr int kResetPin = 33;
constexpr int kBusyPin = 32;

constexpr int kVisibleWidth = 250;
constexpr int kVisibleHeight = 122;

constexpr char kApiBaseUrl[] = "https://api.adsb.lol/v2/point";
constexpr char kAdsbLolRouteApiBaseUrl[] =
    "https://api.adsb.lol/api/0/route";
constexpr char kApiHostname[] = "api.adsb.lol";
constexpr uint16_t kApiPort = 443;
constexpr char kAdsbDbRouteApiBaseUrl[] =
    "https://api.adsbdb.com/v0/callsign";
constexpr char kAdsbDbAircraftApiBaseUrl[] =
    "https://api.adsbdb.com/v0/aircraft";
constexpr char kAdsbDbApiHostname[] = "api.adsbdb.com";
constexpr uint16_t kAdsbDbApiPort = 443;
constexpr double kSearchRadiusNm = 25.0;
constexpr double kMaximumPositionAgeSeconds = 30.0;

constexpr uint32_t kPollIntervalMs = 20UL * 1000UL;
constexpr uint32_t kPortraitRefreshIntervalMs = 60UL * 1000UL;
constexpr uint32_t kDataStaleAfterMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kDailyFullRefreshMs = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kTlsHandshakeTimeoutSeconds = 15;
constexpr uint32_t kHttpConnectTimeoutMs = 15UL * 1000UL;
constexpr uint32_t kHttpResponseTimeoutMs = 30UL * 1000UL;
constexpr uint32_t kMetadataRetryIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint8_t kNetworkFailuresBeforeReconnect = 3;

constexpr double kChallengerDistanceRatio = 0.80;
constexpr double kMinimumSwitchAdvantageNm = 0.5;
constexpr double kDistanceTrendDeadbandNm = 0.05;
constexpr uint8_t kChallengerConfirmations = 2;
constexpr uint8_t kMissingConfirmations = 2;
constexpr uint8_t kClosestPointHoldPolls = 2;

}  // namespace settings
}  // namespace skyprint
