/*
    Program for the Arduino Nano wmachien
    
    Program to send data from arduino (serial, RX & TX)
        to the NodeMCU. Using SoftwareSerial on pins 7 (RX) and 8 (TX)
        Normal Serial port can be used for debugging

*/

// TODOS:
//  -   measure n times and average values, send those... DONE
//  -   add water control system
//  -   keep track of time? receive time from esp8266? keep track of time last wattered...

// LIBRARIES
#include <DHT.h>            // library for reading the DHT22 temp and air humidity sensor
#include <SoftwareSerial.h> // Software serial library for serial connection on arbitrary pins
#include <ArduinoJson.h>    // format data in json, to send to esp8266

// PINS ETC.
#define DHTPIN 2               // what pin the DHT22 is connected to (data, 10K resistor to 5V)
#define DHTTYPE DHT22          // DHT 22  (AM2302)
#define LIGHT_SENSOR A0        // analog pin for the light sensor
#define MOIST_SENSOR_TOP A1    // analog pin for the moisture sensor at the top
#define MOIST_SENSOR_BOTTOM A2 // analog pin for the moisture sensor at the bottom
#define LED_PIN 3              // led pin

// Water system
#define MOTOR_EN 4                                 // motor enable pin
#define MOTOR_IN1 5                                // motor dir pin 1
#define MOTOR_IN2 6                                // motor dir pin 2
#define MOTOR_LED 10                               // motor status led
unsigned long timeWaterStart;                      // in msec
const unsigned long waterGiveDuration = 10 * 1000; // in msec

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

    // init motor
    initMotor();

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
        readSensorData(); // read the data.
        break;
    case 'J':
        // send the requested data
        serialSendDataJson();    // send the data
        serialPrettyPrintData(); // print the sensor data
        break;
    case 'S':
        // switch the LED_PIN
        switchLED();
        break;
    case 'W':
        Serial.println("wattering is not finished");
        // give water to the plant! (holds system for 10 seconds... )
        waterPlant();
        // flush software serial after to discard commands during waterring
        softSerialFlush();
        break;
    }
}

/*
    Read sensor data
*/
int readSensorData()
{
    // number of successive reads
    int n = 5;
    // init data arrays
    float hum_val_array[n];    // float hum_val;
    float temp_val_array[n];   // float temp_val;
    int light_level_array[n];  // int light_level;
    int moist_top_array[n];    // int moist_top;
    int moist_bottom_array[n]; // int moist_bottom;

    // read sensor data n times
    for (int i = 0; i < n; i++)
    {
        // read dht sensor data
        do
        {
            hum_val_array[i] = dht.readHumidity();
            temp_val_array[i] = dht.readTemperature();
            // check if nans, if so, repeat measurement
        } while (isnan(hum_val_array[i]) || isnan(temp_val_array[i]));

        // read light level (1 if light, 0 if dark)
        light_level_array[i] = !digitalRead(LIGHT_SENSOR); // will be one or zero (invert since sensor gives inverted val)

        // read soil moisture (inverse, makes more sense)
        moist_top_array[i] = 1024 - analogRead(MOIST_SENSOR_TOP);
        moist_bottom_array[i] = 1024 - analogRead(MOIST_SENSOR_BOTTOM);
    }

    // calculate the averages
    hum_val = calcAverage(hum_val_array, n);
    temp_val = calcAverage(temp_val_array, n);
    light_level = calcAverage(light_level_array, n);
    moist_top = calcAverage(moist_top_array, n);
    moist_bottom = calcAverage(moist_bottom_array, n);

    return 1;
}

// functions to calculate averages of arrays (int and float arrays)
float calcAverage(float *arrayIn, int len) // assuming array is int.
{
    float sum = 0;
    for (int i = 0; i < len; i++)
        sum += arrayIn[i];

    return ((float)sum) / len; // average will be fractional, so float may be appropriate.
}
int calcAverage(int *arrayIn, int len) // assuming array is int.
{
    long sum = 0L;
    for (int i = 0; i < len; i++)
        sum += arrayIn[i];

    return (int)round(((float)sum) / len); // average will be fractional, so float may be appropriate.
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

// function to init the motor
void initMotor()
{
    // set pinmode
    pinMode(MOTOR_EN, OUTPUT);
    pinMode(MOTOR_IN1, OUTPUT);
    pinMode(MOTOR_IN2, OUTPUT);
    pinMode(MOTOR_LED, OUTPUT);
    // write all low, except for dir
    digitalWrite(MOTOR_EN, LOW);
    digitalWrite(MOTOR_LED, LOW);
    // Set initial rotation direction
    digitalWrite(MOTOR_IN1, HIGH); // reverse
    digitalWrite(MOTOR_IN2, LOW);  // reverse
}

// function to water the plant!
void waterPlant()
{
    // status message
    Serial.println("waterring");

    // start water moment
    timeWaterStart = millis();
    digitalWrite(MOTOR_EN, HIGH);
    digitalWrite(MOTOR_LED, HIGH);

    // while loop to water
    while (millis() - timeWaterStart < waterGiveDuration)
    {
        ;
    }

    // stop motor
    digitalWrite(MOTOR_EN, LOW);
    digitalWrite(MOTOR_LED, LOW);
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