// #define BUTTON_PIN 3
// #define LED_PIN 2
// #define MOTOR_EN 7
// #define MOTOR_IN1 8
// #define MOTOR_IN2 9

// bool button_status = false;

// void setup()
// {
//     // leds and buttons, simple io
//     pinMode(BUTTON_PIN, INPUT);
//     pinMode(LED_PIN, OUTPUT);
//     pinMode(LED_BUILTIN, OUTPUT);

//     // motor
//     pinMode(MOTOR_EN, OUTPUT);
//     pinMode(MOTOR_IN1, OUTPUT);
//     pinMode(MOTOR_IN2, OUTPUT);

//     // init motor
//     digitalWrite(MOTOR_EN, LOW);
//     // Set initial rotation direction
//     // digitalWrite(MOTOR_IN1, LOW);
//     // digitalWrite(MOTOR_IN2, HIGH);
//     digitalWrite(MOTOR_IN1, HIGH); // reverse
//     digitalWrite(MOTOR_IN2, LOW);  // reverse
// }

// void loop()
// {
//     button_status = digitalRead(BUTTON_PIN);

//     if (button_status)
//     {
//         digitalWrite(LED_PIN, HIGH);
//         digitalWrite(LED_BUILTIN, HIGH);
//         digitalWrite(MOTOR_EN, HIGH);
//     }
//     else
//     {
//         digitalWrite(LED_PIN, LOW);
//         digitalWrite(LED_BUILTIN, LOW);
//         digitalWrite(MOTOR_EN, LOW);
//     }
// }

// =====================================

void setup()
{
    pinMode(7, INPUT);
    Serial.begin(115200);
}

void loop()
{
    int r = digitalRead(7);
    if (digitalRead(7))
    {
        Serial.println(r);
    }
    else
    {
        Serial.println(r);
    }
    delay(100);
}