#include <Adafruit_seesaw.h>
#include <Adafruit_SleepyDog.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <StreamUtils.h>
#include <WiFiNINA.h>
#include "secrets.h"

//#define MYDEBUG

#ifdef MYDEBUG
#define SERIALPRINT(x) Serial.print(x)
#define SERIALPRINTLN(x) Serial.println(x)
#else
#define SERIALPRINT(x)
#define SERIALPRINTLN(x)
#endif

const char LOCATION[] = "Sudachi Hybrid Yuzu";
const char OWNER[] = "Daniel";
const char SENSOR_ID[] = "dau-nanoiot33-1";
const char SERVER_NAME[] = "us-central1-moisture-sensor-b5192.cloudfunctions.net";
const char SSID[] = SECRET_SSID;
const char PASS[] = SECRET_PASS;
const int DESIRED_SLEEP_DURATION = 1800e3; // half an hour in ms
const int DOC_CAPACITY = 768; // https://arduinojson.org/v6/assistant/
const int RESPONSE_TIMEOUT = 5000; // 5 seconds.

Adafruit_seesaw ss;
bool sleeping = false;
float temperature;
int contentLength;
int currentSleepDuration;
int timeout;
int wifiStatus = WL_DISCONNECTED;
uint16_t moisture;
WiFiClient wifiClient;

void setup() {

#ifdef MYDEBUG
  Serial.begin(9600);
  while (!Serial);

  SERIALPRINTLN("Initializing seesaw soil sensor...");
  if (!ss.begin(0x36)) {
    SERIALPRINTLN("ERROR! seesaw not found");
    while (1);
  } else {
    SERIALPRINT("seesaw started! version: ");
    Serial.println(ss.getVersion(), HEX);
  }

  SERIALPRINTLN("Checking Wifi module...");
  checkWifiModule();
#else
  ss.begin(0x36);
#endif

  pinMode(LED_BUILTIN, OUTPUT);

  // Upload opportunity
  delay(1000 * 30);
}

void loop() {
  if (sleeping) {
    sleepMore();
  } else {
    act();
  }
}

void act() {

  SERIALPRINTLN("Act");

  digitalWrite(LED_BUILTIN, HIGH);

  measure();

  connectToWifi();

  if (wifiStatus == WL_CONNECTED) {
    sendData();
  }

  disconnectFromWifi();

  currentSleepDuration = 0;
  sleeping = true;

  digitalWrite(LED_BUILTIN, LOW);
}

void checkWifiModule() {
  //  0: WL_IDLE_STATUS
  //  1: WL_NO_SSID_AVAIL
  //  2: WL_SCAN_COMPLETED
  //  3: WL_CONNECTED
  //  4: WL_CONNECT_FAILED
  //  5: WL_CONNECTION_LOST
  //  6: WL_DISCONNECTED
  //  7: WL_AP_LISTENING
  //  8: WL_AP_CONNECTED

  if (WiFi.status() == WL_NO_MODULE) {
    SERIALPRINTLN("Communication with WiFi module failed!");
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    SERIALPRINTLN("Please upgrade the firmware");
  }
}

void connectToWifi() {

  SERIALPRINTLN("Connecting to Wifi...");

  for (int retry = 0; retry < 3; retry++) {
    SERIALPRINT("Attempting to connect to WPA SSID: "); SERIALPRINTLN(SSID);
    wifiStatus = WiFi.begin(SSID, PASS);
    // Wait 10 seconds to establish connection.
    delay(10000);
    if (wifiStatus == WL_CONNECTED) {
      break;
    }
  }

#ifdef MYDEBUG
  SERIALPRINT("You're connected to the network");
  printCurrentNet();
  printWifiData();

  // Print response if any.
  while (wifiClient.available()) {
    char c = wifiClient.read();
    Serial.write(c);
  }
#endif
}

void disconnectFromWifi() {

  SERIALPRINTLN("Disconnecting from Wifi.");

  WiFi.end();
  wifiStatus = WL_DISCONNECTED;
}

void measure() {

  SERIALPRINTLN("Reading values from sensor");

  temperature = ss.getTemp();
  moisture = ss.touchRead(0);

  SERIALPRINTLN("Summary:");
  SERIALPRINT("Temperature: "); SERIALPRINT(temperature); SERIALPRINTLN("*C");
  SERIALPRINT("Moisture: "); SERIALPRINTLN(moisture);
  SERIALPRINT("Wifi Status: "); SERIALPRINTLN(WiFi.status());
  SERIALPRINTLN("");
}

void printCurrentNet() {

  SERIALPRINT("SSID: ");
  SERIALPRINTLN(WiFi.SSID());

  byte bssid[6];
  WiFi.BSSID(bssid);
  SERIALPRINT("BSSID: ");
  printMacAddress(bssid);

  long rssi = WiFi.RSSI();
  SERIALPRINT("signal strength (RSSI):");
  SERIALPRINTLN(rssi);

  byte encryption = WiFi.encryptionType();
  SERIALPRINT("Encryption Type:");
  Serial.println(encryption, HEX);
  SERIALPRINTLN();
}

