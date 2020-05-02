#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <InfluxDb.h>
//#include "ACS712.h"
#include "SmartElec.h"

extern SmartElecNvram smart_nvram;

#if 0
// We have 30 amps version sensor connected to A0 pin of arduino
ACS712 sensor(ACS712_30A, A0);
#endif

#define INFLUXDB_HOST "https://us-west-2-1.aws.cloud2.influxdata.com"
//#define INFLUXDB_HOST "http://www.smart-elex.com"
Influxdb influx(INFLUXDB_HOST);

Ticker metrics_ticker;
#define SendMetricsInterruptVal    15000     // ticker value in milli-seconds
volatile boolean send_metrics_flag = false;  // Boolean to tell 'loop' to send metrics

void metrics_callback()
{
  if (send_metrics_flag == false)
  {
    send_metrics_flag = true;
  }
}

// frequency in milli-seconds
void start_metrics_timer()
{
  // Initialize and Enable the LED flasher
  metrics_ticker.attach_ms(SendMetricsInterruptVal, metrics_callback);
}

void stop_metrics_timer()
{
  metrics_ticker.detach();
}

void setup_metrics()
{
#if 0
  //influx.setDb("test");
  // Uncomment the following lines to use the v2.0 InfluxDB
  influx.setVersion(2);
  influx.setOrg("madhavan.bharath@gmail.com");
  influx.setBucket("smartelex");
  //influx.setPort(9999);
  influx.setToken("xzBozCsfbcekPcJ9Tbwq4EFXNscKuTL-QwoyLu_lKWbnZu04UXxo_0Fr_SxEiVjUk8e0A2CnJOo1RVX0XgZG-g==");
  influx.begin();
  //  Serial.println("Calibrating... Ensure that no current flows through the sensor at this moment");
  //  sensor.calibrate();
  //  Serial.println("Done!");
#endif
  start_metrics_timer();
}

long lastSample = 0;
long sampleSum = 0;
long sampleCount = 0;
long calibration = 0;
float vpc = 4.8828125;
float rms = 0;
float millivolts = 0;
float amp = 0;

void calibrate_acs712()
{
  int i;
  //Serial.println("The analog Read is " + String(analogRead(A0)));
  for (i = 0; i < 1000; i++)
  {
    sampleSum += sq(analogRead(A0));
    sampleCount++;
    delay(1);
  }

  calibration = sqrt(sampleSum / sampleCount);
  //Serial.println("The Calibration value is " + String(calibration));
  sampleSum = 0;
  sampleCount = 0;
}

void process_metrics()
{
  if (millis() > lastSample + 1)
  {
    // Serial.println("The analog Read is " + String(analogRead(A0)));
    sampleSum += sq(analogRead(A0) - calibration);
    sampleCount++;
    lastSample = millis();
  }

  // Averaging the RMS calculation
  if (sampleCount == 1000)
  {
    rms = sqrt(sampleSum / sampleCount);
    millivolts = (rms * vpc);
    // amp = (millivolts / 66);
    amp = ((rms * 30) / calibration);
    sampleSum = 0;
    sampleCount = 0;
  }

  // send the metrics to Influx Server
  if (send_metrics_flag)
  {
    report_metrics();
    send_metrics_flag = false;
  }
}

void report_metrics()
{

  const char * host = "http://www.smart-elex.com:8086/write?db=smartelex";
  WiFiClient client;
  HTTPClient https;
  char dev_id[16];

  Serial.println("connecting to server..");
  if (https.begin(client, host)) {
    //https.addHeader("Authorization", "Token xzBozCsfbcekPcJ9Tbwq4EFXNscKuTL-QwoyLu_lKWbnZu04UXxo_0Fr_SxEiVjUk8e0A2CnJOo1RVX0XgZG-g==");
    https.addHeader("Content-Type", "text/plain");

    Serial.println("The Calibration value is " + String(calibration));
    Serial.println("The RMS value is " + String(rms));
    Serial.println("The RMS voltage is " + String(millivolts));
    Serial.println("The RMS AMPERAGE is " + String(amp));

    InfluxData row("amp");
    sprintf(dev_id, "DEV-%06x", smart_nvram.device_id);
    row.addTag("id", dev_id);
    row.addValue("calib", calibration);
    row.addValue("rms", rms);
    row.addValue("mv", millivolts);
    row.addValue("val", amp);

    int httpsCode = https.POST(row.toString());
    Serial.println(row.toString());

    boolean success;
    if (httpsCode == 204) {
      success = true;
    } else {
      Serial.println("#####\nPOST FAILED\n#####");
      Serial.print(" <-- Response: ");
      String response = https.getString();
      Serial.println(" \"" + response + "\"");
      Serial.print(httpsCode);
      Serial.printf("[HTTP] POST... failed, error: %s\n", https.errorToString(httpsCode).c_str());
      success = false;
    }

  } else {
    Serial.print("failed to connect to server");
  }
  https.end();
}

#if 0
void report_metrics2()
{

  //  const char * host = "https://us-west-2-1.aws.cloud2.influxdata.com/api/v2/write?org=madhavan.bharath@gmail.com&bucket=smartElec";
  const char * host = "http://www.smart-elex.com:8086/write?db=smartelex";
  WiFiClient client;
  HTTPClient https;
  char dev_id[16];

  Serial.println("connecting to server..");
  if (https.begin(client, host)) {
    //https.addHeader("Authorization", "Token xzBozCsfbcekPcJ9Tbwq4EFXNscKuTL-QwoyLu_lKWbnZu04UXxo_0Fr_SxEiVjUk8e0A2CnJOo1RVX0XgZG-g==");
    https.addHeader("Content-Type", "text/plain");

    InfluxData row("amp");
    sprintf(dev_id, "DEV-%06x", smart_nvram.device_id);
    row.addTag("id", dev_id);
    float I = sensor.getCurrentAC();
    row.addValue("val", I);

    int httpsCode = https.POST(row.toString());
    Serial.println(row.toString());

    boolean success;
    if (httpsCode == 204) {
      success = true;
    } else {
      Serial.println("#####\nPOST FAILED\n#####");
      Serial.print(" <-- Response: ");
      String response = https.getString();
      Serial.println(" \"" + response + "\"");
      Serial.print(httpsCode);
      Serial.printf("[HTTP] POST... failed, error: %s\n", https.errorToString(httpsCode).c_str());
      success = false;
    }

  } else {
    Serial.print("failed to connect to server");
  }
  https.end();
}
#endif
