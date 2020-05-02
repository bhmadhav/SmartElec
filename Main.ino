#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>

#ifndef APSSID
#define APSSID "SMART-ELEC"
#define APPSK  "smartelec"
#endif

#define WIFI_CONNECT_TIMEOUT    300  // 5 minutes

/* Set these to your desired credentials. */
static char *wifi_ssid;
static char *wifi_password;
IPAddress local_IP(192, 168, 100, 100);
IPAddress gateway(192, 168, 100, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiClient client;
WiFiServer server(80);
SmartElecNvram smart_nvram;

Ticker led_flicker_ticker;
#define WifiAPInterruptVal    100   // ticker value in milli-seconds

void led_flicker_ticker_callback()
{
  digitalWrite(LED_BUILTIN, !(digitalRead(LED_BUILTIN))); //Toggle LED Pin
}

// frequency in milli-seconds
void start_led_flicker(int frequency)
{
  // Initialize and Enable the LED flasher
  led_flicker_ticker.attach_ms(frequency, led_flicker_ticker_callback);
}

void stop_led_flicker()
{
  led_flicker_ticker.detach();
}

void setup()
{
  char buf[20];
  int counter = 0;

  // prepare LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 0);

  Serial.begin(115200);
  delay(500);
  Serial.println();

  sprintf(buf, "SMART-ELEC-%06X", ESP.getChipId());
  smart_elec_init_nvram();

  //invalidate_smart_elec_nvram();

  smart_elec_read_nvram(&smart_nvram);
  Serial.println("Read from EEPROM: ");
  if (smart_nvram.valid != SMARTELEC_VALID_VALUE)
  {
    Serial.println("EEPROM does not have a valid value. Initializing...");
    initialize_smart_elec_nvram();
  }
  print_smart_elec_nvram();

  // Call to setup the interrupt to check the input pins
  smart_elec_setup();

  if (smart_nvram.wifi_type == SMARTELEC_WIFI_TYPE_INVALID)
  {
    // Initialize and Enable the LED flasher
    start_led_flicker(WifiAPInterruptVal);

    wifi_ssid = buf;
    wifi_password = APPSK;
    Serial.print("WIFI SSID: ");
    Serial.println(wifi_ssid);
    Serial.print("WIFI Password: ");
    Serial.println(wifi_password);
    Serial.println(F("Factory Fresh Device. Coming up as an Access Point..."));
    /* You can remove the password parameter if you want the AP to be open. */
    Serial.println(F("Setting soft-AP configuration ... "));
    WiFi.softAPConfig(local_IP, gateway, subnet);
    Serial.println(F("Configuring access point..."));
    WiFi.softAP(wifi_ssid, wifi_password);
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
  }
  else
  {
    wifi_ssid = smart_nvram.wifi_ssid;
    wifi_password = smart_nvram.wifi_password;
    Serial.print("WIFI SSID: ");
    Serial.println(wifi_ssid);
    Serial.print("WIFI Password: ");
    Serial.println(wifi_password);
    // Connect to WiFi network

    //WiFi.hostname("myesp");
    WiFi.persistent(false);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);

    while (WiFi.status() != WL_CONNECTED) {
      digitalWrite(LED_BUILTIN, 0);
      delay(500);
      digitalWrite(LED_BUILTIN, 1);
      delay(500);
      Serial.print(F("."));
      counter++;
      if (counter >= WIFI_CONNECT_TIMEOUT)
      {
        Serial.println(F("UNABLE TO CONNECT TO WIFI: INVALIDATING WIFI CONFIGURATION"));
        smart_nvram.wifi_type = SMARTELEC_WIFI_TYPE_INVALID;
        smart_elec_write_nvram(&smart_nvram);
        smart_elec_commit_nvram(); // only place we force a commit
        Serial.println(F("Going to reset the device"));
        delay(1000);
        ESP.restart();
      }
    }
    Serial.println();
    Serial.println(F("WiFi connected"));
    // Print the IP address
    Serial.println(WiFi.localIP());

    // setup metrics module
    setup_metrics();
    // setup mqtt module
    setup_mqtt();
  }

  digitalWrite(LED_BUILTIN, 1);

  // Set up mDNS responder:
  // - first argument is the domain name, in this example
  //   the fully-qualified domain name is "esp8266.local"
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  sprintf(buf, "DEV-%d", smart_nvram.device_id);
  if (!MDNS.begin(buf)) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");

  server.begin();
  Serial.println("HTTP server started");

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

}

void loop() 
{
  int http_request, smart_elec_cmd, index, unit_index = 0;

  // Update MDNS for local service discovery
  MDNS.update();

  // Process the AMP Usage and Metrics related logic
  process_metrics();

  // Process the MQTT related commands
  process_mqtt();

  //
  // Play the server role and process the client's request
  //
  client = server.available();
  if (!client) {
    return;
  }
  Serial.println(F("new client"));

  client.setTimeout(5000); // default is 1000

  // Read the first line of the request
  String req = client.readStringUntil('\r');
  Serial.println(F("request: "));
  Serial.println(req);

  // STEP 1: Identify the HTTP request type
  if (req.indexOf("GET") != -1)
    http_request = HTTP_GET;
  else if (req.indexOf("POST") != -1)
    http_request = HTTP_POST;
  else if (req.indexOf("PUT") != -1)
    http_request = HTTP_PUT;
  else
    http_request = HTTP_INVALID;

  // STEP 2: Extract the smart-elec request
  if (req.indexOf("/v1/device") != -1)
    smart_elec_cmd = SMART_ELEC_CMD_DEVICE;
  else if (req.indexOf("/v1/wifi") != -1)
    smart_elec_cmd = SMART_ELEC_CMD_WIFI;
  else if (req.indexOf("/v1/reset") != -1)
    smart_elec_cmd = SMART_ELEC_CMD_RESET;
  else if (req.indexOf("/v1/factory_reset") != -1)
    smart_elec_cmd = SMART_ELEC_CMD_FACTORY_RESET;
  else if (req.indexOf("/v1/unit/name/") != -1)
  {
    smart_elec_cmd = SMART_ELEC_CMD_UNIT_NAME;
    index = req.indexOf("/v1/unit/name/") + strlen("/v1/unit/name/");
    unit_index = atoi(&req[index]);
  }
  else if (req.indexOf("/v1/unit/type") != -1)
  {
    smart_elec_cmd = SMART_ELEC_CMD_UNIT_TYPE;
    index = req.indexOf("/v1/unit/type/") + strlen("/v1/unit/type/");
    unit_index = atoi(&req[index]);
  }
  else if (req.indexOf("/v1/unit/state") != -1)
  {
    smart_elec_cmd = SMART_ELEC_CMD_UNIT_STATE;
    index = req.indexOf("/v1/unit/state/") + strlen("/v1/unit/state/");
    unit_index = atoi(&req[index]);
  }
  else if (req.indexOf("/v1/unit/level") != -1)
  {
    smart_elec_cmd = SMART_ELEC_CMD_UNIT_LEVEL;
    index = req.indexOf("/v1/unit/level/") + strlen("/v1/unit/level/");
    unit_index = atoi(&req[index]);
  }
  else
    smart_elec_cmd = SMART_ELEC_CMD_INVALID;

  // STEP 3: Process the smart elec command
  process_smart_elec_command(client, http_request, smart_elec_cmd, unit_index - 1);
  // The rest of the processing and response happens inside this command

  // The client will actually be *flushed* then disconnected
  // when the function returns and 'client' object is destroyed (out-of-scope)
  // flush = ensure written data are received by the other side
  client.stop();
  Serial.println(F("Disconnecting from client"));

}
