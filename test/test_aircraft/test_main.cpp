#include <unity.h>

#include <string>
#include <vector>

#include "aircraft.h"
#include "aircraft_json.h"

using skyprint::Aircraft;
using skyprint::AircraftSelector;
using skyprint::DistanceTrend;
using skyprint::HeadingSource;
using skyprint::SelectionEvent;

namespace {

Aircraft makeAircraft(const char* hex, double distance) {
  Aircraft aircraft;
  aircraft.hex = hex;
  aircraft.callsign = hex;
  aircraft.registration = hex;
  aircraft.typeCode = "A320";
  aircraft.description = "AIRBUS A320";
  aircraft.altitudeFt = 10000;
  aircraft.distanceNm = distance;
  return aircraft;
}

void testGeometryAndBearing() {
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.04f, static_cast<float>(
                                              skyprint::distanceNm(0.0, 0.0,
                                                                   0.0, 1.0)));
  TEST_ASSERT_FLOAT_WITHIN(
      0.01f, 90.0f,
      static_cast<float>(
          skyprint::initialBearingDeg(0.0, 0.0, 0.0, 1.0)));
  TEST_ASSERT_EQUAL_STRING("E", skyprint::cardinalDirection(90.0));
  TEST_ASSERT_EQUAL_STRING("N", skyprint::cardinalDirection(359.0));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 350.0f,
      static_cast<float>(skyprint::normalizeDegrees(-10.0)));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 90.0f,
      static_cast<float>(skyprint::relativeDirectionDeg(180.0, 90.0)));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 270.0f,
      static_cast<float>(skyprint::relativeDirectionDeg(0.0, 90.0)));
}

void testFormatting() {
  Aircraft aircraft;
  aircraft.hasGroundspeed = true;
  aircraft.groundspeedKt = 421.6;
  TEST_ASSERT_EQUAL_STRING("12,500 FT", skyprint::formatAltitude(12500).c_str());
  TEST_ASSERT_EQUAL_STRING("422 KT",
                           skyprint::formatGroundspeed(aircraft).c_str());
  TEST_ASSERT_EQUAL_STRING("4.8 NM", skyprint::formatDistance(4.75).c_str());
  TEST_ASSERT_EQUAL_STRING("072 ENE", skyprint::formatBearing(72.0).c_str());
  TEST_ASSERT_EQUAL_STRING("000", skyprint::formatHeading(359.7).c_str());

  aircraft.hasVerticalRate = true;
  aircraft.verticalRateFpm = 1234.0;
  TEST_ASSERT_EQUAL_STRING("+1,200",
                           skyprint::formatVerticalRate(aircraft).c_str());
  aircraft.verticalRateFpm = -860.0;
  TEST_ASSERT_EQUAL_STRING("-900",
                           skyprint::formatVerticalRate(aircraft).c_str());
  aircraft.verticalRateFpm = 150.0;
  TEST_ASSERT_EQUAL_STRING("0",
                           skyprint::formatVerticalRate(aircraft).c_str());
}

void testJsonParsingFiltersAndFallbacks() {
  const char json[] = R"json({"ac":[
    {"hex":"abc123","flight":" tst42 ","r":"n123ab","t":"a320",
     "desc":"Airbus A320","lat":0.0,"lon":0.1,"alt_baro":12000,
     "baro_rate":1234,"geom_rate":999,"gs":421.5,"true_heading":361.0,
     "mag_heading":44,"track":90,"seen_pos":1.5},
    {"hex":"ground","lat":0,"lon":0.05,"alt_baro":"ground",
     "alt_geom":125,"seen_pos":1},
    {"hex":"stale","lat":0,"lon":0.05,"alt_baro":1000,"seen_pos":31},
    {"hex":"missing","flight":"","lat":0,"lon":0.2,"alt_geom":5000,
     "track":270,"seen":2}
  ]})json";
  std::vector<Aircraft> parsed;
  std::string error;
  TEST_ASSERT_TRUE(skyprint::parseAircraftJson(
      json, sizeof(json) - 1, 0.0, 0.0, 25.0, parsed, error));
  TEST_ASSERT_EQUAL_UINT32(2, parsed.size());
  TEST_ASSERT_EQUAL_STRING("ABC123", parsed[0].hex.c_str());
  TEST_ASSERT_EQUAL_STRING("TST42", parsed[0].callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("N123AB", parsed[0].registration.c_str());
  TEST_ASSERT_TRUE(parsed[0].hasVerticalRate);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1234.0f,
                           static_cast<float>(parsed[0].verticalRateFpm));
  TEST_ASSERT_EQUAL(HeadingSource::kTrueHeading, parsed[0].headingSource);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f,
                           static_cast<float>(parsed[0].headingDeg));
  TEST_ASSERT_EQUAL_STRING("NO CALLSIGN", parsed[1].callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("MISSING", parsed[1].registration.c_str());
  TEST_ASSERT_EQUAL_STRING("TYPE UNKNOWN", parsed[1].typeCode.c_str());
  TEST_ASSERT_EQUAL(HeadingSource::kTrack, parsed[1].headingSource);
}

void testJsonRejectsMalformedResponse() {
  std::vector<Aircraft> parsed;
  std::string error;
  const char json[] = "{\"msg\":\"no aircraft key\"}";
  TEST_ASSERT_FALSE(skyprint::parseAircraftJson(
      json, sizeof(json) - 1, 0.0, 0.0, 25.0, parsed, error));
  TEST_ASSERT_FALSE(error.empty());
}

