/*
  Functionality:
  -OTA Update, Enable Flag from Web Interface.
  -Sending DHT22 Output to SQL DB.
  -MQTT turn on of relay
*/
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <DHT.h>
#include <PubSubClient.h>

#define DHTTYPE DHT22
#define DHTPIN D3
#define RELAYPIN D2

const char* ssid = "";
const char* password = "";
const char* mqtt_host = "192.168.0.178";

String data, update_request;
char c; //Data from GET
float t = 0, h = 0;
int t_count = 0;
bool u_flag = 0;
unsigned long timepassed = 0;
long last_time = 0, last_time2 = 0, update_time = 0;
int interval = 30 - 1; //desired time -1 to make up for the > sign.
int interval2 = 3 - 1; //interval time for sensor update
int update_interval = 60 - 1; //how often to check for OTA update.
IPAddress server(192, 168, 0, 178); //RPI

//Initialize Client
WiFiClient client;
//Initilize MQTT
PubSubClient mqtt(client);
//Initialize DHT Sensor
DHT dht(DHTPIN, DHTTYPE);

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println(topic);
  for (int i = 0; i < length; i++) {
    char receivedChar = (char) payload[i];
    if (receivedChar == '0')
      digitalWrite(RELAYPIN, LOW);

    if (receivedChar == '1')
      digitalWrite(RELAYPIN, HIGH);

  }
}

//MQTT Connection and subcribe
void reconnect() {
  while (!mqtt.connected()) {
    if (mqtt.connect("KITCHEN")) {
      mqtt.subscribe("RELAY_KITCHEN");
    } else {
      Serial.println(mqtt.state());
      if(mqtt.state()==MQTT_CONNECT_FAILED)
        ESP.restart();
      delay(5000);
    }
  }
}

void OTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  ArduinoOTA.setPassword((const char *)"@dmin123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    ESP.restart();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void setup() {
  pinMode(RELAYPIN, OUTPUT);
  dht.begin();
  Serial.begin(115200);
  Serial.println("Booting");
  Serial.println(ESP.getChipId());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println(WiFi.status());
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  OTA();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  mqtt.setServer(mqtt_host, 1883);
  mqtt.setCallback(callback);
  delay(5000); //INIT DELAY
}

void loop() {
  timepassed = (millis() / 1000);
  //MQTT
  if (!mqtt.connected()) {
    reconnect();
  }

  mqtt.loop();
  //EOF MQTT
  if (timepassed - update_time > update_interval)
  {
    update_time = timepassed;
    if (client.connect(server, 80)) { // REPLACE WITH YOUR SERVER ADDRESS
      client.println("GET /esp/node.php?id=" + String(ESP.getChipId()) + "&update HTTP/1.1");
      client.println("Host: 192.168.0.178:80"); // SERVER ADDRESS HERE TOO
      client.println();
    }

    while (client.connected()) {
      if (client.available()) {
        c = client.read();
        update_request += c;
      }
    }

    //Check for OTA Mode Flag
    if (update_request.endsWith("UPDATE"))
    {
      Serial.println("Going into OTA Mode");
      u_flag = 1; //Set OTA Flag.
    }
    else
      u_flag = 0; //Set Normal Mode.

    if (client.connected()) {
      client.stop();  // DISCONNECT FROM THE SERVER
    }

  }

  if (u_flag == 1) //OTA Mode Flag
    ArduinoOTA.handle();

  else
  {
    //Update Sensor values
    if (timepassed - last_time2 > interval2)
    {
      last_time2 = timepassed;
      t += dht.readTemperature();
      h += dht.readHumidity();
      Serial.println(t);
      t_count++;
    }

    //Send Data to DB.
    if (timepassed - last_time > interval)
    {
      data = "temp=" + String(t / t_count) + "&hum=" + String(h / t_count);
      Serial.println(t_count);
      Serial.printf("Total: %d\n", t);
      t_count = 0, t = 0, h = 0; //Reset counter.
      //Update Last_Time
      last_time = timepassed;
      //Send Data to DB.
      if (client.connect(server, 80)) { // REPLACE WITH YOUR SERVER ADDRESS
        client.println("POST /esp/addtemp.php?id=" + String(ESP.getChipId()) + " HTTP/1.1");
        client.println("Host: 192.168.0.178:80"); // SERVER ADDRESS HERE TOO
        client.println("Content-Type: application/x-www-form-urlencoded");
        client.print("Content-Length: ");
        client.println(data.length());
        client.println();
        client.print(data);
        Serial.println("Data Upload:Success");
      }

      if (client.connected()) {
        client.stop();  // DISCONNECT FROM THE SERVER
      }

    }

  } //Close else statement
} //Close Loop
