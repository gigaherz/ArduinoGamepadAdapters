// Requires custom cores:
//   https://github.com/dmadison/ArduinoXInput_AVR
//   https://github.com/dmadison/ArduinoXInput_Sparkfun/
// Requires library:
//   https://github.com/dmadison/ArduinoXInput

#include <XInput.h>

#define DPAD_IS_HAT false
#define LR_IS_TRIGGERS true

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

#define JOY_A BUTTON_A
#define JOY_B BUTTON_B
#define JOY_X BUTTON_X
#define JOY_Y BUTTON_Y

#define JOY_L BUTTON_LB
#define JOY_R BUTTON_RB

#define JOY_S BUTTON_BACK
#define JOY_T BUTTON_START

#define JOY_DL DPAD_LEFT
#define JOY_DR DPAD_RIGHT
#define JOY_DU DPAD_UP
#define JOY_DD DPAD_DOWN

void setup() {
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA,  INPUT);

  digitalWrite(17, 0);
  TXLED0;
    
  XInput.setAutoSend(false);  // disable automatic output 
  XInput.setRange(JOY_LEFT, -1, 1);
  XInput.begin();
}

// Last state of the buttons
boolean lastState[16] = {0,0,0,0,0,0,0,0,0,0,0,0};
boolean newState[16];
int axisValues[3] = { -32768, 0, 32767 };

int handleButton(int index, int button) 
{
    if (newState[index] != lastState[index])
    {
        XInput.setButton(button, newState[index]);
        return 1;
    }
    return 0;
}

int handleTrigger(int trigger, int index) 
{
    if (newState[index] != lastState[index])
    {
        XInput.setTrigger(trigger, newState[index] ? 255 : 0);
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
            XInput.setJoystickX(JOY_LEFT, result);
        else
            XInput.setJoystickY(JOY_LEFT, result);
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
    changes += handleButton(SNES_S, JOY_S);
    changes += handleButton(SNES_T, JOY_T);
    
    if (LR_IS_TRIGGERS) 
    {
        changes += handleTrigger(TRIGGER_LEFT, SNES_L);
        changes += handleTrigger(TRIGGER_RIGHT, SNES_R);
    }
    else
    {
        changes += handleButton(SNES_L, JOY_L);
        changes += handleButton(SNES_R, JOY_R);
    }

    if (DPAD_IS_HAT) 
    {
        changes += handleButton(SNES_DL, JOY_DL);
        changes += handleButton(SNES_DR, JOY_DR);
        changes += handleButton(SNES_DU, JOY_DU);
        changes += handleButton(SNES_DD, JOY_DD);
    }
    else
    {
        changes += handleAxis(0, SNES_DR, SNES_DL);
        changes += handleAxis(1, SNES_DU, SNES_DD);
    }
 
    for(int i=0;i<16;i++) lastState[i] = newState[i];

    if (changes > 0) 
        XInput.send();

    // milliseconds
    delay(1);
}
