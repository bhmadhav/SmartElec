#include <Ticker.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include "SmartElec.h"

Ticker smart_elec_ticker;
extern SmartElecNvram smart_nvram;
extern volatile boolean port_status_changed[]; // For MQTT

// There are multiple switches to control. There are input and output pins
int SmartElecOutputPins[] = { D0, D2, D1 };
int SmartElecInputPins[]  = { D3, D6, D7 };
//int SmartElecOutputPins[] = { D1, D1, D1 };
//int SmartElecInputPins[]  = { D3, D3, D3 };

// Existing #defines HIGH and LOW are used in the code
// HIGH is ON and LOW is OFF

// variable array that stores the current pin state
int SmartElecSwitchState[SmartElecNumSwitches];
int InputPinCurrentState[SmartElecNumSwitches];

// Smart Elec regulator level
// level (1-10), 10 = full-on (no regulation)
int SmartElecRegulatorLevel = SMARTELEC_UNIT_LEVEL_MAX;

// set the switch to a particular value
void SetSwitchState (int port, int val)
{
  int output_pin;
  if (port >= SmartElecNumSwitches)
    return;
  // deal with the switch plus level port differently
  output_pin = SmartElecOutputPins[port];
  if (port == SMARTELEC_UNIT_LEVEL_SWITCH_NUM)
  {
    if (val == LOW)
    {
      // stop regulator logic first
      //stop_regulator();
      // set the output pin
      digitalWrite(output_pin, LOW);
      SmartElecSwitchState[port] = LOW;
      port_status_changed[port] = true;
    }
    else if (val == HIGH)
    {
#if 0
      // control regulation logic first
      if (SmartElecRegulatorLevel < SMARTELEC_UNIT_LEVEL_MAX)
        start_regulator(SmartElecRegulatorLevel);
      else
        stop_regulator();
#endif
      // set the output pin
      digitalWrite(output_pin, HIGH);
      SmartElecSwitchState[port] = HIGH;
      port_status_changed[port] = true;
    }
  }
  else
  {
    if (val == LOW)
    {
      digitalWrite(output_pin, LOW); // high is high; low is low
      SmartElecSwitchState[port] = LOW;
      port_status_changed[port] = true;
    }
    else if (val == HIGH)
    {
      digitalWrite(output_pin, HIGH); // high is high; low is low
      SmartElecSwitchState[port] = HIGH;
      port_status_changed[port] = true;
    }
  }
}

// return the current color of LED.
int GetSwitchState (int port)
{
  if (port >= SmartElecNumSwitches)
    return -1;

  return SmartElecSwitchState[port];
}

#define TimerInterruptVal    100   // ticker value in milli-seconds

void smart_elec_ticker_callback()
{
  int i, val;

  // process the input pins state
  for (i = 0; i < SmartElecNumSwitches; i++)
  {
    val = digitalRead(SmartElecInputPins[i]);
    if (val == 0)
    {
      if (InputPinCurrentState[i] != HIGH)
      {
        SetSwitchState (i, HIGH);
        InputPinCurrentState[i] = HIGH;
        smart_nvram.unit[i].unit_state = HIGH;
        smart_elec_write_nvram(&smart_nvram);
      }
    }
    else
    {
      if (InputPinCurrentState[i] != LOW)
      {
        SetSwitchState (i, LOW);
        InputPinCurrentState[i] = LOW;
        smart_nvram.unit[i].unit_state = LOW;
        smart_elec_write_nvram(&smart_nvram);
      }
    }
  }
}

void setupInputPinTimerCheck()
{
  // Initialize and Enable the timer interrupt
  smart_elec_ticker.attach_ms(TimerInterruptVal, smart_elec_ticker_callback);  // attaches callback() as a timer overflow interrupt
}

