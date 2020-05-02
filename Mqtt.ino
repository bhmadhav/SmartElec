#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

//#define MQTT_VERSION    MQTT_VERSION_3_1
//#define MQTT_MAX_PACKET_SIZE 512

WiFiClient client_for_mqtt;
const char* mqtt_server = "test.mosquitto.org";
//const char* mqtt_server = "broker.mqtt-dashboard.com";
PubSubClient mqtt_client(client_for_mqtt);

volatile boolean ping_resp_pending = false;  // Flag set to send ping response
volatile boolean info_req_pending = false;  // Flag set to send device info message
volatile boolean port_status_changed[SmartElecNumSwitches]; // Flag set to indicate port status change

#define MAX_STRING_LEN 64
// Function to return a substring defined by a delimiter at an index
char* subStr (char* str, char *delim, int index)
{
  char *act, *sub, *ptr;
  static char copy[MAX_STRING_LEN];
  int i;

  // Since strtok consumes the first arg, make a copy
  strcpy(copy, str);

  for (i = 1, act = copy; i <= index; i++, act = NULL)
  {
    //Serial.print(".");
    sub = strtok_r(act, delim, &ptr);
    if (sub == NULL)
      break;
  }
  return sub;

}

void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
  char dev_id_topic[16];
  char sub_msg[MQTT_MAX_PACKET_SIZE];
  char *unit_str;
  int unit_index;

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    sub_msg[i] = (char)payload[i];
  }
  sub_msg[length] = NULL;
  Serial.print((char *)sub_msg);
  Serial.println();
  
  // validate if this message is addressed to this device
  sprintf(dev_id_topic, "SE-%06x", smart_nvram.device_id);

  if (strcmp (dev_id_topic, subStr(topic, "/", 1)) != 0)
  {
    Serial.println("Message is not for this device. Ignore...");
    return;
  }

  // Topic: "SE-xxxxxx/ping":
  if (strcmp ("ping", subStr(topic, "/", 2)) == 0)
  {
    Serial.println("Received PING request. Setting flag to send PING response");
    ping_resp_pending = true;
    return;
  }

  // Topic: “SE-xxxxxx/info_req”:
  if (strcmp ("info_req", subStr(topic, "/", 2)) == 0)
  {
    Serial.println("Received INFO request. Setting flag to send DEVICE INFO message");
    info_req_pending = true;
    return;
  }

  // Topic: “SE-xxxxxx/reset”:
  if (strcmp ("reset", subStr(topic, "/", 2)) == 0)
  {
    Serial.println("Received RESET from MQTT. Resetting device");
    delay(1000);
    ESP.restart();
    return;
  }

  // Topic: “SE-xxxxxx/wifi”:
  if (strcmp ("wifi", subStr(topic, "/", 2)) == 0)
  {
    DynamicJsonDocument doc(MQTT_MAX_PACKET_SIZE);
    Serial.println("Received WIFI config from MQTT. Saving WIFI params");
    DeserializationError error = deserializeJson(doc, sub_msg);
    smart_nvram.wifi_type = doc["wifi_type"];
    strcpy(smart_nvram.wifi_ssid, doc["SSID"]);
    strcpy(smart_nvram.wifi_password, doc["password"]);
    print_smart_elec_nvram();
    smart_elec_write_nvram(&smart_nvram);
    return;
  }

  // Topic: “SE-xxxxxx/dev_name”:
  if (strcmp ("dev_name", subStr(topic, "/", 2)) == 0)
  {
    Serial.println("Received dev_name change from MQTT. Saving settings");
    strcpy(smart_nvram.device_name, (char *)sub_msg);
    smart_elec_write_nvram(&smart_nvram);
    return;
  }

  // Topic: “SE-xxxxxx/unit_name/<unit>”:
  if (strcmp ("unit_name", subStr(topic, "/", 2)) == 0)
  {
    Serial.println("Received unit_name change from MQTT. Saving settings");
    // find out the unit ID from the topic
    unit_str = subStr(topic, "/", 3);
    unit_index = atoi(unit_str);
    if ((unit_index <= 0) || (unit_index > SmartElecNumSwitches))
    {
      Serial.println("ERROR: Invalid unit number in topic. Ignore msg and return");
      return;
    }
    unit_index--; // make it zero based
    strcpy(smart_nvram.unit[unit_index].unit_name, (char *)sub_msg);
    smart_elec_write_nvram(&smart_nvram);
    return;
  }

  // Topic: “SE-xxxxxx/unit_state/<unit>”:
  if (strcmp ("unit_state", subStr(topic, "/", 2)) == 0)
  {
    Serial.println("Received unit_state change from MQTT. Turn the particular switch ON or OFF");
    // find out the unit ID from the topic
    unit_str = subStr(topic, "/", 3);
    unit_index = atoi(unit_str);
    if ((unit_index <= 0) || (unit_index > SmartElecNumSwitches))
    {
      Serial.println("ERROR: Invalid unit number in topic. Ignore msg and return");
      return;
    }
    unit_index--; // make it zero based
    if (strcmp((char *)sub_msg, "ON") == 0)
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
    return;
  }

  // Topic: “SE-xxxxxx/unit_level/<unit>”:
  if (strcmp ("unit_level", subStr(topic, "/", 2)) == 0)
  {
    Serial.println("Received unit_level change from MQTT. Saving settings");
    // find out the unit ID from the topic
    unit_str = subStr(topic, "/", 3);
    unit_index = atoi(unit_str);
    if ((unit_index <= 0) || (unit_index > SmartElecNumSwitches))
    {
      Serial.println("ERROR: Invalid unit number in topic. Ignore msg and return");
      return;
    }
    unit_index--; // make it zero based
    smart_nvram.unit[unit_index].unit_level = atoi((char *)sub_msg);
    // start regulation logic here
    SmartElecRegulatorLevel = smart_nvram.unit[unit_index].unit_level;
    // just set the switch state according to its current value
    // level trigger will get started or stopped within that logic
    SetSwitchState (SMARTELEC_UNIT_LEVEL_SWITCH_NUM, GetSwitchState(SMARTELEC_UNIT_LEVEL_SWITCH_NUM));
    smart_elec_write_nvram(&smart_nvram);
    return;
  }
}

