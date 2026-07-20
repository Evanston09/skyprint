#include "route_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "adsblol_ca.h"
#include "route_selection.h"
#include "settings.h"

namespace skyprint {
namespace {

// Let’s Encrypt Root YE from
// https://letsencrypt.org/certs/gen-y/root-ye.pem. ADSBDB is currently issued
// through YE2; trusting the root directly avoids the longer cross-sign path on
// the ESP32's constrained TLS client.
constexpr char kLetsEncryptRootYe[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIB2TCCAWCgAwIBAgIRAKQCa6LvbHwg1AR+XmWmk4AwCgYIKoZIzj0EAwMwLjEL
MAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWUUwHhcN
MjUwOTAzMDAwMDAwWhcNNDUwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzENMAsG
A1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZRTB2MBAGByqGSM49AgEGBSuBBAAi
A2IABDwS/6vhrcVqcbBo+wgdI3fwn9x7DNJJOY/lTOti0vkwuRN87RhEhTH17E7X
yFjWsPYhIPt/wzOqxTd2b+4ZJNy9ID04YywF9U5zasDVyGSNErVNtz8uSGh5izW8
7j77GaNCMEAwDgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0O
BBYEFKPIJlqOoUzQNWP8myPIOq5W809WMAoGCCqGSM49BAMDA2cAMGQCMHhMr8N9
LdL1VQKs9BdV81r76eXRB6mtjuNjzk6/lBsPNToWLTDzGYgtQKO1jl63uAIwGV7m
onyF377c+MM1oqVNs17sgu7F9YKZwgLmVbeOMDbKAXHtKMDLbiGllCcs8f47
-----END CERTIFICATE-----
)CERT";

PublishedAirport publishedAirport(JsonObjectConst airport) {
  PublishedAirport result;
  const char* iata = airport["iata_code"] | "";
  if (iata[0] == '\0') iata = airport["iata"] | "";
  const char* icao = airport["icao_code"] | "";
  if (icao[0] == '\0') icao = airport["icao"] | "";
  result.code = iata[0] != '\0' ? iata : icao;
  result.latitude = airport["lat"] | 0.0;
  result.longitude = airport["lon"] | 0.0;
  return result;
}

std::string uppercaseTrimmed(const char* value) {
  if (value == nullptr) return {};
  std::string result(value);
  const auto first = std::find_if_not(
      result.begin(), result.end(),
      [](unsigned char character) { return std::isspace(character); });
  const auto last = std::find_if_not(
      result.rbegin(), result.rend(),
      [](unsigned char character) { return std::isspace(character); })
                        .base();
  if (first >= last) return {};
  result = std::string(first, last);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return result;
}

}  // namespace

RouteFetchResult AviationMetadataClient::fetchAdsbDbAirlineIdentity(
    const std::string& callsign) const {
  RouteFetchResult result;
  if (callsign.empty() || callsign == "NO CALLSIGN") {
    result.completed = true;
    result.error = "airline identity unavailable without a callsign";
    return result;
  }

  IPAddress apiAddress;
  if (!WiFi.hostByName(settings::kAdsbDbApiHostname, apiAddress)) {
    result.error = std::string("airline identity DNS lookup failed for ") +
                   settings::kAdsbDbApiHostname;
    return result;
  }

  WiFiClientSecure secureClient;
  secureClient.setCACert(kLetsEncryptRootYe);
  secureClient.setHandshakeTimeout(settings::kTlsHandshakeTimeoutSeconds);
  if (!secureClient.connect(apiAddress, settings::kAdsbDbApiPort,
                            settings::kAdsbDbApiHostname, kLetsEncryptRootYe,
                            nullptr, nullptr)) {
    result.error = "airline identity TLS connection failed";
    char tlsError[128] = {};
    const int tlsErrorCode = secureClient.lastError(tlsError, sizeof(tlsError));
    if (tlsErrorCode < 0) {
      result.error += std::string(": ") + std::to_string(tlsErrorCode) +
                      " (" + tlsError + ")";
    }
    return result;
  }

  String url = settings::kAdsbDbRouteApiBaseUrl;
  url += "/";
  url += callsign.c_str();

  HTTPClient http;
  http.setConnectTimeout(settings::kHttpConnectTimeoutMs);
  http.setTimeout(settings::kHttpResponseTimeoutMs);
  http.useHTTP10(true);
  if (!http.begin(secureClient, url)) {
    result.error = "could not initialize airline identity HTTPS request";
    return result;
  }
  http.setUserAgent("Skyprint/1.0 personal-aircraft-display");
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");

  const int status = http.GET();
  if (status == HTTP_CODE_BAD_REQUEST || status == HTTP_CODE_NOT_FOUND) {
    result.completed = true;
    result.error = "airline identity unavailable";
    http.end();
    return result;
  }
  if (status != HTTP_CODE_OK) {
    result.error = status < 0
                       ? std::string("airline identity HTTPS request failed: ") +
                             http.errorToString(status).c_str()
                       : std::string("ADSBDB returned HTTP ") +
                             std::to_string(status);
    http.end();
    return result;
  }

  JsonDocument filter;
  filter["response"]["flightroute"]["airline"]["name"] = true;
  filter["response"]["flightroute"]["airline"]["callsign"] = true;

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(
      document, http.getStream(), DeserializationOption::Filter(filter));
  if (jsonError) {
    result.error =
        std::string("airline identity JSON parse failed: ") + jsonError.c_str();
    http.end();
    return result;
  }

  const JsonObjectConst route =
      document["response"]["flightroute"].as<JsonObjectConst>();
  if (!route.isNull()) {
    const JsonObjectConst airline = route["airline"].as<JsonObjectConst>();
    result.route.airlineName = uppercaseTrimmed(airline["name"] | "");
    result.route.airlineCallsign =
        uppercaseTrimmed(airline["callsign"] | "");
  }
  result.completed = true;
  if (!result.route.hasAirlineIdentity()) {
    result.error = "airline identity unavailable";
  }
  http.end();
  return result;
}

RouteFetchResult AviationMetadataClient::fetchAdsbLolRoute(
    const std::string& callsign, double latitude, double longitude,
    bool hasHeading, double headingDegrees) const {
  RouteFetchResult result;
  if (callsign.empty() || callsign == "NO CALLSIGN") {
    result.completed = true;
    result.error = "route unavailable without a callsign";
    return result;
  }

  IPAddress apiAddress;
  if (!WiFi.hostByName(settings::kApiHostname, apiAddress)) {
    result.error =
        std::string("route DNS lookup failed for ") + settings::kApiHostname;
    return result;
  }

  WiFiClientSecure secureClient;
  secureClient.setCACert(kAdsbLolRootCa);
  secureClient.setHandshakeTimeout(settings::kTlsHandshakeTimeoutSeconds);
  if (!secureClient.connect(apiAddress, settings::kApiPort,
                            settings::kApiHostname, kAdsbLolRootCa, nullptr,
                            nullptr)) {
    result.error = "route TLS connection failed";
    char tlsError[128] = {};
    const int tlsErrorCode = secureClient.lastError(tlsError, sizeof(tlsError));
    if (tlsErrorCode < 0) {
      result.error += std::string(": ") + std::to_string(tlsErrorCode) +
                      " (" + tlsError + ")";
    }
    return result;
  }

  String url = settings::kAdsbLolRouteApiBaseUrl;
  url += "/";
  url += callsign.c_str();
  url += "/";
  url += String(latitude, 6);
  url += "/";
  url += String(longitude, 6);

  HTTPClient http;
  http.setConnectTimeout(settings::kHttpConnectTimeoutMs);
  http.setTimeout(settings::kHttpResponseTimeoutMs);
  http.useHTTP10(true);
  if (!http.begin(secureClient, url)) {
    result.error = "could not initialize route HTTPS request";
    return result;
  }
  http.setUserAgent("Skyprint/1.0 personal-aircraft-display");
  // ADSB.lol serves the route lookup used by its tar1090 map differently from
  // an unqualified API request. Match the public map's request context so the
  // position-aware endpoint returns its cached route instead of an HTTP 500.
  http.addHeader("Origin", "https://birds.adsb.lol");
  http.addHeader("Referer", "https://birds.adsb.lol/");
  http.addHeader("Accept", "application/json, text/javascript, */*; q=0.01");
  http.addHeader("Accept-Encoding", "identity");

  const int status = http.GET();
  if (status == HTTP_CODE_BAD_REQUEST || status == HTTP_CODE_NOT_FOUND) {
    result.completed = true;
    result.error = "route unavailable";
    http.end();
    return result;
  }
  if (status != HTTP_CODE_OK) {
    result.error = status < 0
                       ? std::string("route HTTPS request failed: ") +
                             http.errorToString(status).c_str()
                       : std::string("ADSB.lol route API returned HTTP ") +
                             std::to_string(status);
    http.end();
    return result;
  }

  JsonDocument filter;
  filter["_airports"][0]["iata"] = true;
  filter["_airports"][0]["icao"] = true;
  filter["_airports"][0]["lat"] = true;
  filter["_airports"][0]["lon"] = true;

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(
      document, http.getStream(), DeserializationOption::Filter(filter));
  if (jsonError) {
    result.error = std::string("route JSON parse failed: ") + jsonError.c_str();
    http.end();
    return result;
  }

  result.completed = true;
  const JsonArrayConst airportJson =
      document["_airports"].as<JsonArrayConst>();
  std::vector<PublishedAirport> airports;
  airports.reserve(airportJson.size());
  for (JsonObjectConst airport : airportJson) {
    if (airport["lat"].isNull() || airport["lon"].isNull()) continue;
    const PublishedAirport parsed = publishedAirport(airport);
    if (!parsed.code.empty() && std::isfinite(parsed.latitude) &&
        std::isfinite(parsed.longitude) && parsed.latitude >= -90.0 &&
        parsed.latitude <= 90.0 && parsed.longitude >= -180.0 &&
        parsed.longitude <= 180.0) {
      airports.push_back(parsed);
    }
  }

  if (!selectPublishedRouteLeg(airports, latitude, longitude, hasHeading,
                               headingDegrees, result.route)) {
    result.error = "route rejected as implausible";
  }
  http.end();
  return result;
}

RouteFetchResult AviationMetadataClient::fetchRoute(
    const std::string& callsign, double latitude, double longitude,
    bool hasHeading, double headingDegrees) const {
  RouteFetchResult route = fetchAdsbLolRoute(
      callsign, latitude, longitude, hasHeading, headingDegrees);
  const RouteFetchResult airline = fetchAdsbDbAirlineIdentity(callsign);
  route.route.airlineName = airline.route.airlineName;
  route.route.airlineCallsign = airline.route.airlineCallsign;
  route.completed = route.completed && airline.completed;
  if (!airline.error.empty() && route.error.empty()) {
    route.error = airline.error;
  }
  return route;
}

AircraftNameFetchResult AviationMetadataClient::fetchAircraftName(
    const std::string& hex) const {
  AircraftNameFetchResult result;
  if (hex.empty()) {
    result.completed = true;
    result.error = "aircraft name unavailable without an ICAO address";
    return result;
  }

  IPAddress apiAddress;
  if (!WiFi.hostByName(settings::kAdsbDbApiHostname, apiAddress)) {
    result.error = std::string("aircraft metadata DNS lookup failed for ") +
                   settings::kAdsbDbApiHostname;
    return result;
  }

  WiFiClientSecure secureClient;
  secureClient.setCACert(kLetsEncryptRootYe);
  secureClient.setHandshakeTimeout(settings::kTlsHandshakeTimeoutSeconds);
  if (!secureClient.connect(apiAddress, settings::kAdsbDbApiPort,
                            settings::kAdsbDbApiHostname, kLetsEncryptRootYe,
                            nullptr, nullptr)) {
    result.error = "aircraft metadata TLS connection failed";
    char tlsError[128] = {};
    const int tlsErrorCode = secureClient.lastError(tlsError, sizeof(tlsError));
    if (tlsErrorCode < 0) {
      result.error += std::string(": ") + std::to_string(tlsErrorCode) +
                      " (" + tlsError + ")";
    }
    return result;
  }

  String url = settings::kAdsbDbAircraftApiBaseUrl;
  url += "/";
  url += hex.c_str();

  HTTPClient http;
  http.setConnectTimeout(settings::kHttpConnectTimeoutMs);
  http.setTimeout(settings::kHttpResponseTimeoutMs);
  http.useHTTP10(true);
  if (!http.begin(secureClient, url)) {
    result.error = "could not initialize aircraft metadata HTTPS request";
    return result;
  }
  http.setUserAgent("Skyprint/1.0 personal-aircraft-display");
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");

  const int status = http.GET();
  if (status == HTTP_CODE_BAD_REQUEST || status == HTTP_CODE_NOT_FOUND) {
    result.completed = true;
    result.error = "aircraft name unavailable";
    http.end();
    return result;
  }
  if (status != HTTP_CODE_OK) {
    result.error = status < 0
                       ? std::string("aircraft metadata HTTPS request failed: ") +
                             http.errorToString(status).c_str()
                       : std::string("ADSBDB returned HTTP ") +
                             std::to_string(status);
    http.end();
    return result;
  }

  JsonDocument filter;
  filter["response"]["aircraft"]["manufacturer"] = true;
  filter["response"]["aircraft"]["type"] = true;

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(
      document, http.getStream(), DeserializationOption::Filter(filter));
  if (jsonError) {
    result.error =
        std::string("aircraft metadata JSON parse failed: ") + jsonError.c_str();
    http.end();
    return result;
  }

  const JsonObjectConst aircraft =
      document["response"]["aircraft"].as<JsonObjectConst>();
  const std::string manufacturer =
      uppercaseTrimmed(aircraft["manufacturer"] | "");
  const std::string type = uppercaseTrimmed(aircraft["type"] | "");
  if (!type.empty()) {
    result.fullName = type;
    if (!manufacturer.empty() && type.rfind(manufacturer, 0) != 0) {
      result.fullName = manufacturer + " " + type;
    }
  }

  result.completed = true;
  if (result.fullName.empty()) result.error = "aircraft name unavailable";
  http.end();
  return result;
}

}  // namespace skyprint
