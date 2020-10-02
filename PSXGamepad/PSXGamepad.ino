// Requires custom cores:
//   https://github.com/dmadison/ArduinoXInput_AVR
//   https://github.com/dmadison/ArduinoXInput_Sparkfun/
// Requires library:
//   https://github.com/dmadison/ArduinoXInput

#include <Joystick.h>

#define SERIAL_DEBUG true

// ============X============
// | o o o | o o o | o o o | Controller plug
//  \_____________________/
//   1 2 3   4 5 6   7 8 9
//
//   Controller    Arduino PIN   COLOR        SPI
//   1 - Data      8             Brown        MISO|PB3
//   2 - Command   6             Orange       MOSI|PB2
//   3 - +9V(opt)  ---------     Purple
//   4 - GND       GND           Gray + Black
//   5 - Vcc       VCC           Red
//   6 - ATT       7             Yellow       /SS|PB4
//   7 - Clock     4             Blue         CLK|PB1
//   8 - N/C       ---------     -----
//   9 - ACK       5             Green
//   X - Shielding? Presence?

#define PIN_ACK  7
#define PIN_CS   3
#define PIN_CLK  4
#define PIN_MISO 8
#define PIN_MOSI 6

#define PIN_EXTRA 16

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID,JOYSTICK_TYPE_GAMEPAD,
  13, 1,                  // Button Count, Hat Switch Count
  true, true, true,     // X and Y, but no Z Axis
  false, false, true,   // No Rx, Ry, or Rz
  false, false,          // No rudder or throttle
  false, false, false);  // No accelerator, brake, or steering


void delayHelper(int microseconds) {
  delayMicroseconds(microseconds);
  //delay(/*debug milliseconds*/ microseconds * 10);
}

uint8_t exchangeOneByte(uint8_t data)
{
  int i;
  uint8_t d = 0;

  for (i = 0; i < 8; i++)
  {
    delayHelper(10);
    digitalWrite(PIN_CLK, 0);

    digitalWrite(PIN_MOSI, (data >> i) & 1);

    delayHelper(10);
    digitalWrite(PIN_CLK, 1);

    d |= (digitalRead(PIN_MISO) << i);
  }

  return d;
}

void sendOneByte(uint8_t data)
{
  int i;

  for (i = 0; i < 8; i++)
  {
    delayHelper(10);
    digitalWrite(PIN_CLK, 0);

    digitalWrite(PIN_MOSI, (data >> i) & 1);

    delayHelper(10);
    digitalWrite(PIN_CLK, 1);
  }
}

void waitForAck(void)
{
  int loops = 500; // 200 loops at 16mhz are 12 microseconds if it was 1 cycle per iteration, so probably a lot more.

  for (int i = 0; i < loops; i++)
  {
    if (!digitalRead(PIN_ACK))
      break;
  }
}

void sendMessage(uint8_t bytes[], int length)
{
  int i;

  digitalWrite(PIN_CS, 0);

  delayHelper(5);

  length++;

  for (i = 0; i < length; i++)
  {
    sendOneByte((uint8_t)bytes[i]);
    waitForAck();
  }

  sendOneByte((uint8_t)bytes[i]);

  digitalWrite(PIN_CS, 1);
}

void exchangeMessage(uint8_t *result, int reslen, uint8_t *command, int cmdlen)
{
  int i, mode, msgLength;

  digitalWrite(PIN_CS, 0);

  delayHelper(5);

  result[0] = exchangeOneByte(command[0]);
  waitForAck();
  result[1] = exchangeOneByte(command[1]);
  waitForAck();

  mode = (result[1] >> 4);
  msgLength = (result[1] & 15) * 2 + 3;

  int maxlen = (reslen > cmdlen ? reslen : cmdlen);

  if (msgLength > maxlen)
    msgLength = maxlen;

  msgLength--;

  for (i = 2; i <= msgLength; i++)
  {
    int a = (i < reslen);
    int b = (i < cmdlen);

    if (a && b)
    {
      result[i] = exchangeOneByte(command[i]);
    }
    else if (a)
    {
      result[i] = exchangeOneByte(0x00);
    }
    else
    {
      exchangeOneByte(command[i]);
    }

    if (i < msgLength)
      waitForAck();
  }

  digitalWrite(PIN_CS, 1);
}

