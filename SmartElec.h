// SmartElec.h

#pragma once

#ifdef __cplusplus

#define SMARTELEC_VALID_VALUE     0xA1E0E1EC

#define SMARTELEC_WIFI_TYPE_INVALID  -1
#define SMARTELEC_WIFI_TYPE_NONE     1
#define SMARTELEC_WIFI_TYPE_WPA2     2
#define SMARTELEC_WIFI_TYPE_WEP      3

#define SMARTELEC_UNIT_TYPE_SWITCH_ONLY         0
#define SMARTELEC_UNIT_TYPE_SWITCH_AND_LEVEL    1
#define SMARTELEC_UNIT_TYPE_HEAVY_APPLIANCE     2

// MIN is same as OFF and MAX is same as ON without regulation
#define SMARTELEC_UNIT_LEVEL_MIN    1
#define SMARTELEC_UNIT_LEVEL_MAX    10
#define SMARTELEC_UNIT_LEVEL_SWITCH_NUM       0 // unit 0 has the level+switch feature
#define SMARTELEC_UNIT_LEVEL_INTERRUPT_PIN    D5
#define SMARTELEC_UNIT_LEVEL_OUTPUT_PIN       D0

#define SmartElecNumSwitches      3

struct SmartElecUnit {
  char unit_name[16];
  int  unit_type;
  int  unit_state;
  int  unit_level;
};

struct SmartElecNvram {
  unsigned int valid;
  unsigned int device_id;
  char device_name[16];
  int  device_type;
  int  wifi_type;
  char wifi_ssid[32];
  char wifi_password[64];
  int  num_units;
  SmartElecUnit unit[SmartElecNumSwitches];
};

#define HTTP_INVALID   0
#define HTTP_GET       1
#define HTTP_POST      2
#define HTTP_PUT       3

#define SMART_ELEC_CMD_INVALID          0
#define SMART_ELEC_CMD_DEVICE           1
#define SMART_ELEC_CMD_WIFI             2
#define SMART_ELEC_CMD_RESET            3
#define SMART_ELEC_CMD_FACTORY_RESET    4
#define SMART_ELEC_CMD_UNIT_NAME        5
#define SMART_ELEC_CMD_UNIT_STATE       6
#define SMART_ELEC_CMD_UNIT_TYPE        7
#define SMART_ELEC_CMD_UNIT_LEVEL       8

#define http_200_response "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
#define http_400_response "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n"

#else

#error SmartElec requires a C++ compiler, please change file extension to .cc or .cpp

#endif
