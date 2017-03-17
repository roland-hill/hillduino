/*-----( Import needed libraries )-----*/
#include <OneWire.h>
#include <PubSubClient.h>
#include <WiFiEsp.h>
#include <SparkFunBME280.h>
#include <Wire.h>

/*-----( Emulate Serial1 on pins 2/3 )-----*/
#ifndef HAVE_HWSERIAL1
#include <SoftwareSerial.h>
SoftwareSerial Serial1(2, 3); // RX, TX
#endif

/*-----( Declare Constants and Pin Numbers )-----*/
#define wifi_ssid "ssid"
#define wifi_password "passwd"
#define mqtt_server "192.16x.x.xx"
#define mqtt_user "mqtt_user"
#define mqtt_password "mqtt_passwd"
#define tt "sensor/bme280/temperature"
#define ht "sensor/bme280/humidity"
#define pt "sensor/bme280/pressure"

char hexChars[] = "0123456789ABCDEF";
#define HEX_MSB(v) hexChars[(v & 0xf0) >> 4]
#define HEX_LSB(v) hexChars[v & 0x0f]

/*-----( Declare objects )-----*/
// Setup a oneWire instance
OneWire  ds(8);  // on pin 8 (a 4.7K resistor is necessary)

// I2C Sensor
BME280 bme280;

// Initialize the Ethernet client object
WiFiEspClient espClient;
// Initialize mqtt server object
PubSubClient client(espClient);

/*-----( Declare Variables )-----*/
// Wifi radio's status
int status = WL_IDLE_STATUS;
//indicator LED pins
const int red_led = 4;
const int green_led = 5;
const int blue_led = 6;

void setup() {
  Serial.begin(9600);
  setup_wifi();
  setup_bme280();
  client.setServer(mqtt_server, 1883);

  // configure 3 RGB pins as outputs
  pinMode(red_led, OUTPUT);
  pinMode(green_led, OUTPUT);
  pinMode(blue_led, OUTPUT);
  digitalWrite(red_led, LOW);
  digitalWrite(green_led, LOW);
  digitalWrite(blue_led, LOW);
}

void blink_red() {
  digitalWrite(red_led, HIGH);
  delay(1000);
  digitalWrite(red_led, LOW);
}

void blink_blue() {
  digitalWrite(blue_led, HIGH);
  delay(1000);
  digitalWrite(blue_led, LOW);
}

void blink_green() {
  digitalWrite(green_led, HIGH);
  delay(1000);
  digitalWrite(green_led, LOW);
}

void setup_wifi() {
  // initialize serial for ESP module
  Serial1.begin(9600);
  // initialize ESP module
  WiFi.init(&Serial1);
  // check for the presence of the shield
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue
    while (true);
  }
  // attempt to connect to WiFi network
  while ( status != WL_CONNECTED) {
    blink_red();
    delay(480);
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(wifi_ssid);
    // Connect to WPA/WPA2 network
    status = WiFi.begin(wifi_ssid, wifi_password);
  }
  // you're connected now, so print out the data
  Serial.println("You're connected to the network\n");
  blink_blue();
}

void setup_bme280()
{
  bme280.settings.commInterface = I2C_MODE;
  bme280.settings.I2CAddress = 0x76;
  bme280.settings.runMode = 3; //Normal mode
  //Standby can be:  0, 0.5ms - 1, 62.5ms - 2, 125ms -  3, 250ms - 4, 500ms - 5, 1000ms - 6, 10ms - 7, 20ms
  bme280.settings.tStandby = 3;
  bme280.settings.filter = 4;
  //*OverSample can be: 0, skipped - 1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme280.settings.tempOverSample = 5;
  bme280.settings.pressOverSample = 5;
  bme280.settings.humidOverSample = 5;

  //Calling .begin() causes the settings to be loaded
  delay(10);  //BME280 requires 2ms to start up.
  Serial.println(bme280.begin(), HEX);
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected()) {
    digitalWrite(blue_led, LOW);
    Serial.println();
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("connected\n");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      blink_red();
      delay(200);
      blink_red();
      delay(4760);
    }
  }
}

void loop() {
  //Connect to MQTT
  if (!client.connected())
  {
    reconnect();
  } else {
    client.loop();
  }
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius;

  if ( !ds.search(addr)) {
    Serial.println("No more addresses.\n");
    ds.reset_search();

    //Start with bme280 temperature, as that data is needed for accurate compensation.
    float bme280_t = bme280.readTempC();
    Serial.println("BME280 device");
    Serial.print("  Temperature: ");
    Serial.print(bme280_t, 2);
    client.publish(tt, String(bme280_t).c_str(), true);
    Serial.println(" Celsius");

    float bme280_p = bme280.readFloatPressure();
    Serial.print("  Pressure: ");
    Serial.print(bme280_p, 2);
    client.publish(pt, String(bme280_p).c_str(), true);
    Serial.println(" Pa");

    float bme280_h = bme280.readFloatHumidity();
    Serial.print("  %RH: ");
    Serial.print(bme280_h, 2);
    client.publish(ht, String(bme280_h).c_str(), true);
    Serial.println(" %");

    Serial.println();
    blink_green();
    delay(1000);

    Serial.println("Pausing between search's");
    delay(30000); //delay between finding all the sensors
    return;
  }

  Serial.println("Chip = DS18B20");
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);// start conversion, with parasite power on at the end
  delay(1000);      // maybe 750ms is enough, maybe not

  present = ds.reset();
  ds.select(addr);
  ds.write(0xBE);   // Read Scratchpad

  for ( i = 0; i < 9; i++) {  // we need 9 bytes
    data[i] = ds.read();
  }

  // Convert the data to actual temperature
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    // default is 12 bit resolution, 750 ms conversion time
  }

  celsius = (float)raw / 16.0;
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.println(" Celsius\n");

  //publish the temp now
  char charTopic[] = "sensor/XXXXXXXXXXXXXXXX/temperature";
  for (i = 0; i < 8; i++) {
    charTopic[7 + i * 2] = HEX_MSB(addr[i]); //7 is where the backlash before XXX starts
    charTopic[8 + i * 2] = HEX_LSB(addr[i]); //8 is plus one on the above
  }
  char charMsg[10];
  memset(charMsg, '\0', 10);
  dtostrf(celsius, 4, 2, charMsg);
  client.publish(charTopic, charMsg);
  blink_green();
  delay(1000); // just adding a small delay between publishing just incase
}