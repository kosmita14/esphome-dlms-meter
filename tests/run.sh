#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${ARDUINOJSON_INCLUDE:?Set ARDUINOJSON_INCLUDE to the ArduinoJson src directory}"
binary=$(mktemp "${TMPDIR:-/tmp}/dlms-meter-test.XXXXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Itests/stubs -Icomponents/dlms_meter \
  -I"$ARDUINOJSON_INCLUDE" tests/test_pipeline.cpp \
  components/dlms_meter/dlms_meter.cpp components/dlms_meter/obis_parser.cpp \
  components/dlms_meter/meter_sensors.cpp -lcrypto -o "$binary"
"$binary"
