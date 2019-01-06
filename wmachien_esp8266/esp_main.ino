/*
    Program for the NodeMCU (based on ESP8266) wmachien
    
    ESP8266 MAC Address: 84:F3:EB:81:4D:A3
    DHCP Static IP :     192.168.0.201 (on laprowainternet_2.4)

    Program to receive data from arduino (serial, RX & TX)
        And write this data to the sd card

    Connect SD card module:
        MOSI    D7
        MISO    D6
        SCK     D5
        CS      D8 (in code referred to as SS)
        VCC     5v of e.g. Arduino
        GND     Common gnd with e.g. Arduino

    MQTT broker: ThingsBoard (demo server)
        Device ID       7bd5a970-0865-11e9-a1f6-a3a281c054e4
        Access token    rinkert_wmachien

    NodeMCU pin numbers
        LED_BUILTIN 16
        D0          16
        D1          5
        D2          4
        D3          0
        D4          2
        D5          14
        D6          12
        D7          13
        D8          15
        D9          3
        D10         1
*/

// TODO:
//      - add last water time

// include libraries
#include <SPI.h>              // SPI library for SDcard module
#include <SD.h>               // SD library for writing and reading files on SDcard
#include <ESP8266WiFi.h>      // Enables the ESP8266 to connect to the local network (via WiFi)
#include <ESP8266WiFiMulti.h> // Allows setting up multiple access points for ESP8266 wifi connection
#include <WiFiUdp.h>          // UDP messaging protocol
#include <TimeLib.h>          // library to manage time
#include <PubSubClient.h>     // MQTT library
#include <SoftwareSerial.h>   // Software serial library for serial connection on arbitrary pins
#include <ArduinoJson.h>      // receive formatted data in json, from arduino nano

// MQTT AND OTHER GPIO control
#define GPIO0 0                       // D3 = 0
#define GPIO2 2                       // D4 = 2
#define GPIO0_PIN 0                   // D3 = 0
#define GPIO2_PIN 2                   // D4 = 2
boolean gpioState[] = {false, false}; // We assume that all GPIOs are LOW

// WiFi
ESP8266WiFiMulti wifiMulti; // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
const char *ssid1 = "laprowainternet_2.4";
const char *pass1 = "geenidee";
const char *ssid2 = "laprowainternet_5.0";
const char *pass2 = "geenidee";
const char *ssid3 = "LekkerBrownenN";
const char *pass3 = "wegwezen";
int status = WL_IDLE_STATUS;

// Setup UDP for NTP time requests
WiFiUDP UDP;                                // A UDP instance to let us send and receive packets over UDP
unsigned int localPort = 2390;              // local port to listen for UDP packets
IPAddress timeServerIP;                     // will be set to time.nist.gov NTP server address
const char *ntpServerName = "pool.ntp.org"; // NTP server, alternative: "time.nist.gov";
const int NTP_PACKET_SIZE = 48;             // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE];         // buffer to hold incoming and outgoing packets
const int timeZone = 1;                     // Central European Time

// MQTT (ThingsBoard)
#define TOKEN "8hmP8jxwJP1wh6FIFRp7"              // token to authenthicate at ThingsBoard server
const char *mqttClientID = "Wmachien Laprowa";    // client ID for this ESP8266
char thingsboardServer[] = "demo.thingsboard.io"; // adress (IP) to the thingsboard server
WiFiClient wifiClient;                            // init wifi client for mqtt
PubSubClient client(wifiClient);                  // init mqtt client

// SoftwareSerial
SoftwareSerial ssa(D2, D1); // SoftwareSerial(RX, TX), make sure RX to TX, and TX to RX...
bool LED_status = false;

// sensor data variables
int moist_top;                      // moisture at the top
int moist_bottom;                   // moisture at the bottom
int moist_extra;                    // moisture at the extra sensor
int light_level;                    // light level (1 or 0)
float hum_val;                      // air humidity
float temp_val;                     // air temperature
float waterDurationSec = 0;         // duration of water giving
time_t last_time_data_received = 0; // unix timestamp at last sensor data received

