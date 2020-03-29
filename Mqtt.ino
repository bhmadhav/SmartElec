#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

void mqtt_callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void mqtt_reconnect() 
{
  // Loop until we're reconnected
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqtt_client.connect("mbtestClient")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      mqtt_client.publish("mbtest/port1/stat","hello world");
      // ... and resubscribe
      mqtt_client.subscribe("mbtest/port1/ctrl");
      mqtt_client.subscribe("mbtest/port2/ctrl");
      mqtt_client.subscribe("mbtest/port3/ctrl");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void mqtt_setup()
{
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(mqtt_callback);
}

void mqtt_loop()
{
  if (!mqtt_client.connected()) {
    mqtt_reconnect();
  }
  mqtt_client.loop();
}