void sendModeChange(uint8_t enter)
{
  uint8_t ModeChange[] = {0x01, 0x43, 0x00, enter };

  sendMessage( ModeChange, sizeof(ModeChange) );
}

void setActuatorMapping(uint8_t enable)
{
  static uint8_t ActuatorsMap[] = {0x01, 0x4d, 0x00, 0xFF, 0xFF, 0xff, 0xff, 0xff, 0xff };

  if (enable)
  {
    ActuatorsMap[3] = 0x00; // Map output byte 3 to actuator 00
    ActuatorsMap[4] = 0x01; // Map output byte 4 to actuator 01
  }

  sendMessage( ActuatorsMap, sizeof(ActuatorsMap) );
}

void setControllerMode(uint8_t mode, uint8_t lock)
{
  uint8_t Mode[] = {0x01, 0x44, 0x00, mode, lock, 0x00, 0x00, 0x00, 0x00 };

  sendMessage( Mode, sizeof(Mode) );

}

void sendVibrationLevels(uint8_t small, uint8_t big)
{
  uint8_t ShockString4[] = {0x01, 0x42, 0x00, small, big, 0x01 };

  sendMessage( ShockString4, sizeof(ShockString4) );
}

void setDualShockEnabled(uint8_t enable)
{
  if (!enable)
  {
    sendVibrationLevels(0, 0);
  }

  sendModeChange(1);
  setActuatorMapping(enable);
  sendModeChange(0);
}

void changeControllerMode(uint8_t mode, uint8_t lock)
{
  sendModeChange(1);
  setControllerMode(mode, lock);
  sendModeChange(0);
}

void receiveMessage(uint8_t *data, int buflen)
{
  int i, mode, msgLength;

  data[0] = 0x5c;
  data[1] = 0x5c;
  data[2] = 0x5c;
  data[3] = 0x5c;
  data[4] = 0x5c;
  data[5] = 0x5c;
  data[6] = 0x5c;
  data[7] = 0x5c;
  data[8] = 0x5c;
  data[9] = 0x5c;

  digitalWrite(PIN_CS, 0);

  delayHelper(5);

  data[0] = exchangeOneByte(0x01);
  waitForAck();
  data[1] = exchangeOneByte(0x42);
  waitForAck();

  mode = data[1] >> 4;
  msgLength = (data[1] & 15) * 2 + 1;

  if (msgLength > (buflen - 2))
    msgLength = (buflen - 2);

  msgLength--;

  for (i = 0; i < msgLength; i++)
  {
    data[i + 2] = exchangeOneByte(0x00);
    waitForAck();
  }

  data[i + 2] = exchangeOneByte(0x00);

  digitalWrite(PIN_CS, 1);
}

// 12 buttons + 4 dpad + 4 axis
struct PSX_t {
  int Mode;
  struct BUTTONS_t {
    boolean Sl;
    boolean L3;
    boolean R3;
    boolean St;
    boolean Up;
    boolean Rt;
    boolean Dn;
    boolean Lt;
    boolean L2;
    boolean R2;
    boolean L1;
    boolean R1;
    boolean T;
    boolean C;
    boolean X;
    boolean S;
  } Buttons;
  struct AXIS_t {
    uint8_t RX;
    uint8_t RY;
    uint8_t LX;
    uint8_t LY;
  } Axes;
} oldPsx = {0}, psx = {0};

int oldExtraButton = 0;
int oldHat = -1;