void mqtt_subscribe()
{
  char subscribe_topic[32];

  // Topic: “SE-xxxxxx/ping”:
  sprintf(subscribe_topic, "SE-%06x/ping", smart_nvram.device_id);
  Serial.println(subscribe_topic);
  mqtt_client.subscribe(subscribe_topic);

  // Topic: “SE-xxxxxx/info_req”:
  sprintf(subscribe_topic, "SE-%06x/info_req", smart_nvram.device_id);
  Serial.println(subscribe_topic);
  mqtt_client.subscribe(subscribe_topic);

  // Topic: “SE-xxxxxx/reset”:
  sprintf(subscribe_topic, "SE-%06x/reset", smart_nvram.device_id);
  Serial.println(subscribe_topic);
  mqtt_client.subscribe(subscribe_topic);

  // Topic: “SE-xxxxxx/wifi”:
  sprintf(subscribe_topic, "SE-%06x/wifi", smart_nvram.device_id);
  Serial.println(subscribe_topic);
  mqtt_client.subscribe(subscribe_topic);

  // Topic: “SE-xxxxxx/dev_name”:
  sprintf(subscribe_topic, "SE-%06x/dev_name", smart_nvram.device_id);
  Serial.println(subscribe_topic);
  mqtt_client.subscribe(subscribe_topic);

  // Topics: “unit_name, unit_state, unit_level”:
  for (int i = 0; i < SmartElecNumSwitches; i++)
  {
    sprintf(subscribe_topic, "SE-%06x/unit_name/%d", smart_nvram.device_id, i + 1);
    Serial.println(subscribe_topic);
    mqtt_client.subscribe(subscribe_topic);

    sprintf(subscribe_topic, "SE-%06x/unit_state/%d", smart_nvram.device_id, i + 1);
    Serial.println(subscribe_topic);
    mqtt_client.subscribe(subscribe_topic);

    if (smart_nvram.unit[i].unit_type == SMARTELEC_UNIT_TYPE_SWITCH_AND_LEVEL)
    {
      sprintf(subscribe_topic, "SE-%06x/unit_level/%d", smart_nvram.device_id, i + 1);
      Serial.println(subscribe_topic);
      mqtt_client.subscribe(subscribe_topic);
    }
  }
}

