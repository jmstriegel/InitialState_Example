#include <ESP8266WiFi.h>          // Onboard WiFi and HTTP client
#include <Adafruit_Sensor.h>      // Adafruit Unified Sensor Library
#include <DHT.h>                  // DHT22 Temperature Sensor
#include <DHT_U.h>
#include "config.h"

#define DHTPIN            2       // Pin which is connected to the DHT sensor.
#define DHTTYPE           DHT22   // Options: DHT11 DHT22 DHT21

DHT_Unified dht(DHTPIN, DHTTYPE);
bool wifi_connected = false;
uint32_t dhtDelayMS;
uint32_t readTime;

void WiFiEvent(WiFiEvent_t event) {
  //Serial.printf("WiFi:: event: %d\n", event);
  switch(event) {
    case WIFI_EVENT_STAMODE_GOT_IP:
      Serial.println("WiFi:: connected");
      Serial.println("WiFi:: IP address");
      Serial.println(WiFi.localIP());
      wifi_connected = true;
      break;
    case WIFI_EVENT_STAMODE_DISCONNECTED:
      Serial.println("WiFi:: lost connection");
      wifi_connected = false;
      break;
  }
}


void setup() {
  Serial.begin(115200);
  delay(100);
  
  // Init DHT22 Sensor
  dht.begin();
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  dhtDelayMS = sensor.min_delay / 1000;
  readTime = millis();

  // Init Wifi
  wifi_connected = false; 
  Serial.printf("\n\nSetup:: connecting to WiFi SSID: %s\n", WIFI_SSID);
  WiFi.disconnect(true);
  WiFi.onEvent(WiFiEvent);
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while(!wifi_connected) {
    //wait for wifi to finish connecting
    delay(10);
  }
  
  // Create InitialState Bucket
  Serial.println("Setup:: Create InitialState bucket...");
  while(!createBucket()) {
    Serial.println("Setup:: createBucket failed. Retrying...");
    delay(100);
  }

  Serial.println("\n\nSetup:: complete.\n");
}

void loop() {
  int updateReady = false;
  sensors_event_t event;
  float temp;
  float humidity;
  

  // Read DHT22 if the right amount of time has passed
  if (millis() - readTime > dhtDelayMS) {
    readTime = millis();
    updateReady = true;
    
    dht.temperature().getEvent(&event);
    if (isnan(event.temperature)) {
      Serial.println("Error reading temperature!");
      updateReady = false;
    } else {
      Serial.print("Temperature update: ");
      Serial.println(event.temperature);
      temp = event.temperature;
    }

    dht.humidity().getEvent(&event);
    if (isnan(event.relative_humidity)) {
      Serial.println("Error reading humidity!");
      updateReady = false;
    } else {
      Serial.print("Humidity update: ");
      Serial.println(event.relative_humidity);
      humidity = event.relative_humidity;
    }
  }
  
  if (!wifi_connected) {
    Serial.println("Loop:: waiting for WiFi connection...");
    delay(1000);
  } else {

    // WiFi is connected
    // Send an update to the server if we have one
    if (updateReady) {
      sendUpdate(temp, humidity, WiFi.RSSI());
    }
  }
  
}


boolean createBucket() {
  
  String headers = String("Content-Type: application/json\r\n") +
    "X-IS-AccessKey: " + INITIALSTATE_ACCESS_KEY + "\r\n" +
    "Accept-Version: ~0\r\n";

  String data = String("{") +
    "\"bucketKey\": \"" + INITIALSTATE_BUCKET_KEY + "\", "
    "\"bucketName\": \"" + INITIALSTATE_BUCKET_NAME + "\""
    "}";

  return postData("/api/buckets", headers, data);
}

boolean sendUpdate(float temp, float humidity, int rssi) {
  
  String headers = String("Content-Type: application/json\r\n") +
    "X-IS-AccessKey: " + INITIALSTATE_ACCESS_KEY + "\r\n" +
    "X-IS-BucketKey: " + INITIALSTATE_BUCKET_KEY + "\r\n" +
    "Accept-Version: ~0\r\n";

  String data = String("[") +
    "{ \"key\":\"temperature\", \"value\":\"" + temp + "\"}," +
    "{ \"key\":\"relative_humidity\", \"value\":\"" + humidity + "\"}," +
    "{ \"key\":\"rssi\", \"value\":\"" + rssi + "\"}" +
    "]";

  return postData("/api/events", headers, data);
}


boolean postData(String path, String headers, String body) {
  WiFiClientSecure client;

  if (!client.connect(INITIALSTATE_HTTPS_SERVER, INITIALSTATE_HTTPS_PORT)) {
    Serial.println("postData:: could not connect");
    return false;
  }

  String post = String("POST ") + path + " HTTP/1.1\r\n" +
    "Host: " + INITIALSTATE_HTTPS_SERVER + "\r\n" +
    "Connection: Close\r\n";
    
  String contentLength = String("Content-Length: ") + body.length() + "\r\n";
  
  Serial.print("postData:: SENDING: \n=================\n");
  Serial.print(post);
  Serial.print(headers);
  Serial.print(contentLength + "\r\n");
  Serial.print(body);
  Serial.println("\n=================\n");

  client.print(post);
  client.print(headers);
  client.print(contentLength + "\r\n");
  client.print(body + "\r\n");
  
  Serial.println("postData:: Response:\n=================\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println("postData:: ERROR Timeout");
      client.stop();
      return false;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  Serial.println("\n=================\n");

  Serial.println("postData:: closing connection");
  client.stop();
  
  return true;
}

