/*
    Program for the Arduino Nano wmachien
    
    Program to send data from arduino (serial, RX & TX)
        to the NodeMCU. Using SoftwareSerial on pins 7 (RX) and 8 (TX)
        Normal Serial port can be used for debugging

*/

// LIBRARIES
#include <DHT.h>            // library for reading the DHT22 temp and air humidity sensor
#include <SoftwareSerial.h> // Software serial library for serial connection on arbitrary pins
#include <ArduinoJson.h>    // format data in json, to send to esp8266
// #include <TimeLib.h>     // library for handling time

// PINS ETC.
#define DHTPIN 2               // what pin the DHT22 is connected to (data, 10K resistor to 5V)
#define DHTTYPE DHT22          // DHT 22  (AM2302)
#define LIGHT_SENSOR A0        // analog pin for the light sensor
#define MOIST_SENSOR_TOP A1    // analog pin for the moisture sensor at the top
#define MOIST_SENSOR_BOTTOM A2 // analog pin for the moisture sensor at the bottom
#define LED_PIN 3              // led pin

// define global sensor data variables
float hum_val;
float temp_val;
int light_level;
int moist_top;
int moist_bottom;

// shit for led
bool LED_status = false;
unsigned long timeSinceSwitch = 0;

// SoftwareSerial
SoftwareSerial ssa(7, 8); // SoftwareSerial(RX, TX), make sure RX to TX, and TX to RX...

// Serial shit
// String inputString = "";     // a String to hold incoming data
// bool stringComplete = false; // whether the string is complete

// Struct to contain sensor data
typedef struct
{
    int moist_top;
    int moist_bot;
    int light_level;
    float hum_val;
    float temp_val;
    // etc...
} sensorData_t;

sensorData_t sensorData; // create instance of struct definition

// instance of DHT sensor
DHT dht(DHTPIN, DHTTYPE);

void setup()
{
    // begin Serial (debug) and SoftwareSerial (communication arduino)
    Serial.begin(115200);
    ssa.begin(115200);

    // begin DHT sensor
    dht.begin();

    // set pinmode for different sensors
    pinMode(LIGHT_SENSOR, INPUT);
    pinMode(MOIST_SENSOR_TOP, INPUT);
    pinMode(MOIST_SENSOR_BOTTOM, INPUT);

    // led for debugging
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // perform initial sensor read
    if (!readSensorData())
    {
        // if failed, turn led on...
        digitalWrite(LED_PIN, HIGH);
    }

    // let know booted
    Serial.println("Ready for work");
}

void loop()
{
    // serial events...
    char receive = NULL;
    if (ssa.available() > 0)
    {
        receive = ssa.read();
        Serial.print(receive);
    }

    switch (receive)
    {
    case 'D':
        // data is requested
        readSensorData();        // read the data.
        serialSendDataJson();    // send the data
        serialPrettyPrintData(); // print the sensor data
    case 'S':
        // switch the LED_PIN
        switchLED();
    }
    // Serial.println("dit is een test");
    // serialPrettyPrintData();
    // serialSendDataJson();
    // delay(5000);
}

/*
    Read sensor data
        returns 1 if successful
        returns 0 if read e.g. nan
*/
int readSensorData()
{
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    hum_val = dht.readHumidity();
    // Read temperature as Celsius
    temp_val = dht.readTemperature();

    // read light level
    light_level = digitalRead(LIGHT_SENSOR); // will be one or zero

    // read soil moisture
    moist_top = analogRead(MOIST_SENSOR_TOP);
    moist_bottom = analogRead(MOIST_SENSOR_BOTTOM);

    // Check if any reads failed and exit early (to try again).
    if (isnan(hum_val) || isnan(temp_val))
    {
        return 0;
    }
    return 1;
}

/*
    print the serial data in csv format
*/
void serialPrintCSVData()
{
    // print results
    Serial.print(hum_val);
    Serial.print(",");
    Serial.print(temp_val);
    Serial.print(",");
    Serial.print(light_level);
    Serial.print(",");
    Serial.print(moist_top);
    Serial.print(",");
    Serial.println(moist_bottom);
}

int serialSendDataJson()
{
    // make Json buffer
    StaticJsonBuffer<400> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    // set values
    root["hv"] = hum_val;      // root["hum_val"] = hum_val;
    root["tv"] = temp_val;     // root["temp_val"] = temp_val;
    root["ll"] = light_level;  // root["light_level"] = light_level;
    root["mt"] = moist_top;    // root["moist_top"] = moist_top;
    root["mb"] = moist_bottom; // root["moist_bottom"] = moist_bottom;

    // send json bubber
    root.printTo(ssa);
    Serial.println("printed buffer");

    // // check answer from nano
    // while (ssa.available() > 0)
    // {
    //     char answer = Serial.read();
    //     if (answer == 'y')
    //     {
    //         // success
    //         Serial.println("success");
    //         return 1;
    //     }
    // }
    // no success
    return 1;
}

// pretty print the data over serial
void serialPrettyPrintData()
{
    // print results
    Serial.print("Humidity: ");
    Serial.print(hum_val);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(temp_val);
    Serial.print(" *C \t");
    Serial.print("Light: ");
    Serial.print(light_level);
    Serial.print("\t Moist top: ");
    Serial.print(moist_top);
    Serial.print("\t Moist bottom: ");
    Serial.println(moist_bottom);
}

// function to switch the led
void switchLED()
{
    LED_status = !LED_status;
    digitalWrite(LED_PIN, LED_status);
}