// keep track of time
time_t lastRealTime = 0;                                // last real time obtained from the internet, in unix timestamp (seconds)
time_t softwareTime = 0;                                // time to track in software
unsigned long lastFlash = 0;                            // flashing nano and NodeMCU leds
unsigned long lastRealTimeMs = 0;                       // last moment in time in millis() counter that the realtime is obtained
const unsigned long time_update_delay = 60 * 1000;      // 60 seconds, update the realtime every xx seconds
const unsigned long sensor_update_delay = 60;           // 1 minute = 60 sec
const unsigned long system_restart_duration = 86400000; // 1 day = 86.400.000 msec

/*===================================================================
    Setup for NodeMCU
===================================================================*/
void setup()
{
    // Open serial communications and wait for port to open:
    Serial.begin(115200);
    ssa.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    // pins for mqtt gpio control, set mode and set low
    pinMode(GPIO0_PIN, OUTPUT);
    pinMode(GPIO2_PIN, OUTPUT);
    digitalWrite(GPIO0_PIN, LOW);
    digitalWrite(GPIO2_PIN, LOW);

    // initialize the SD card
    initSDCard();

    // Connect to Wifi network
    startWiFi();

    // start UDP
    startUDP();

    // start MQTT connection
    client.setServer(thingsboardServer, 1883);
    client.setCallback(mqttCallbackOnMessage);

    // init the timers
    storeCurrentTime(/* printing */ false);

    // init Watchdog Timer
}

/*===================================================================
    Loop for NodeMCU
===================================================================*/
void loop()
{
    // check if restart esp required
    if (millis() > system_restart_duration)
    {
        ESP.restart();
    }

    // if 10 minutes past since last time update, get new time:
    unsigned long timeDiff = millis() - lastRealTimeMs;
    if (timeDiff > time_update_delay)
    // if (softwareTime - lastRealTime > time_update_delay)
    {
        storeCurrentTime(/* printing */ true); // true if want to print to hardware serial
        timeDiff = millis() - lastRealTimeMs;  // update timeDiff, since lastRealTimeMs is updated in storeCurrentTime()
    }
    // update softwareTime
    // Serial.print("\t"); Serial.print(timeDiff); Serial.print("\t");
    softwareTime = lastRealTime + (timeDiff / 1000); // go from msec back to secs (divide by 1000)
    // printCurrentTime(softwareTime);

    // request sensor data every sensor_update_delay seconds
    if (softwareTime - last_time_data_received > sensor_update_delay)
    {
        Serial.println("requesting data");
        // request the data from the nano
        requestSensorData();
        // write the obtained data to SD Card
        writeToSD();
        // publish data to mqttClient
        mqttSendData();
    }

    // mqtt client loop, check for incoming messages
    if (!client.connected())
    {
        Serial.println("reconnecting to mqtt broker");
        mqttReconnect();
    }
    client.loop(); // needed to listen for mqtt messages

    // switch LED on nano debug, and on ESP8266 for effect with delay of 500 msec
    if (millis() - lastFlash > 500)
    {
        lastFlash = millis();
        ssa.write('S');
        switchLED();
    }

    // check for incoming messages from nano
    if (ssa.available() > 0)
    {
        char receive = ssa.read();
        switch (receive)
        {
        case 'T':
            // water is given to plant, notify server
            unsigned int waterDuration = ssa.parseInt(); // in msec
            char end_msg = ssa.read();                   // read the closing message
            if (end_msg == 'E')
                Serial.println("end message received");

            waterDurationSec += (float)(waterDuration / 1000);
            break;
        }
    }
}

// ===========================================================================
//                                  SD FUNCTIONS
// ===========================================================================

/*===================================================================
    function to start the sd card. 
===================================================================*/
void initSDCard()
{
    Serial.print("Initializing SD card...");
    if (!SD.begin(SS))
    {
        Serial.println("initialization failed!");
        // if failed, do not continiue (flash light)
        while (1)
        {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(100);
            digitalWrite(LED_BUILTIN, LOW);
            delay(900);
        }
    }
    Serial.println("initialization done.");
}