void mqtt_reconnect()
{
  // Loop until we're reconnected
  while (!mqtt_client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqtt_client.connect(clientId.c_str()))
    {
      Serial.println("connected");
      mqtt_subscribe();
    } else
    {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup_mqtt()
{
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(mqtt_callback);
  for (int i = 0; i < SmartElecNumSwitches; i++)
    port_status_changed[i] = false;
}

void process_mqtt()
{
  if (!mqtt_client.connected())
  {
    mqtt_reconnect();
  }
  mqtt_client.loop();

  char publish_topic[32];
  char ping_resp[16];

  // Publish PING response message
  if (ping_resp_pending)
  {
    sprintf(publish_topic, "SE-%06x/ping_resp", smart_nvram.device_id);
    Serial.print("MQTT Publish on topic: ");
    Serial.println(publish_topic);
    sprintf(ping_resp, "amp: %.2f", amp);
    mqtt_client.publish(publish_topic, ping_resp);
    ping_resp_pending = false;
  }

  // Publish DEV_INFO, WIFI_INFO, UNIT_INFO messages
  if (info_req_pending)
  {
    mqtt_publish_dev_info();
    mqtt_publish_wifi_info();
    for (int i = 0; i < SmartElecNumSwitches; i++)
      mqtt_publish_unit_info(i);
    info_req_pending = false;
  }

  // Check if any port status has changed
  // Publish UNIT_INFO message
  for (int i = 0; i < SmartElecNumSwitches; i++)
  {
    if (port_status_changed[i])
    {
      mqtt_publish_unit_info(i);
      port_status_changed[i] = false;
    }
  }
}

void mqtt_publish_dev_info()
{
  char publish_topic[32];
  DynamicJsonDocument doc(MQTT_MAX_PACKET_SIZE);
  char pub_msg[MQTT_MAX_PACKET_SIZE];

  sprintf(publish_topic, "SE-%06x/dev_info", smart_nvram.device_id);
  Serial.print("MQTT Publish on topic: ");
  Serial.println(publish_topic);
  doc["device_id"] = smart_nvram.device_id;
  doc["device_name"] = smart_nvram.device_name;
  doc["num_units"] = smart_nvram.num_units;
  doc["firmware_version"] = "1.0.0";
  //doc["uptime"] = "0 Days, 8 Hours, 7 Minutes, 19 Seconds";
  serializeJsonPretty(doc, Serial);
  Serial.println();
  serializeJson(doc, pub_msg);
  mqtt_client.publish(publish_topic, pub_msg);
}

void mqtt_publish_wifi_info()
{
  char publish_topic[32];
  DynamicJsonDocument doc(MQTT_MAX_PACKET_SIZE);
  char pub_msg[MQTT_MAX_PACKET_SIZE];

  sprintf(publish_topic, "SE-%06x/wifi_info", smart_nvram.device_id);
  Serial.print("MQTT Publish on topic: ");
  Serial.println(publish_topic);
  doc["wifi_type"] = smart_nvram.wifi_type;
  doc["SSID"] = smart_nvram.wifi_ssid;
  doc["password"] = smart_nvram.wifi_password;
  serializeJsonPretty(doc, Serial);
  Serial.println();
  serializeJson(doc, pub_msg);
  mqtt_client.publish(publish_topic, pub_msg);
}

void mqtt_publish_unit_info(int unit)
{
  char publish_topic[32];
  DynamicJsonDocument doc(MQTT_MAX_PACKET_SIZE);
  char pub_msg[MQTT_MAX_PACKET_SIZE];

  sprintf(publish_topic, "SE-%06x/unit_info", smart_nvram.device_id);
  Serial.print("MQTT Publish on topic: ");
  Serial.println(publish_topic);
  doc["id"] = unit + 1;
  doc["name"] = smart_nvram.unit[unit].unit_name;
  doc["type"] = smart_nvram.unit[unit].unit_type;
  doc["state"] = smart_nvram.unit[unit].unit_state;
  doc["level"] = smart_nvram.unit[unit].unit_level;
  serializeJsonPretty(doc, Serial);
  Serial.println();
  serializeJson(doc, pub_msg);
  mqtt_client.publish(publish_topic, pub_msg);
}