void smart_elec_setup()
{
  int i;

  // Configure the appropriate input and output pins
  pinMode(A0, INPUT);        // Input from ACS712
  pinMode(D0, OUTPUT);       // controls the regulator output
  pinMode(D1, OUTPUT);       // controls the relay port 1
  pinMode(D2, OUTPUT);       // controls the relay port 2
  pinMode(D5, INPUT);        // Interrupt input pin for zero cross detect
  pinMode(D6, INPUT);        // Input from switch 1
  pinMode(D7, INPUT);        // Input from switch 2
  pinMode(D3, INPUT_PULLUP); // Input from switch 3

  // initiatize the local state variables
  // assumption here is the NVRAM already has valid values
  // initialize the regulator level parameter
  SmartElecRegulatorLevel = smart_nvram.unit[SMARTELEC_UNIT_LEVEL_SWITCH_NUM].unit_level;

  // initialize all switches to OFF position and calibrate the ACS712 device
  for (i = 0; i < SmartElecNumSwitches; i++)
  {
    // set it to off state
    SetSwitchState (i, LOW);
  }
  // calibrate the ACS712 device
  delay(1000);
  calibrate_acs712();

  // check the initial state of switches and set the states accordingly
  for (i = 0; i < SmartElecNumSwitches; i++)
  {
    SmartElecSwitchState[i] = LOW;
    if (digitalRead(SmartElecInputPins[i]) == 0)
    {
      InputPinCurrentState[i] = HIGH;
      Serial.print("Initial State of Switch ");
      Serial.print(i);
      Serial.println(": ON");
    }
    else
    {
      InputPinCurrentState[i] = LOW;
      Serial.print("Initial State of Switch ");
      Serial.print(i);
      Serial.println(": OFF");
    }
    // set it to the same state as saved in nvram
    SetSwitchState (i, smart_nvram.unit[i].unit_state);
  }

  // Call to setup the interrupt to check the input pins
  setupInputPinTimerCheck();
}

void print_smart_elec_nvram()
{
  if (smart_nvram.valid != SMARTELEC_VALID_VALUE)
  {
    Serial.println("EEPROM does not have a valid value");
    return;
  }
  Serial.println(smart_nvram.valid);
  Serial.println(smart_nvram.device_id);
  Serial.println(smart_nvram.device_name);
  Serial.println(smart_nvram.device_type);
  Serial.println(smart_nvram.wifi_type);
  Serial.println(smart_nvram.wifi_ssid);
  Serial.println(smart_nvram.wifi_password);
}
void invalidate_smart_elec_nvram()
{
  smart_nvram.valid = 0;
  smart_nvram.wifi_type = SMARTELEC_WIFI_TYPE_INVALID;
  smart_elec_write_nvram(&smart_nvram);
  smart_elec_commit_nvram();
}

void initialize_smart_elec_nvram()
{
  smart_nvram.valid = SMARTELEC_VALID_VALUE;
  //smart_nvram.device_id = 12345678; //ESP.getChipId()
  smart_nvram.device_id = ESP.getChipId();
  //sprintf(smart_nvram.device_name, "DEV-%06X", smart_nvram.device_id);
  sprintf(smart_nvram.device_name, "DEV-%d", smart_nvram.device_id);
  smart_nvram.device_type = 0;
  smart_nvram.num_units = SmartElecNumSwitches;
  smart_nvram.wifi_type = SMARTELEC_WIFI_TYPE_INVALID;
  for (int i = 0; i < smart_nvram.num_units; i++)
  {
    sprintf(smart_nvram.unit[i].unit_name, "Unit-%d", i + 1);
    smart_nvram.unit[i].unit_type = SMARTELEC_UNIT_TYPE_SWITCH_ONLY;
    smart_nvram.unit[i].unit_state = LOW;
    smart_nvram.unit[i].unit_level = SMARTELEC_UNIT_LEVEL_MAX;
  }
  // the first unit is a switch plus level unit
  smart_nvram.unit[SMARTELEC_UNIT_LEVEL_SWITCH_NUM].unit_type = SMARTELEC_UNIT_TYPE_SWITCH_AND_LEVEL;
  print_smart_elec_nvram();
  smart_elec_write_nvram(&smart_nvram);
  smart_elec_commit_nvram();
}

void set_device_name(DynamicJsonDocument doc)
{
  strcpy(smart_nvram.device_name, doc["device_name"]);
  smart_elec_write_nvram(&smart_nvram);
}

void set_unit_name(DynamicJsonDocument doc, int unit_index)
{
  strcpy(smart_nvram.unit[unit_index].unit_name, doc["unit_name"]);
  smart_elec_write_nvram(&smart_nvram);
}

void set_unit_state(DynamicJsonDocument doc, int unit_index)
{
  if (strcmp(doc["unit_state"], "ON") == 0)
  {
    SetSwitchState(unit_index, HIGH);
    smart_nvram.unit[unit_index].unit_state = HIGH;
  }
  else
  {
    SetSwitchState(unit_index, LOW);
    smart_nvram.unit[unit_index].unit_state = LOW;
  }
  smart_elec_write_nvram(&smart_nvram);
}