/*===================================================================
    function to append data to (csv) file on sd card
    
        time_t curTime      current time in seconds since 1 Jan 1970
        int sensor1         sensor value ... 
        int sensor1         sensor value ... 

===================================================================*/
void writeToSD()
{
    // open file (only one can be open at a time)
    File dataFile = SD.open("data.csv", FILE_WRITE);

    // if file is available, write to it
    if (dataFile)
    {
        // write to file CSV header:
        //  lastRealTime,softwareTime,last_time_data_received,moist_top,moist_bottom,light_level,hum_val,temp_val
        dataFile.print(lastRealTime);
        dataFile.print(',');
        dataFile.print(softwareTime);
        dataFile.print(',');
        dataFile.print(last_time_data_received);
        dataFile.print(',');
        dataFile.print(moist_top);
        dataFile.print(',');
        dataFile.print(moist_bottom);
        dataFile.print(',');
        dataFile.print(light_level);
        dataFile.print(',');
        dataFile.print(hum_val);
        dataFile.print(',');
        dataFile.print(temp_val);
        dataFile.print(',');
        dataFile.println(moist_extra);

        // close file after writing
        dataFile.close();
    }
    else
    {
        Serial.println("Error: could not open file!");
    }
}

// ===========================================================================
//                                  WIFI AND INTERNET FUNCTIONS
// ===========================================================================

/*===================================================================
    function to initialize the wifi connection
===================================================================*/
void startWiFi()
{
    // Add all access points (APs)
    wifiMulti.addAP(ssid1, pass1);
    wifiMulti.addAP(ssid2, pass2);
    wifiMulti.addAP(ssid3, pass3);
    // wifiMulti.addAP(ssid4, pass4);

    // Try to connect to some given access points. Then wait for a connection
    Serial.println("Connecting");
    while (wifiMulti.run() != WL_CONNECTED)
    { // Wait for the Wi-Fi to connect
        delay(250);
        Serial.print('.');
    }
    Serial.println("\r\n");
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID()); // Tell us what network we're connected to
    Serial.print("IP address:\t");
    Serial.print(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer
    Serial.println("\r\n");
}

/*===================================================================
    Start UDP to listen for messages (e.g. current time)
===================================================================*/
void startUDP()
{
    Serial.println("Starting UDP");
    UDP.begin(localPort); // Start listening for UDP messages to port 'localPort'
    Serial.print("Local port:\t");
    Serial.println(UDP.localPort());
}

// ===========================================================================
//                                  TIME FUNCTIONS
// ===========================================================================

/*===================================================================
    Function keep polling time till obtained, and store it in global variables
===================================================================*/
void storeCurrentTime(bool printing)
{
    time_t curTime = 0;
    while (curTime == 0)
    {
        curTime = getCurrentTime();
        // delay after failed Attempt
        if (!curTime)
        {
            delay(1500);
        }
    }
    // write to global time variable
    lastRealTime = curTime;
    softwareTime = curTime;    // set softwaretime to the realtime
    lastRealTimeMs = millis(); // update the lasTimeMs

    // print time if wanted
    if (printing)
    {
        printCurrentTime(curTime);
    }
}

/*===================================================================
    Function to get the current time via UDP messages with the NTP server
===================================================================*/
time_t getCurrentTime()
{
    // dicard any previously received packets
    while (UDP.parsePacket() > 0)
        ;

    // get a random server from the pool, and ask for time
    WiFi.hostByName(ntpServerName, timeServerIP);
    sendNTPpacket(timeServerIP); // send an NTP packet to a time server

    // check for answer
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1000) // wait max 3sec for answer
    {
        int size = UDP.parsePacket();
        if (size >= NTP_PACKET_SIZE)
        {
            Serial.println("Receive NTP Response");
            UDP.read(packetBuffer, NTP_PACKET_SIZE); // read packet into the buffer

            unsigned long secsSince1900;
            // convert four bytes starting at location 40 to a long integer
            secsSince1900 = (unsigned long)packetBuffer[40] << 24;
            secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
            secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
            secsSince1900 |= (unsigned long)packetBuffer[43];
            // convert secsSince1900 to unix timestamp (time since 1 Jan 1970)
            time_t secsSince1970 = secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;

            // return the unix timestamp
            return secsSince1970;
        }
    }
    Serial.println("No NTP Response :-(");
    return 0; // return 0 if unable to get the time
}

