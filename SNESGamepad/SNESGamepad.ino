#include <Joystick.h>

#define DEBUG_MODE false

#define PIN_LATCH 14
#define PIN_CLOCK 15
#define PIN_DATA 16

#define SNES_A 8
#define SNES_B 0
#define SNES_X 9
#define SNES_Y 1

#define SNES_L 10
#define SNES_R 11
#define SNES_S 2
#define SNES_T 3

#define SNES_DU 4
#define SNES_DD 5
#define SNES_DL 6
#define SNES_DR 7

#define JOY_A 0
#define JOY_B 1
#define JOY_X 2
#define JOY_Y 3

#define JOY_L 4
#define JOY_R 5
#define JOY_S 6
#define JOY_T 7


Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID,JOYSTICK_TYPE_GAMEPAD,
  8, 0,                  // Button Count, Hat Switch Count
  true, true, false,     // X and Y, but no Z Axis
  false, false, false,   // No Rx, Ry, or Rz
  false, false,          // No rudder or throttle
  false, false, false);  // No accelerator, brake, or steering

void setup() {
  // Initialize Button Pins
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA,  INPUT);

  digitalWrite(17, 0);
  TXLED0

  // Initialize Joystick Library
  Joystick.begin(false);
  Joystick.setXAxisRange(-1, 1);
  Joystick.setYAxisRange(-1, 1);
}

// Last state of the buttons
int lastState[16] = {0,0,0,0,0,0,0,0,0,0,0,0};
int newState[16];

int handleButton(int index, int button) 
{
    if (newState[index] != lastState[index])
    {
        Joystick.setButton(button, newState[index]);
        return 1;
    }
    return 0;
}

int handleAxis(int axis, int positive, int negative) 
{
    if ( (newState[negative] != lastState[negative])  ||
         (newState[positive] != lastState[positive]) ) 
    {
        int result = newState[positive] - newState[negative];
        if (axis == 0)
            Joystick.setXAxis(result);
        else
            Joystick.setYAxis(result);
        return 1;
    }
    return 0;
}

void loop() {
  
    digitalWrite(PIN_LATCH, 1);
    delayMicroseconds(12);
    digitalWrite(PIN_LATCH, 0);
    delayMicroseconds(6);
    for (int i = 0; i < 16; i++)
    {
      newState[i] = !digitalRead(PIN_DATA);
      delayMicroseconds(6);
      digitalWrite(PIN_CLOCK, 1);
      delayMicroseconds(6);
      digitalWrite(PIN_CLOCK, 0);
    }

    int changes = 0;
    changes += handleButton(SNES_A, JOY_A);
    changes += handleButton(SNES_B, JOY_B);
    changes += handleButton(SNES_X, JOY_X);
    changes += handleButton(SNES_Y, JOY_Y);
    changes += handleButton(SNES_L, JOY_L);
    changes += handleButton(SNES_R, JOY_R);
    changes += handleButton(SNES_S, JOY_S);
    changes += handleButton(SNES_T, JOY_T);
  
    changes += handleAxis(0, SNES_DR, SNES_DL);
    changes += handleAxis(1, SNES_DD, SNES_DU);

    for(int i=0;i<16;i++) lastState[i] = newState[i];

    if (changes > 0) 
        Joystick.sendState();

    // milliseconds
    delay(1);
}