void set_unit_level(DynamicJsonDocument doc, int unit_index)
{
  smart_nvram.unit[unit_index].unit_level = doc["unit_level"];
  // start regulation logic here
  SmartElecRegulatorLevel = smart_nvram.unit[unit_index].unit_level;
  // just set the switch state according to its current value
  // level trigger will get started or stopped within that logic
  SetSwitchState (SMARTELEC_UNIT_LEVEL_SWITCH_NUM, GetSwitchState(SMARTELEC_UNIT_LEVEL_SWITCH_NUM));
  // save in nvram
  smart_elec_write_nvram(&smart_nvram);
}

void set_wifi_params(DynamicJsonDocument doc)
{
#if 0
  if (strcmp(doc["wifi_type"], "None") == 0)
    smart_nvram.wifi_type = SMARTELEC_WIFI_TYPE_NONE;
  else if (strcmp(doc["wifi_type"], "WPA2") == 0)
    smart_nvram.wifi_type = SMARTELEC_WIFI_TYPE_WPA2;
  else if (strcmp(doc["wifi_type"], "WEP") == 0)
    smart_nvram.wifi_type = SMARTELEC_WIFI_TYPE_WEP;
  else
    smart_nvram.wifi_type = SMARTELEC_WIFI_TYPE_INVALID;
#endif
  smart_nvram.wifi_type = doc["wifi_type"];
  strcpy(smart_nvram.wifi_ssid, doc["SSID"]);
  strcpy(smart_nvram.wifi_password, doc["password"]);
  print_smart_elec_nvram();
  smart_elec_write_nvram(&smart_nvram);
}