/*===================================================================
    Function to send the time request to the NTP UDP server
===================================================================*/
void sendNTPpacket(IPAddress &address)
{
    Serial.println("sending NTP packet...");
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011; // LI, Version, Mode
    packetBuffer[1] = 0;          // Stratum, or type of clock
    packetBuffer[2] = 6;          // Polling Interval
    packetBuffer[3] = 0xEC;       // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    UDP.beginPacket(address, 123); //NTP requests are to port 123
    UDP.write(packetBuffer, NTP_PACKET_SIZE);
    UDP.endPacket();
}

/*===================================================================
    Function to print the current time to Serial monitor
===================================================================*/
// utility function for digital clock display: prints preceding colon and leading 0
void printDigits(int digits)
{
    Serial.print(":");
    if (digits < 10)
        Serial.print('0');
    Serial.print(digits);
}
void printCurrentTime(time_t curTime)
{
    // print the time
    Serial.print("The current time is:\t");
    Serial.print(hour(curTime));
    printDigits(minute(curTime));
    printDigits(second(curTime));
    Serial.print(" ");
    Serial.print(day(curTime));
    Serial.print("-");
    Serial.print(month(curTime));
    Serial.print("-");
    Serial.print(year(curTime));
    Serial.print("\t\tSeconds since Jan 1970:\t");
    Serial.println(curTime);
}

// ===========================================================================
//                                  MQTT FUNCTIONS
// ===========================================================================