void testHeadingFallsBackToMagneticBeforeTrack() {
  const char json[] = R"json({"ac":[{
    "hex":"abc999","lat":0,"lon":0.1,"alt_baro":8000,"seen_pos":1,
    "mag_heading":181,"track":270
  }]})json";
  std::vector<Aircraft> parsed;
  std::string error;
  TEST_ASSERT_TRUE(skyprint::parseAircraftJson(
      json, sizeof(json) - 1, 0.0, 0.0, 25.0, parsed, error));
  TEST_ASSERT_EQUAL_UINT32(1, parsed.size());
  TEST_ASSERT_EQUAL(HeadingSource::kMagneticHeading,
                    parsed[0].headingSource);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 181.0f,
                           static_cast<float>(parsed[0].headingDeg));
}

void testSelectorRequiresStableMeaningfulChallenger() {
  AircraftSelector selector;
  TEST_ASSERT_EQUAL(SelectionEvent::kSelected,
                    selector.ingestSuccessfulScan({makeAircraft("A", 10.0)}));
  TEST_ASSERT_EQUAL(
      SelectionEvent::kUpdated,
      selector.ingestSuccessfulScan(
          {makeAircraft("A", 10.0), makeAircraft("B", 7.5)}));
  TEST_ASSERT_EQUAL_STRING("A", selector.current()->hex.c_str());
  TEST_ASSERT_EQUAL(
      SelectionEvent::kSwitched,
      selector.ingestSuccessfulScan(
          {makeAircraft("A", 10.0), makeAircraft("B", 7.4)}));
  TEST_ASSERT_EQUAL_STRING("B", selector.current()->hex.c_str());
}

void testSelectorDoesNotSwitchForMarginalLead() {
  AircraftSelector selector;
  selector.ingestSuccessfulScan({makeAircraft("A", 10.0)});
  for (int index = 0; index < 3; ++index) {
    selector.ingestSuccessfulScan(
        {makeAircraft("A", 10.0), makeAircraft("B", 8.1)});
  }
  TEST_ASSERT_EQUAL_STRING("A", selector.current()->hex.c_str());
}

void testSelectorWaitsForTwoMissingScans() {
  AircraftSelector selector;
  selector.ingestSuccessfulScan({makeAircraft("A", 3.0)});
  TEST_ASSERT_EQUAL(SelectionEvent::kNone,
                    selector.ingestSuccessfulScan({makeAircraft("B", 2.0)}));
  TEST_ASSERT_EQUAL_STRING("A", selector.current()->hex.c_str());
  TEST_ASSERT_EQUAL(SelectionEvent::kSwitched,
                    selector.ingestSuccessfulScan({makeAircraft("B", 1.5)}));
  TEST_ASSERT_EQUAL_STRING("B", selector.current()->hex.c_str());
}

void testSelectorClearsAfterTwoEmptyScans() {
  AircraftSelector selector;
  selector.ingestSuccessfulScan({makeAircraft("A", 3.0)});
  TEST_ASSERT_EQUAL(SelectionEvent::kNone, selector.ingestSuccessfulScan({}));
  TEST_ASSERT_TRUE(selector.hasCurrent());
  TEST_ASSERT_EQUAL(SelectionEvent::kCleared,
                    selector.ingestSuccessfulScan({}));
  TEST_ASSERT_FALSE(selector.hasCurrent());
}

void testDistanceTrendAndClosestPointHold() {
  AircraftSelector selector;
  selector.ingestSuccessfulScan({makeAircraft("A", 5.0)});
  TEST_ASSERT_EQUAL(DistanceTrend::kUnknown,
                    selector.current()->distanceTrend);

  selector.ingestSuccessfulScan({makeAircraft("A", 4.0)});
  TEST_ASSERT_EQUAL(DistanceTrend::kClosing,
                    selector.current()->distanceTrend);
  TEST_ASSERT_EQUAL_STRING(
      "CLOSING", skyprint::distanceTrendLabel(selector.current()->distanceTrend));

  // A small change inside the deadband keeps the established state stable.
  selector.ingestSuccessfulScan({makeAircraft("A", 3.98)});
  TEST_ASSERT_EQUAL(DistanceTrend::kClosing,
                    selector.current()->distanceTrend);

  selector.ingestSuccessfulScan({makeAircraft("A", 4.2)});
  TEST_ASSERT_EQUAL(DistanceTrend::kClosestPoint,
                    selector.current()->distanceTrend);
  selector.ingestSuccessfulScan({makeAircraft("A", 4.4)});
  TEST_ASSERT_EQUAL(DistanceTrend::kClosestPoint,
                    selector.current()->distanceTrend);
  selector.ingestSuccessfulScan({makeAircraft("A", 4.6)});
  TEST_ASSERT_EQUAL(DistanceTrend::kClosestPoint,
                    selector.current()->distanceTrend);
  selector.ingestSuccessfulScan({makeAircraft("A", 4.8)});
  TEST_ASSERT_EQUAL(DistanceTrend::kReceding,
                    selector.current()->distanceTrend);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(testGeometryAndBearing);
  RUN_TEST(testFormatting);
  RUN_TEST(testJsonParsingFiltersAndFallbacks);
  RUN_TEST(testJsonRejectsMalformedResponse);
  RUN_TEST(testHeadingFallsBackToMagneticBeforeTrack);
  RUN_TEST(testSelectorRequiresStableMeaningfulChallenger);
  RUN_TEST(testSelectorDoesNotSwitchForMarginalLead);
  RUN_TEST(testSelectorWaitsForTwoMissingScans);
  RUN_TEST(testSelectorClearsAfterTwoEmptyScans);
  RUN_TEST(testDistanceTrendAndClosestPointHold);
  return UNITY_END();
}