void printPsx(struct PSX_t * data) {
  char text[200];
  sprintf(text,
          "MODE: %d ||DPAD: {%d,%d,%d,%d} || BTN: {%d,%d,%d,%d},{%d,%d},{%d,%d},{%d,%d},{%d} || AXIS: {%d,%d},{%d,%d}\n",
          data->Mode,
          data->Buttons.Up, data->Buttons.Rt, data->Buttons.Dn, data->Buttons.Lt,
          data->Buttons.X, data->Buttons.C, data->Buttons.S, data->Buttons.T,
          data->Buttons.Sl, data->Buttons.St,
          data->Buttons.L1, data->Buttons.R1,
          data->Buttons.L2, data->Buttons.R2,
          data->Buttons.L3, data->Buttons.R3,
          data->Axes.LX, data->Axes.LY,
          data->Axes.RX, data->Axes.RY
         );
  Serial.print(text);
}

void setup() {
  // Setup pins
  pinMode(PIN_ACK,  INPUT_PULLUP);
  pinMode(PIN_CS,  OUTPUT);
  pinMode(PIN_MISO, INPUT_PULLUP);
  pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_CLK,  OUTPUT);

  // Custom hardware button, maps to the xbox logo action
  pinMode(PIN_EXTRA, INPUT_PULLUP);

  // Turn off transfer leds
  digitalWrite(17, 0);
  TXLED0;

  changeControllerMode(1, 0); // analog, locked (PS2 Only?)

  //XInput.setAutoSend(false);  // disable automatic output
  // Initialize Joystick Library
  Joystick.begin(false);
  Joystick.setXAxisRange(0, 255);
  Joystick.setYAxisRange(0, 255);
  Joystick.setZAxisRange(0, 255);
  Joystick.setRzAxisRange(0, 255);

#if SERIAL_DEBUG
  Serial.begin(115200);
  Serial.println("HI!");
#endif
}

int handleButton(int button, int newValue, int oldValue)
{
  if (newValue != oldValue)
  {
    Joystick.setButton(button, newValue);
    return 1;
  }
  return 0;
}

int handleAxisAnalog(int joystick, int axis, int newValue, int oldValue, boolean invert = false)
{
  if ( newValue != oldValue )
  {
    if (joystick == 0)
    {
      if (axis == 0)
        Joystick.setXAxis(invert ? 255 - newValue : newValue);
      else
        Joystick.setYAxis(invert ? 255 - newValue : newValue);
    }
    else
    {
      if (axis == 0)
        Joystick.setZAxis(invert ? 255 - newValue : newValue);
      else
        Joystick.setRzAxis(invert ? 255 - newValue : newValue);
    }
    return 1;
  }
  return 0;
}