/*===================================================================
    Function to reconnect to MQTT broker if connection lost
===================================================================*/
void mqttReconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        status = WiFi.status();
        if (status != WL_CONNECTED)
        {
            startWiFi();
        }
        Serial.print("Connecting to ThingsBoard node ...");

        // Attempt to connect (clientId, username, password) (clientID does not matter)
        if (client.connect("ESP8266 DEMO DEVICE", TOKEN, NULL))
        // if (client.connect(mqttClientID, TOKEN, NULL))
        {
            Serial.println("[DONE]");
            // Subscribing to receive RPC requests
            client.subscribe("v1/devices/me/rpc/request/+");
            // Sending current GPIO status
            Serial.println("Sending current GPIO status ...");
            client.publish("v1/devices/me/attributes", get_gpio_status().c_str());
        }
        else
        {
            Serial.print("[FAILED] [ rc = ");
            Serial.print(client.state());
            Serial.println(" : retrying in 5 seconds]");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void mqttSendData()
{
    // reconnect to mqtt broker if needed
    if (!client.connected())
    {
        Serial.println("reconnecting to mqtt broker");
        mqttReconnect();
    }

    // convert values to strings
    String s_moist_top = String(moist_top);
    String s_moist_bottom = String(moist_bottom);
    String s_moist_extra = String(moist_extra);
    String s_light_level = String(light_level);
    String s_hum_val = String(hum_val);
    String s_temp_val = String(temp_val);
    String s_water = String(waterDurationSec);
    String s_last_time_data_received = String(last_time_data_received);

    // Prepare a JSON payload string
    String payload = "{";
    payload += "\"moisttop\":";
    payload += s_moist_top;
    payload += ",";
    payload += "\"moistbottom\":";
    payload += s_moist_bottom;
    payload += ",";
    payload += "\"moistextra\":";
    payload += s_moist_extra;
    payload += ",";
    payload += "\"lightlevel\":";
    payload += s_light_level;
    payload += ",";
    payload += "\"humval\":";
    payload += s_hum_val;
    payload += ",";
    payload += "\"tempval\":";
    payload += s_temp_val;
    payload += ",";
    payload += "\"waterDur\":";
    payload += s_temp_val;
    payload += "}";

    // DONT sent timestamp, does not work
    // Alternative: {"ts":1451649600512, "values":{"key1":"value1", "key2":"value2"}}
    // payload += ",";
    // payload += "\"lasttimedatareceived\":";
    // payload += s_last_time_data_received;

    // Send payload
    char attributes[200];
    payload.toCharArray(attributes, 200);
    client.publish("v1/devices/me/telemetry", attributes);
    Serial.println(attributes);

    // reset the water give duration, since it is publised
    waterDurationSec = 0;
}

// ===========================================================================
//                                  COMMUNICATION WITH NANO
// ===========================================================================

/*===================================================================
    function to request sensor data in json format from arduino nano
===================================================================*/
void requestSensorData()
{
    int success = 0;
    while (success == 0)
    {
        // write 'D' to request measuring of data
        ssa.write('D');
        // measuring takes approx 275 msec, so long delay
        delay(500);
        // write 'J' to request sending of data
        ssa.write('J');
        success = receiveJsonData();
        if (success == 0)
        {
            Serial.println("not successfull... flushing serial buffer");
            softSerialFlush();
            Serial.println("additional delay 3s");
            delay(3000);
        }
        else
        {
            Serial.println("json received");
            // when success: update time since last data
            last_time_data_received = softwareTime;
        }
    }
}

/*===================================================================
    function to receive json data from arduino nano
        
    return int:
        0   if failed (data invalid)
        1   if success
===================================================================*/
int receiveJsonData()
{
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(ssa);

    if (root == JsonObject::invalid())
    {
        Serial.println("JsonObject invalid");
        return 0;
    }
    Serial.println("JSON received and parsed");
    root.prettyPrintTo(Serial);
    Serial.println();

    hum_val = root["hv"];      // hum_val = root["hum_val"];
    temp_val = root["tv"];     // temp_val = root["temp_val"];
    light_level = root["ll"];  // light_level = root["light_level"];
    moist_top = root["mt"];    // moist_top = root["moist_top"];
    moist_bottom = root["mb"]; // moist_bottom = root["moist_bottom"];
    moist_extra = root["me"];  // moist_extra = root["moist_extra"];

    // let nano know data parsed succesfully
    // ssa.write('y');
    return 1;
}

// function to flush the hardware serial
void serialFlush()
{
    while (Serial.available() > 0)
    {
        char t = Serial.read();
    }
}
// function to flush the software serial
void softSerialFlush()
{
    while (ssa.available() > 0)
    {
        char t = ssa.read();
    }
}

// function to switch the builtin led: LED_BUILTIN
void switchLED()
{
    LED_status = !LED_status;
    digitalWrite(LED_BUILTIN, LED_status);
}

// ===========================================================================
//                                  MQTT CALLBACK FUNCTIONS
// ===========================================================================

// The callback for when a PUBLISH message is received from the server.
void mqttCallbackOnMessage(const char *topic, byte *payload, unsigned int length)
{
    Serial.println("Message Received! -->");
    // copy incoming payload to json formatted char array
    char json[length + 1];
    strncpy(json, (char *)payload, length);
    json[length] = '\0';

    Serial.print("Topic: ");
    Serial.println(topic);
    Serial.print("Message: ");
    Serial.println(json);

    // Decode JSON request
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject &data = jsonBuffer.parseObject((char *)json);
    // if json does not make sense:
    if (!data.success())
    {
        Serial.println("parseObject() failed");
        return;
    }

    // Check request method
    String methodName = String((const char *)data["method"]);

    if (methodName.equals("getGpioStatus"))
    {
        // Reply with GPIO status
        String responseTopic = String(topic);
        responseTopic.replace("request", "response");
        client.publish(responseTopic.c_str(), get_gpio_status().c_str());
    }
    else if (methodName.equals("setGpioStatus"))
    {
        // Update GPIO status and reply
        set_gpio_status(data["params"]["pin"], data["params"]["enabled"]);
        String responseTopic = String(topic);
        responseTopic.replace("request", "response");
        client.publish(responseTopic.c_str(), get_gpio_status().c_str());
        client.publish("v1/devices/me/attributes", get_gpio_status().c_str());
    }
}

String get_gpio_status()
{
    // Prepare gpios JSON payload string
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject &data = jsonBuffer.createObject();
    data[String(GPIO0_PIN)] = gpioState[0] ? true : false;
    data[String(GPIO2_PIN)] = gpioState[1] ? true : false;
    char payload[256];
    data.printTo(payload, sizeof(payload));
    String strPayload = String(payload);
    Serial.print("Get gpio status: ");
    Serial.println(strPayload);
    return strPayload;
}

void set_gpio_status(int pin, boolean enabled)
{
    if (pin == GPIO0_PIN)
    {
        // Output GPIOs state
        digitalWrite(GPIO0, enabled ? HIGH : LOW);
        // Update GPIOs state
        gpioState[0] = enabled;
    }
    else if (pin == GPIO2_PIN)
    {
        // Output GPIOs state
        digitalWrite(GPIO2, enabled ? HIGH : LOW);
        // Update GPIOs state
        gpioState[1] = enabled;
    }
}