void printMacAddress(byte mac[]) {
  for (int i = 5; i >= 0; i--) {
    if (mac[i] < 16) {
      SERIALPRINT("0");
    }

    Serial.print(mac[i], HEX);

    if (i > 0) {
      SERIALPRINT(":");
    }
  }
  SERIALPRINTLN();
}

void printWifiData() {

  IPAddress ip = WiFi.localIP();
  SERIALPRINT("IP Address: ");
  SERIALPRINTLN(ip);

  byte mac[6];
  WiFi.macAddress(mac);
  SERIALPRINT("MAC address: ");
  printMacAddress(mac);
}

void sendData() {

  SERIALPRINTLN("Send Data.");

  // close any connection before send a new request.
  // This will free the socket on the Nina module
  wifiClient.stop();

  StaticJsonDocument<DOC_CAPACITY> doc;

  doc["sensorId"] = SENSOR_ID;
  doc["location"] = LOCATION;
  doc["owner"] = OWNER;
  doc["soilMoisture"]["value"] = moisture;
  doc["soilTemperature"]["value"] = temperature;
  doc["sleepDuration"]["value"] = currentSleepDuration;
  doc["activeTime"]["value"] = millis();

  JsonArray measurementTypes = doc.createNestedArray("measurementTypes");

  JsonObject measurementTypes_0 = measurementTypes.createNestedObject();
  measurementTypes_0["type"] = "soilMoisture";
  measurementTypes_0["name"] = "Soil Moisture";
  measurementTypes_0["unit"] = "capacitance";

  JsonObject measurementTypes_1 = measurementTypes.createNestedObject();
  measurementTypes_1["type"] = "soilTemperature";
  measurementTypes_1["name"] = "Soil Temperature";
  measurementTypes_1["unit"] = "°C";

  JsonObject measurementTypes_2 = measurementTypes.createNestedObject();
  measurementTypes_2["type"] = "sleepDuration";
  measurementTypes_2["name"] = "Sleep Duration";
  measurementTypes_2["unit"] = "ms";

  JsonObject measurementTypes_3 = measurementTypes.createNestedObject();
  measurementTypes_3["type"] = "activeTime";
  measurementTypes_3["name"] = "Active Time";
  measurementTypes_3["unit"] = "ms";

#ifdef MYDEBUG
  SERIALPRINT("Json Payload; length: "); SERIALPRINTLN(measureJson(doc));
  serializeJsonPretty(doc, Serial);
  SERIALPRINTLN();
#endif

  if (wifiClient.connect(SERVER_NAME, 80)) {
    WriteBufferingStream bufferedWifiClient{wifiClient, 64};

    SERIALPRINTLN("Connected.");
    SERIALPRINT("POST /app/api/measurements?key="); SERIALPRINT(API_KEY); SERIALPRINTLN(" HTTP/1.1");
    SERIALPRINTLN("Content-Type: application/json");
    SERIALPRINTLN("User-Agent: ArduinoWiFi/1.1");
    SERIALPRINT("Host: "); SERIALPRINTLN(SERVER_NAME);
    SERIALPRINT("Content-Length: "); SERIALPRINTLN(measureJson(doc));
    SERIALPRINTLN("Connection: close");
    SERIALPRINTLN();
#ifdef MYDEBUG
    serializeJson(doc, Serial);
    SERIALPRINTLN();
#endif

    wifiClient.print("POST /app/api/measurements?key="); wifiClient.print(API_KEY); wifiClient.println(" HTTP/1.1");
    wifiClient.println("Content-Type: application/json");
    wifiClient.println("User-Agent: ArduinoWiFi/1.1");
    wifiClient.print("Host: "); wifiClient.println(SERVER_NAME);
    wifiClient.print("Content-Length: "); wifiClient.println(measureJson(doc));
    wifiClient.println("Connection: close");
    wifiClient.println();
    serializeJson(doc, bufferedWifiClient);

    bufferedWifiClient.flush();

    SERIALPRINTLN("Request sent.");

    timeout = millis() + RESPONSE_TIMEOUT;
    while (wifiClient.available() == 0) {
      if (timeout - millis() < 0) {
        SERIALPRINTLN("No response from server, wifi client time out!");
        break;
      }
    }

#ifdef MYDEBUG
    if (wifiClient.available()) {
      SERIALPRINTLN("Response received.");
    }
    while (wifiClient.available()) {
      SERIALPRINTLN(wifiClient.readStringUntil('\r'));
    }
#endif

    wifiClient.stop();

  } else {
    SERIALPRINTLN("Connection failed.");
  }
}

void sleepMore() {

  SERIALPRINT("Sleep More, currentSleepDuration: "); SERIALPRINTLN(currentSleepDuration);

  if (currentSleepDuration < DESIRED_SLEEP_DURATION) {
    currentSleepDuration += Watchdog.sleep();
  } else {
    sleeping = false;
  }
}