void loop()
{
  uint8_t data[10] = { 0 };

  receiveMessage(data, sizeof(data));

  int mode = psx.Mode = (data[1]) >> 4;

  psx.Buttons.Sl = ((data[3] >> 0) & 1) ^ 1;
  psx.Buttons.L3 = ((data[3] >> 1) & 1) ^ 1;
  psx.Buttons.R3 = ((data[3] >> 2) & 1) ^ 1;
  psx.Buttons.St = ((data[3] >> 3) & 1) ^ 1;

  uint8_t Up = ((data[3] >> 4) & 1) ^ 1;
  uint8_t Rt = ((data[3] >> 5) & 1) ^ 1;
  uint8_t Dn = ((data[3] >> 6) & 1) ^ 1;
  uint8_t Lt = ((data[3] >> 7) & 1) ^ 1;

  psx.Buttons.L2 = ((data[4] >> 0) & 1) ^ 1;
  psx.Buttons.R2 = ((data[4] >> 1) & 1) ^ 1;
  psx.Buttons.L1 = ((data[4] >> 2) & 1) ^ 1;
  psx.Buttons.R1 = ((data[4] >> 3) & 1) ^ 1;
  psx.Buttons.T  = ((data[4] >> 4) & 1) ^ 1;
  psx.Buttons.C  = ((data[4] >> 5) & 1) ^ 1;
  psx.Buttons.X  = ((data[4] >> 6) & 1) ^ 1;
  psx.Buttons.S  = ((data[4] >> 7) & 1) ^ 1;

  if (mode != 4)
  {
    psx.Axes.RX = data[5];
    psx.Axes.RY = data[6];
    psx.Axes.LX = data[7];
    psx.Axes.LY = data[8];

    psx.Buttons.Up = Up;
    psx.Buttons.Rt = Rt;
    psx.Buttons.Dn = Dn;
    psx.Buttons.Lt = Lt;
  }
  else
  {
    psx.Axes.LX = 0x80;
    psx.Axes.LY = 0x80;
    psx.Axes.RX = 0x80;
    psx.Axes.RY = 0x80;

    if (Lt && !Rt)
    {
      psx.Axes.LX = 0;
    }
    else if (Rt && !Lt)
    {
      psx.Axes.LX = 0xFF;
    }

    if (Up && !Dn)
    {
      psx.Axes.LY = 0xFF;
    }
    else if (Dn && !Up)
    {
      psx.Axes.LY = 0x00;
    }
  }

  int extraButton = !digitalRead(PIN_EXTRA);

  int changes = 0;

  // Dpad buttons
  static int hatAngles[16] = {
    /*D,L,U,R*/
    /*0,0,0,0*/JOYSTICK_HATSWITCH_RELEASE,
    /*1,0,0,0*/0,
    /*0,1,0,0*/2,
    /*1,1,0,0*/1,    
    /*0,0,1,0*/4,
    /*1,0,1,0*/JOYSTICK_HATSWITCH_RELEASE,
    /*0,1,1,0*/3,
    /*1,1,1,0*/2,    
    /*0,0,0,1*/6,
    /*1,0,0,1*/7,
    /*0,1,0,1*/JOYSTICK_HATSWITCH_RELEASE,
    /*1,1,0,1*/0,    
    /*0,0,1,1*/5,
    /*1,0,1,1*/6,    
    /*0,1,1,1*/4,    
    /*1,1,1,1*/JOYSTICK_HATSWITCH_RELEASE,
    };
  int di = (psx.Buttons.Lt <<3) + (psx.Buttons.Dn << 2) + (psx.Buttons.Rt << 1) + psx.Buttons.Up;
  int dHat = hatAngles[di] * 45;
  if (dHat != oldHat) {
    Joystick.setHatSwitch(0, dHat);
    changes += 1;
  }

  // Face buttons
  changes += handleButton(0, psx.Buttons.X, oldPsx.Buttons.X);
  changes += handleButton(1, psx.Buttons.C, oldPsx.Buttons.C);
  changes += handleButton(2, psx.Buttons.S, oldPsx.Buttons.S);
  changes += handleButton(3, psx.Buttons.T, oldPsx.Buttons.T);

  // Shoulder buttons
  changes += handleButton(4, psx.Buttons.L1, oldPsx.Buttons.L1);
  changes += handleButton(5, psx.Buttons.R1, oldPsx.Buttons.R1);
  changes += handleButton(6, psx.Buttons.L2, oldPsx.Buttons.L2);  // TODO: Analog triggers in DS2 mode
  changes += handleButton(7, psx.Buttons.R2, oldPsx.Buttons.R2);

  // Center buttons and stick buttons
  changes += handleButton(8, psx.Buttons.Sl, oldPsx.Buttons.Sl);
  changes += handleButton(9, psx.Buttons.St, oldPsx.Buttons.St);
  changes += handleButton(10, psx.Buttons.L3, oldPsx.Buttons.L3);
  changes += handleButton(11, psx.Buttons.R3, oldPsx.Buttons.R3);

  // Axes
  changes += handleAxisAnalog(0, 0, psx.Axes.LX, oldPsx.Axes.LX);
  changes += handleAxisAnalog(0, 1, psx.Axes.LY, oldPsx.Axes.LY);
  changes += handleAxisAnalog(1, 0, psx.Axes.RX, oldPsx.Axes.RX);
  changes += handleAxisAnalog(1, 1, psx.Axes.RY, oldPsx.Axes.RY);

  // Special
  changes += handleButton(12, extraButton, oldExtraButton);

  oldPsx = psx;
  oldExtraButton = extraButton;
  oldHat = dHat;
  if (changes > 0)
    Joystick.sendState();

  // milliseconds
  delay(1);

#if SERIAL_DEBUG
  if (changes > 0)
    printPsx(&psx);
#endif
}
