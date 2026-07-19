#include "aircraft_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <ctime>

#include "adsblol_ca.h"
#include "aircraft_json.h"
#include "settings.h"

namespace skyprint {

// Let’s Encrypt Root YR from
// https://letsencrypt.org/certs/gen-y/root-yr.pem. ADSB.lol is issued through
// YR1; trusting YR directly avoids the longer X1 cross-sign path that can
// exceed the practical TLS limits of older embedded clients.
const char kAdsbLolRootCa[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFKTCCAxGgAwIBAgIRAOxGNJNgz0sP+KmC2Tqpyj0wDQYJKoZIhvcNAQELBQAw
LjELMAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWVIw
HhcNMjUwOTAzMDAwMDAwWhcNNDUwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzEN
MAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZUjCCAiIwDQYJKoZIhvcNAQEB
BQADggIPADCCAgoCggIBANvGJnN78CTJdWL3+eGfsLN5TrNBJs+VH9hRXqRbwxu9
sGNiB0BD1fcOxbSUQCJIM1xE13Db+5Cw1w0s0EBYsvuIP/6joF0w8cuImbgR1OGg
YbSQ4OpzI+DG8SGuTlcE873OCS+kh3srlo6vl43M5OJg4Aeo1sfHp6kTJDoIiFBN
JAY+OKfX/FUvYKuhjT+no49lmqmupSBI5PkBQiqrEGtWU5uxU/cQWHGu8jSjFBzn
ZqvbNPLMXMLFxCb3WTfrJBXXjqvWG+v4bjzxjjeAtOlU7qarRDvNOyAuQYLln904
M+faKx8hnLCpJ15ZqaEgcNlY+9MMWcC5yvL2A2j3l9+2buggZX+dOE91zYmIdawT
vSZuVvlbRrAlLxIB6pwMBjneXCjYQ8+3BCCjssbSNpZU3hTcBDdhfAlEDlYr6pEa
tnMdmDT5BqnKC92bd0EhM1fbLHioLccLCuievT8ZkPhZrq7Mii7gNXAcUEAR8+lz
Yal+9zTg7C5DALyVOeG/CqfRAMn1KSHCR0NSA6P8tn/mGRlnCct5rtVCLnVySVpU
6H1qGg3DgTOuskf8eahTMiYbI5ezPJmO5ertalskQ1utp74+eDy92PI4ftHKTbq9
IWhH4YZKh3WnJEIt+oQvlYZbY8tpEroKrFB6PFGzrJIDRyts4HqvuH52RFj2zv/B
AgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1Ud
DgQWBBTe51tg0CJtQCh9Pw0B/qS1UrRRlDANBgkqhkiG9w0BAQsFAAOCAgEAWHnf
713Bdkq7t5yN2dNIgQakUb94X9WuyhMEHHkgx4oDpSUlnG0w4g94MoqaEUE31ZjR
LU7L5LD1g9ujFHTQu8AD215AHMVQFbm6j8hQxdXHAzDajFNQnOlDJrLjzIx176oy
AjvUtejZx2NNmdb5fd0WGVGsCdoAJ3N8ozo7ajE8t6vfxStZb4BQ9WYJGHUDrv2N
i5tJF6CNiPnlzs3BUfECRbE4JSk+jvy8+VoGiFE8qsH/j78x2fjgQhAQFV7P7Zxy
dBTZ1wEkNpZNW2qnaK1SKBLa+xf6E06YRIq5uaI+HWH8SY1y5VbRgzq40EKg3yxP
06fz+uYAUIFJoLNfhwRCc3Q6pQVuMX3yAjHAes4gk4moGcLQ5p7HAh39yeylZc1J
41sx/jKwLIkPE6Rr1Nf4pxdsxf9SA4yOEiAkDgq04DVxn8hgYFdUtBCuiuVC2heA
EiqVEa+8QZjuw8Gj0EbHXcRd1nInvGqRS1o9Is7YBdQN57X1AYveGBNNqjICSb7c
awuw1EawTDrs13VUlJVEsbQ0/O/1aaV73mCdOQ8azqL2KTv1Ewu1xbquE2S+kdQU
To9TUwat3wUA6cwXh1EfpS/3fJ0aGah5hdpRyoCLDlsSn8tkrjMfFFX0viC+GxHc
sI1ANRYvqSFC2X1VRZfDg+wD6E21BccmifG4yWc=
-----END CERTIFICATE-----
)CERT";

FetchResult AdsbLolClient::fetchNearby(double latitude, double longitude,
                                       double radiusNm) const {
  FetchResult result;
  if (std::time(nullptr) < 1700000000) {
    result.failure = FetchFailure::kClock;
    result.error = "clock is not synchronized; TLS verification deferred";
    return result;
  }

  String url = settings::kApiBaseUrl;
  url += "/";
  url += String(latitude, 6);
  url += "/";
  url += String(longitude, 6);
  url += "/";
  url += String(radiusNm, 0);

  // Resolve separately so DNS failures are not flattened into HTTPClient's
  // generic "connection refused" error. Connecting to the resolved address
  // with the hostname argument preserves SNI and certificate verification.
  IPAddress apiAddress;
  if (!WiFi.hostByName(settings::kApiHostname, apiAddress)) {
    result.failure = FetchFailure::kDns;
    result.error = std::string("DNS lookup failed for ") +
                   settings::kApiHostname;
    return result;
  }

  WiFiClientSecure secureClient;
  secureClient.setCACert(kAdsbLolRootCa);
  secureClient.setHandshakeTimeout(settings::kTlsHandshakeTimeoutSeconds);
  if (!secureClient.connect(apiAddress, settings::kApiPort,
                            settings::kApiHostname, kAdsbLolRootCa, nullptr,
                            nullptr)) {
    result.failure = FetchFailure::kConnection;
    result.error = "TLS connection failed";
    char tlsError[128] = {};
    const int tlsErrorCode = secureClient.lastError(tlsError, sizeof(tlsError));
    if (tlsErrorCode < 0) {
      result.error += std::string(": ") + std::to_string(tlsErrorCode) +
                      " (" + tlsError + ")";
    }
    return result;
  }

  HTTPClient http;
  http.setConnectTimeout(settings::kHttpConnectTimeoutMs);
  http.setTimeout(settings::kHttpResponseTimeoutMs);
  http.useHTTP10(true);
  if (!http.begin(secureClient, url)) {
    result.failure = FetchFailure::kConnection;
    result.error = "could not initialize HTTPS request";
    return result;
  }
  http.setUserAgent("Skyprint/1.0 personal-aircraft-display");
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");

  result.httpStatus = http.GET();
  if (result.httpStatus != HTTP_CODE_OK) {
    result.failure = result.httpStatus < 0 ? FetchFailure::kConnection
                                          : FetchFailure::kHttp;
    result.error = result.httpStatus < 0
                       ? std::string("HTTPS request failed: ") +
                             http.errorToString(result.httpStatus).c_str()
                       : std::string("ADSB.lol returned HTTP ") +
                             std::to_string(result.httpStatus);
    http.end();
    return result;
  }

  if (!parseAircraftJson(http.getStream(), latitude, longitude, radiusNm,
                         result.aircraft, result.error)) {
    result.failure = FetchFailure::kJson;
    http.end();
    return result;
  }

  result.success = true;
  http.end();
  return result;
}

}  // namespace skyprint
