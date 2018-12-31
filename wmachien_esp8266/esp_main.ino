/*
    Program for the NodeMCU (based on ESP8266) wmachien
    
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
*/

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

// WiFi
ESP8266WiFiMulti wifiMulti; // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
const char *ssid1 = "laprowainternet_2.4";
const char *pass1 = "geenidee";
const char *ssid2 = "laprowainternet_5.0";
const char *pass2 = "geenidee";
const char *ssid3 = "LekkerBrownenN";
const char *pass3 = "wegwezen";
// const char *ssid4 = "wifinetwerk";
// const char *pass4 = "0641206012joep";
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
int light_level;                    // light level (1 or 0)
float hum_val;                      // air humidity
float temp_val;                     // air temperature
time_t last_time_data_received = 0; // unix timestamp at last sensor data received

// keep track of time
time_t lastRealTime = 0;          // last real time obtained from the internet, in unix timestamp (seconds)
time_t softwareTime = 0;          // time to track in software
unsigned long lastRealTimeMs = 0; // last moment in time in millis() counter that the realtime is obtained
// unsigned long time_update_delay = 10 * 60 * 1000; // 10 #minutes times 60 seconds * 1000 msec/sec= 600.000 seconds = 10 minutes
unsigned long time_update_delay = 30 * 1000; // 30 seconds

/*===================================================================
    Setup for NodeMCU
===================================================================*/
void setup()
{
    // Open serial communications and wait for port to open:
    Serial.begin(115200);
    ssa.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    // initialize the SD card
    initSDCard();

    // Connect to Wifi network
    startWiFi();

    // start UDP
    startUDP();

    // start MQTT connection
    client.setServer(thingsboardServer, 1883);

    // init the timers
    storeCurrentTime(/* printing */ false);
}

/*===================================================================
    Loop for NodeMCU
===================================================================*/
void loop()
{

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

    // switch LED on nano debug, and on ESP8266 for effect
    ssa.write('S');
    switchLED();

    // request sensor data every 5 minutes
    if (softwareTime - last_time_data_received > 5*60)
    {
        Serial.println("requesting data");
        // request the data from the nano
        requestSensorData();
        // write the obtained data to SD Card
        writeToSD();
        // publish data to mqttClient
        mqttSendData();
    }

    // delay to 
    delay(500);
}

// ===========================================================================
//                                  OTHER FUNCTIONS
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
            delay(200);
            digitalWrite(LED_BUILTIN, LOW);
            delay(800);
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
        dataFile.println(temp_val);

        // close file after writing
        dataFile.close();
    }
    else
    {
        Serial.println("Error: could not open file!");
    }
}

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

        // Attempt to connect (clientId, username, password)
        if (client.connect("ESP8266 DEMO DEVICE", TOKEN, NULL))
        // if (client.connect(mqttClientID, TOKEN, NULL))
        {
            Serial.println("[DONE]");
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

    String s_moist_top = String(moist_top);
    String s_moist_bottom = String(moist_bottom);
    String s_light_level = String(light_level);
    String s_hum_val = String(hum_val);
    String s_temp_val = String(temp_val);
    String s_last_time_data_received = String(last_time_data_received);

    // Prepare a JSON payload string
    String payload = "{";
    payload += "\"moisttop\":";
    payload += s_moist_top;
    payload += ",";
    payload += "\"moistbottom\":";
    payload += s_moist_bottom;
    payload += ",";
    payload += "\"lightlevel\":";
    payload += s_light_level;
    payload += ",";
    payload += "\"humval\":";
    payload += s_hum_val;
    payload += ",";
    payload += "\"tempval\":";
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
}

/*===================================================================
    function to request sensor data in json format from arduino nano
===================================================================*/
void requestSensorData()
{
    int success = 0;
    while (success == 0)
    {
        // write 'D' to request data
        ssa.write('D');
        success = receiveJsonData();
        if (success == 0)
        {
            Serial.println("not successfull... flushing serial buffer");
            // FIXME: serial flush? flush the entire serial buffer, try
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
/*===================================================================
    TODOS:
        get time+date from internet ==> CHECK ==> success
        write time+date to csv

        get data from arduino
        write data from arduino to csv

        connect to mqtt broker ==> CHECK

===================================================================*/

//===========================================
// typdef struct
// {
//     int cmd_ACK;
//     String raw_result;
//     int cmd_results[8];
// } ctlrResults_t;

// ctlrResults_t ctlrResults; // This is a global variable

// void setup()
// {
//     do_something(&ctlrResults);
//     Serial.begin(115200);
//     Serial.print("Raw Results:");
//     Serial.println(ctlrResults.raw_result);
// }

// void loop()
// {
// }

//===========================================

// void do_something(ctlrResults_t *r)
// {
//     r->cmd_ACK = 1;
//     r->raw_result = "M1:323:M2:435";
//     r->cmd_results[0] = 323;
//     r->cmd_results[1] = 435;
// }

// typedef struct
// {
//     int8_t id;
//     int16_t messageCount;
//     int16_t time;
// } __attribute__((__packed__)) packet_header_t;

// typedef struct
// {
//     packet_header_t header;
//     int16_t rotationRate;
//     int16_t therm1;
//     int16_t therm2;
//     int16_t heading;
//     //uint32_t pressure;
//     int16_t airTemp;
//     int16_t checksum;
// } __attribute__((__packed__)) data_packet_t;

// data_packet_t dp;

// void setup()
// {
//     Serial.begin(115200);
//     dp.header.id = 99;
//     dp.header.messageCount = 0;
//     dp.header.time = 1;
//     dp.rotationRate = 2;
//     dp.therm1 = 3;
//     dp.therm2 = 4;
//     dp.heading = 5;
//     dp.airTemp = 6;
//     dp.checksum = 7;
// }

// void loop()
// {
//     unsigned short checkSum;

//     unsigned long uBufSize = sizeof(data_packet_t);
//     char pBuffer[uBufSize];

//     memcpy(pBuffer, &dp, uBufSize);
//     for (int i = 0; i < uBufSize; i++)
//     {
//         Serial.print(pBuffer[i]);
//     }
//     Serial.println();
// }