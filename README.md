# Skyprint

Skyprint is an ESP32 desk display that presents the closest airborne aircraft
as a quiet monochrome identification plaque. It deliberately avoids maps,
tracks, and radar styling.

## Hardware and wiring

- Inland 38-pin ESP32 development board
- Waveshare 2.13-inch V4 black-and-white e-paper display
- 250 x 122 visible pixels, SSD1680 controller
- Stable USB power supply

Connect the display with power removed:

| Display | ESP32 |
| --- | ---: |
| DIN / MOSI | GPIO 14 |
| CLK | GPIO 27 |
| CS | GPIO 26 |
| DC | GPIO 25 |
| RST | GPIO 33 |
| BUSY | GPIO 32 |
| VCC | 3.3 V |
| GND | GND |

## Configuration

Copy the example and edit the private copy:

```sh
cp include/config.example.h include/config.h
```

Set the 2.4 GHz Wi-Fi credentials, office latitude/longitude, and change
`SKYPRINT_CONFIGURED` to `1`. `include/config.h` is ignored by git. Building
without a completed private configuration intentionally fails with a clear
compiler message.

Set `SKYPRINT_DISPLAY_HEADING_DEGREES` to the compass direction represented by
an UP arrow on the screen. Use the direction you face when looking straight
ahead over the display: north is `0`, east `90`, south `180`, and west `270`.
For example, if the display sits at an east-facing office window, use `90`.
The LOOK arrow then rotates by `aircraft bearing - display heading`, so it
points left, right, ahead, or behind relative to the physical display.

Set `SKYPRINT_INVERT_DISPLAY` to `1` for a black background with white text,
rules, logos, and direction graphics. Leave it at `0` for the standard white
background. Inverted mode changes only the palette; layout and refresh policy
stay the same. A mostly black image may make e-paper ghosting more noticeable.

The configured coordinates are sent to ADSB.lol in each radius query. ADSB.lol
is a third-party community service whose availability and access policy can
change. This firmware is intended for personal, non-commercial use.

When a new aircraft portrait is selected, its callsign and live coordinates
are sent to ADSB.lol's position-aware route endpoint. A route is displayed only
when ADSB.lol marks the callsign route as plausible for that position; rejected
or unknown routes show `N/A`. The ICAO address and callsign are also sent to the
[ADSBDB API](https://www.adsbdb.com/) for the manufacturer/full model name and
the airline/radio telephony identity. That identity replaces the generic
top-left caption—for example, `BLUE STREAK / PSA AIRLINES` for a JIA flight.
Metadata may be missing or incorrect, and lookup failures never affect aircraft
selection. Temporary failures are retried after five minutes; the caption falls
back to `NEAREST AIRBORNE` when no airline identity is available.

### Airline logos

Airline logos are selected from the first three letters of the flight callsign,
which normally contain its ICAO airline designator. Put transparent PNG source
files in `assets/logos` using names such as `AAL.png`, `DAL.png`, and `UAL.png`.
`default.png` is used for private, military, or otherwise unknown operators.

After adding or replacing PNGs, regenerate the 16 x 16 monochrome firmware
assets and rebuild:

```sh
python3 tools/generate_airline_logos.py
pio run -e esp32dev
```

The converter requires ImageMagick's `magick` command. Generated bitmap code is
checked in, so ImageMagick is not required for ordinary firmware builds.

## Build, upload, and monitor

```sh
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

## Behavior

The device scans a 25 NM radius every 20 seconds. It accepts only aircraft with
a numeric airborne altitude, a position no more than 30 seconds old, and
coordinates inside the radius. Distance and true bearing are calculated on the
ESP32.

The current portrait remains selected until the same challenger is at least 20
percent and 0.5 NM closer in two consecutive scans. A selected aircraft must be
missing from two successful scans before it is replaced or the screen changes
to Clear Sky. Network and API failures never count as missing-aircraft scans.

The plaque refreshes changing details at most once per minute. Same-aircraft
updates use fast partial refresh; identity and status changes use full refresh.
A full refresh also occurs after five partial refreshes and once every 24 hours.
Data labels use a consistent bold weight while the `NEAREST AIRBORNE` caption
and live values remain regular for quick visual separation.

- `SCANNING`: startup, clock synchronization, or no valid data for five minutes
- `CLEAR SKY`: a successful scan found no eligible aircraft
- Aircraft plaque: callsign, type/description, registration, altitude,
  groundspeed, distance, bearing, aircraft heading/track, and signed vertical
  rate. The full aircraft type sits beneath the callsign with deliberate
  spacing above and below the large identity text, while the
  registration/heading/motion row sits immediately above the metrics divider.
  The bottom metrics are ordered `SPD`, `DIST`, `ALT`, and `FPM`; bearing
  appears without a label beneath the direction roundel. A compact motion state
  immediately after the heading/track reads `CLOSING`, `CLOSEST`, or `RECEDING`
  based on successive distances from the configured location; it reads
  `TRACKING` until a trend is known. Rates within 200 FPM
  of level are presented as `0`; barometric rate is preferred with geometric
  rate as a fallback. A calibrated LOOK arrow points from the office toward the
  aircraft. The plaque also includes a compact published route block
  labeled `ROUTE` with `BNA>FLL` beneath it beside the callsign when available.
  Missing routes show `N/A`; the aircraft squawk code
  appears below a bold `SQUAWK` label. The upper-right corner shows the UTC date
  and last successful live aircraft update on one aviation-style line, such as
  `19JUL 14:37Z`.

Serial output reports the assigned IP address, signal strength, active DNS
server, Wi-Fi retries, API errors, valid aircraft counts, selection changes,
and full refreshes without printing credentials. Because the display is
continuously powered, Wi-Fi power saving is disabled for connection stability.
Cloudflare and Google DNS are configured instead of relying on a potentially
unreliable router DNS proxy. Three consecutive DNS or connection failures
trigger an automatic Wi-Fi link reset; they never count as aircraft misses.

## Hardware acceptance checklist

1. Confirm the first screen is landscape and all content fits within 250 x 122.
2. Confirm BUSY completes both full and partial updates without hanging.
3. Compare one live result with ADSB.lol for the configured point.
4. Confirm the LOOK arrow points up when aircraft bearing equals the configured
   display heading, right at heading + 90, and left at heading - 90.
5. Disconnect Wi-Fi: the last portrait should remain for five minutes, then
   change once to Scanning. Reconnect and confirm automatic recovery.
6. Temporarily use a remote location with no traffic to verify Clear Sky.
7. Observe six same-aircraft updates and confirm the sixth is a full refresh.