void process_smart_elec_command(WiFiClient client, int http_request, int smart_elec_cmd, int unit_index)
{
  DynamicJsonDocument doc(1024);
  char endOfHeaders[] = "\r\n\r\n";

#if 0
  // read/ignore the rest of the request
  while (client.available()) {
    client.read();
  }
#endif

  switch (smart_elec_cmd)
  {
    case SMART_ELEC_CMD_DEVICE:
      {
        if (http_request == HTTP_GET)
        {
          Serial.println(F("GET DEVICE Smart Elec command received"));
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          // Send the response to the client
          client.print(F(http_200_response));
          doc["device_id"] = smart_nvram.device_id;
          doc["device_name"] = smart_nvram.device_name;
          doc["num_units"] = smart_nvram.num_units;
          doc["firmware_version"] = "1.0.0";
          doc["uptime"] = "0 Days, 8 Hours, 7 Minutes, 19 Seconds";
          serializeJsonPretty(doc, Serial);
          Serial.println();
          serializeJson(doc, client);
          client.println();
        }
        else
        {
          Serial.println(F("POST DEVICE Smart Elec command received"));
          if (!client.find(endOfHeaders))
          {
            Serial.println(F("POST Request does not have a body. Return 400 ERROR"));
            client.print(F(http_400_response));
            return;
          }
          // Allocate the JSON document
          // Use arduinojson.org/v6/assistant to compute the capacity.
          //const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2) + 60;
          //DynamicJsonDocument doc(capacity);
          // Parse JSON object
          DeserializationError error = deserializeJson(doc, client);
          if (error)
          {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            client.print(F(http_400_response));
            return;
          }
          serializeJsonPretty(doc, Serial);
          Serial.println();
          set_device_name(doc);
          client.print(F(http_200_response));
        }
      }
      break;

    case SMART_ELEC_CMD_WIFI:
      {
        if (http_request == HTTP_GET)
        {
          Serial.println(F("GET WIFI Smart Elec command received"));
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          // Send the response to the client
          client.print(F(http_200_response));
#if 0
          if (smart_nvram.wifi_type == SMARTELEC_WIFI_TYPE_INVALID)
            doc["wifi_type"] = "Invalid";
          else if (smart_nvram.wifi_type == SMARTELEC_WIFI_TYPE_NONE)
            doc["wifi_type"] = "None";
          else if (smart_nvram.wifi_type == SMARTELEC_WIFI_TYPE_WPA2)
            doc["wifi_type"] = "WPA2";
          else if (smart_nvram.wifi_type == SMARTELEC_WIFI_TYPE_WEP)
            doc["wifi_type"] = "WEP";
#endif
          doc["wifi_type"] = smart_nvram.wifi_type;
          doc["SSID"] = smart_nvram.wifi_ssid;
          doc["password"] = smart_nvram.wifi_password;
          serializeJsonPretty(doc, Serial);
          Serial.println();
          serializeJson(doc, client);
          client.println();
        }
        else
        {
          Serial.println(F("POST WIFI Smart Elec command received"));
          if (!client.find(endOfHeaders))
          {
            Serial.println(F("POST Request does not have a body. Return 400 ERROR"));
            client.print(F(http_400_response));
            return;
          }
          // Allocate the JSON document
          // Use arduinojson.org/v6/assistant to compute the capacity.
          //const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2) + 60;
          //DynamicJsonDocument doc(capacity);
          // Parse JSON object
          DeserializationError error = deserializeJson(doc, client);
          if (error)
          {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            client.print(F(http_400_response));
            return;
          }
          serializeJsonPretty(doc, Serial);
          Serial.println();
          set_wifi_params(doc);
          client.print(F(http_200_response));
        }
      }
      break;

    case SMART_ELEC_CMD_RESET:
      {
        // read/ignore the rest of the request
        while (client.available()) {
          client.read();
        }
        if (http_request == HTTP_GET)
        {
          Serial.println(F("GET RESET Smart Elec command not accepted. Return 400 ERROR"));
          client.print(F(http_400_response));
          return;
        }
        else
        {
          smart_elec_commit_nvram(); // commit to nvram before resetting the device
          Serial.println(F("POST RESET Smart Elec command received"));
          client.print(F(http_200_response));
          client.stop();
          Serial.println(F("Disconnecting from client"));
          Serial.println(F("Going to reset the device"));
          delay(1000);
          ESP.restart();
        }
      }
      break;

    case SMART_ELEC_CMD_FACTORY_RESET:
      {
        // read/ignore the rest of the request
        while (client.available()) {
          client.read();
        }
        if (http_request == HTTP_GET)
        {
          Serial.println(F("GET FACTORY RESET Smart Elec command not accepted. Return 400 ERROR"));
          client.print(F(http_400_response));
          return;
        }
        else
        {
          Serial.println(F("POST FACTORY RESET Smart Elec command received"));
          invalidate_smart_elec_nvram();
          client.print(F(http_200_response));
        }
      }
      break;

    case SMART_ELEC_CMD_UNIT_NAME:
      {
        if ((unit_index < 0) || (unit_index >= SmartElecNumSwitches))
        {
          Serial.println(F("UNIT NAME Smart Elec command not accepted. Return 400 ERROR"));
          Serial.print(F("Incorrect UNIT Number in command: "));
          Serial.println(unit_index);
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          client.print(F(http_400_response));
          return;
        }
        if (http_request == HTTP_GET)
        {
          Serial.println(F("GET UNIT NAME Smart Elec command received"));
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          // Send the response to the client
          client.print(F(http_200_response));
          doc["unit_name"] = smart_nvram.unit[unit_index].unit_name;
          serializeJsonPretty(doc, Serial);
          Serial.println();
          serializeJson(doc, client);
          client.println();
        }
        else
        {
          Serial.println(F("POST UNIT NAME Smart Elec command received"));
          if (!client.find(endOfHeaders))
          {
            Serial.println(F("POST Request does not have a body. Return 400 ERROR"));
            client.print(F(http_400_response));
            return;
          }
          // Allocate the JSON document
          // Use arduinojson.org/v6/assistant to compute the capacity.
          //const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2) + 60;
          //DynamicJsonDocument doc(capacity);
          // Parse JSON object
          DeserializationError error = deserializeJson(doc, client);
          if (error)
          {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            client.print(F(http_400_response));
            return;
          }
          serializeJsonPretty(doc, Serial);
          Serial.println();
          set_unit_name(doc, unit_index);
          client.print(F(http_200_response));
        }
      }
      break;

    case SMART_ELEC_CMD_UNIT_STATE:
      {
        if ((unit_index < 0) || (unit_index >= SmartElecNumSwitches))
        {
          Serial.println(F("UNIT STATE Smart Elec command not accepted. Return 400 ERROR"));
          Serial.print(F("Incorrect UNIT Number in command: "));
          Serial.println(unit_index);
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          client.print(F(http_400_response));
          return;
        }
        if (http_request == HTTP_GET)
        {
          Serial.println(F("GET UNIT STATE Smart Elec command received"));
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          // Send the response to the client
          client.print(F(http_200_response));
          if (smart_nvram.unit[unit_index].unit_state == HIGH)
            doc["unit_state"] = "ON";
          else
            doc["unit_state"] = "OFF";
          serializeJsonPretty(doc, Serial);
          Serial.println();
          serializeJson(doc, client);
          client.println();
        }
        else
        {
          Serial.println(F("POST UNIT STATE Smart Elec command received"));
          if (!client.find(endOfHeaders))
          {
            Serial.println(F("POST Request does not have a body. Return 400 ERROR"));
            client.print(F(http_400_response));
            return;
          }
          // Allocate the JSON document
          // Use arduinojson.org/v6/assistant to compute the capacity.
          //const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2) + 60;
          //DynamicJsonDocument doc(capacity);
          // Parse JSON object
          DeserializationError error = deserializeJson(doc, client);
          if (error)
          {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            client.print(F(http_400_response));
            return;
          }
          serializeJsonPretty(doc, Serial);
          Serial.println();
          set_unit_state(doc, unit_index);
          client.print(F(http_200_response));
        }
      }
      break;

    case SMART_ELEC_CMD_UNIT_TYPE:
      {
        if ((unit_index < 0) || (unit_index >= SmartElecNumSwitches))
        {
          Serial.println(F("UNIT TYPE Smart Elec command not accepted. Return 400 ERROR"));
          Serial.print(F("Incorrect UNIT Number in command: "));
          Serial.println(unit_index);
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          client.print(F(http_400_response));
          return;
        }
        if (http_request == HTTP_GET)
        {
          Serial.println(F("GET UNIT TYPE Smart Elec command received"));
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          // Send the response to the client
          client.print(F(http_200_response));
          doc["unit_type"] = smart_nvram.unit[unit_index].unit_type;
          serializeJsonPretty(doc, Serial);
          Serial.println();
          serializeJson(doc, client);
          client.println();
        }
        else
        {
          Serial.println(F("POST UNIT TYPE Smart Elec command not accepted. Return 400 ERROR"));
          client.print(F(http_400_response));
          return;
        }
      }
      break;

    case SMART_ELEC_CMD_UNIT_LEVEL:
      {
        if ((unit_index < 0) || (unit_index >= SmartElecNumSwitches))
        {
          Serial.println(F("UNIT LEVEL Smart Elec command not accepted. Return 400 ERROR"));
          Serial.print(F("Incorrect UNIT Number in command: "));
          Serial.println(unit_index);
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          client.print(F(http_400_response));
          return;
        }
        // have an additional check here. this call is valid only if this unit has the feature supported
        if (smart_nvram.unit[unit_index].unit_type != SMARTELEC_UNIT_TYPE_SWITCH_AND_LEVEL)
        {
          Serial.println(F("UNIT LEVEL Smart Elec command not accepted. Return 400 ERROR"));
          Serial.print(F("Incorrect UNIT Type in command: "));
          Serial.println(smart_nvram.unit[unit_index].unit_type);
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          client.print(F(http_400_response));
          return;
        }

        if (http_request == HTTP_GET)
        {
          Serial.println(F("GET UNIT LEVEL Smart Elec command received"));
          // read/ignore the rest of the request
          while (client.available()) {
            client.read();
          }
          // Send the response to the client
          client.print(F(http_200_response));
          doc["unit_level"] = smart_nvram.unit[unit_index].unit_level;
          serializeJsonPretty(doc, Serial);
          Serial.println();
          serializeJson(doc, client);
          client.println();
        }
        else
        {
          Serial.println(F("POST UNIT LEVEL Smart Elec command received"));
          if (!client.find(endOfHeaders))
          {
            Serial.println(F("POST Request does not have a body. Return 400 ERROR"));
            client.print(F(http_400_response));
            return;
          }
          // Parse JSON object
          DeserializationError error = deserializeJson(doc, client);
          if (error)
          {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            client.print(F(http_400_response));
            return;
          }
          serializeJsonPretty(doc, Serial);
          Serial.println();
          set_unit_level(doc, unit_index);
          client.print(F(http_200_response));
        }
      }
      break;

    case SMART_ELEC_CMD_INVALID:
    default:
      Serial.println("Unknown Smart Elec command received");
      // read/ignore the rest of the request
      while (client.available()) {
        client.read();
      }
      client.print(F(http_400_response));
      break;
  }
}
