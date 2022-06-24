// Requires custom cores:
//   https://github.com/dmadison/ArduinoXInput_AVR
//   https://github.com/dmadison/ArduinoXInput_Sparkfun/
// Requires library:
//   https://github.com/dmadison/ArduinoXInput

#include <math.h>
#include <XInput.h>

#define DEBUG_SERIAL false

#if DEBUG_SERIAL
#define SOFT_SERIAL false
#define DEBUG_CMD false
#define DEBUG_PSX true
#endif

#if SOFT_SERIAL
#define SERIAL debugSerial
#else
#define SERIAL Serial
#endif

#define USE_DEADZONE true
#define DEADZONE 48
#define DEADZONE_TRIG true

#define ENABLE_RUMBLE true
#define DEBUG_RUMBLE false

#define ANALOG_BUTTONS false

/* 0 or positive number: success, negative number: error code */
static int error; /* used to contain the error code for the DO macros */
static int _error_source;

// when true, adds a delay between commands
static boolean initializationDelayHack = false;

typedef int32_t RESULT;
#define STATUS_SUCCESS 0
#define STATUS_NO_ACK -1

#define DO(statement) if ((error = statement) < 0) return error;
#define DO_AND_ASSIGN(target, statement) if ((error = statement) < 0) return error; else target = error;
#define DO_R(statement, err) if ((error = statement) < 0) { _error_source = (err); return error; }
#define DO_AND_ASSIGN_R(target, statement, err) if ((error = statement) < 0) { _error_source = (err); return error; } else target = error;

#if SOFT_SERIAL
#include <SoftwareSerial.h>

SoftwareSerial debugSerial(14, 15); // RX, TX
#endif

#define SLOWMODE_DELAY (20 * 1000) /* 20 seconds */
int32_t cyclesWithoutActivity = 0;

// ============X============
// | o o o | o o o | o o o | Controller plug
//  \_____________________/
//   1 2 3   4 5 6   7 8 9
//
//   Controller    COLOR        SPI
//   1 - Data      Brown        MISO
//   2 - Command   Orange       MOSI
//   3 - +9V(opt)  Purple
//   4 - GND       Gray + Black
//   5 - Vcc       Red
//   6 - ATT       Yellow       /SS|PB4
//   7 - Clock     Blue         CLK|PB1
//   8 - N/C
//   9 - ACK       Green
//   X - Shielding? Presence?

#define PIN_ACK  7
#define PIN_CS   3
#define PIN_CLK  4
#define PIN_MISO 8
#define PIN_MOSI 6

#define PIN_EXTRA 16

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
  struct ANALOG_BUTTONS_t {
    uint8_t Rt;
    uint8_t Lt;
    uint8_t Up;
    uint8_t Dn;
    uint8_t T;
    uint8_t C;
    uint8_t X;
    uint8_t S;
    uint8_t L1;
    uint8_t R1;
    uint8_t L2;
    uint8_t R2;
  } AnalogButtons;
} oldPsx = {0}, psx = {0};

#define RESPONSE_BIT_BUTTONS1        0x00001L
#define RESPONSE_BIT_BUTTONS2        0x00002L
#define RESPONSE_BIT_RX              0x00004L
#define RESPONSE_BIT_RY              0x00008L
#define RESPONSE_BIT_LX              0x00010L
#define RESPONSE_BIT_LY              0x00020L
#define RESPONSE_BIT_ANALOG_LEFT     0x00040L
#define RESPONSE_BIT_ANALOG_RIGHT    0x00080L
#define RESPONSE_BIT_ANALOG_UP       0x00100L
#define RESPONSE_BIT_ANALOG_DOWN     0x00200L
#define RESPONSE_BIT_ANALOG_TRIANGLE 0x00400L
#define RESPONSE_BIT_ANALOG_CIRCLE   0x00800L
#define RESPONSE_BIT_ANALOG_CROSS    0x01000L
#define RESPONSE_BIT_ANALOG_SQUARE   0x02000L
#define RESPONSE_BIT_ANALOG_L1       0x04000L
#define RESPONSE_BIT_ANALOG_R1       0x08000L
#define RESPONSE_BIT_ANALOG_L2       0x10000L
#define RESPONSE_BIT_ANALOG_R2       0x20000L

#define ANALOG_BUTTONS 0 // (RESPONSE_BIT_ANALOG_L2 | RESPONSE_BIT_ANALOG_R2)

int oldExtraButton = 0;
#if DEBUG_SERIAL
int oldBigRumble = 0;
int oldSmallRumble = 0;
#endif

int isConfigurationMode = 0;

/* startup rumble animation */
int smallMotorPulse = 0;
int bigMotorPulse = 0;

void DEBUG_blinkStage(int stage) {
#if false
  delay(500);
  for (int i = 0; i < stage; i++)
  {
    delay(100);
    digitalWrite(17, 0);
    delay(100);
    digitalWrite(17, 1);
    ftoa()
  }
#endif
}

#if DEBUG_SERIAL
void DEBUG_printf(const char* fmt, ...)
{
  char str[200];
  va_list args;
  va_start(args, fmt);
  vsprintf(str, fmt, args);
  va_end(args);
  SERIAL.print(str);
}
#define DBGPRINT DEBUG_printf
#else
#define DBGPRINT(f, a...)
#endif

/* pin abstraction */
boolean PIN_readAck() {
  return !digitalRead(PIN_ACK);
}
void PIN_beginMessage() {
  digitalWrite(PIN_CS, 0);
}
void PIN_endMessage() {
  digitalWrite(PIN_CS, 1);
}
void PIN_clockOn() {
  digitalWrite(PIN_CLK, 0);
}
void PIN_clockOff() {
  digitalWrite(PIN_CLK, 1);
}
void PIN_clockDelay() {
  delayMicroseconds(10);
}
void PIN_dataOut(boolean bit) {
  digitalWrite(PIN_MOSI, bit);
}
boolean PIN_dataIn() {
  return digitalRead(PIN_MISO);
}

volatile boolean ackReceived = false;
void PSX_ackReceived() {
  ackReceived = true;
}

RESULT PSX_exchangeOneByte(int data, boolean ack)
{
  int d = 0;

  ackReceived = false;

  for (int i = 0; i < 8; i++)
  {
    boolean bitOut = (data >> i) & 1;

    // down tick
    PIN_clockOn();
    PIN_dataOut(bitOut);
    PIN_clockDelay();

    // up tick
    PIN_clockOff();
    boolean bitIn = PIN_dataIn();
    PIN_clockDelay();

    d |= bitIn << i;
  }

  if (ack)
  {
    int loops = 20; // 200 loops at 16mhz are 12 microseconds if it was 1 cycle per iteration, so probably a lot more... but just to be safe.
    while ((loops--) > 0)
    {
      delayMicroseconds(2);
      if (ackReceived)
        break;
    }
    if (!ackReceived)
    {
      return STATUS_NO_ACK; // Did not receive an ACK
    }
  }

  return d;
}

RESULT PSX_exchangeMessage0(uint8_t result[], int reslen, uint8_t command[], int cmdlen)
{
  PIN_clockDelay();

  DO_AND_ASSIGN_R(result[0], PSX_exchangeOneByte(command[0], true), 2);
  DO_AND_ASSIGN_R(result[1], PSX_exchangeOneByte(command[1], true), 3);
  DO_AND_ASSIGN_R(result[2], PSX_exchangeOneByte(command[2], true), 4);

  int mode = (result[1] >> 4);
  int words = result[1] & 15;
  int totalLength = words * 2 + 3;

  isConfigurationMode = mode == 0xF;

  int limit = totalLength - 1;
  for (int i = 3; i <= limit; i++)
  {
    int cmdByte = (i < cmdlen) ? command[i] : 0x00;
    int ack = i < limit;

    if (i < reslen)
    {
      DO_AND_ASSIGN_R(result[i], PSX_exchangeOneByte(cmdByte, ack), 5);
    }
    else
    {
      DO_R(PSX_exchangeOneByte(cmdByte, ack), 5);
    }
  }

  return totalLength;
}

RESULT PSX_exchangeMessage(uint8_t result[], int reslen, uint8_t command[], int cmdlen)
{
#if DEBUG_CMD
  SERIAL.print("Sending: ");
  for (int i = 0; i < cmdlen; i++) {
    static char dbg[10];
    sprintf(dbg, "%02x ", command[i]);
    SERIAL.print(dbg);
  }
#endif

  PIN_beginMessage();

  PIN_clockDelay();
  PIN_clockDelay();

  RESULT r = PSX_exchangeMessage0(result, reslen, command, cmdlen);

#if DEBUG_CMD
  SERIAL.print(" -  received: ");
  for (int i = 0; i < r; i++) {
    static char dbg[10];
    sprintf(dbg, "%02x ", result[i]);
    SERIAL.print(dbg);
  }
  SERIAL.println();
  delay(5);
#endif

  if (initializationDelayHack)
    delay(5);

  PIN_clockDelay();
  PIN_clockDelay();

  PIN_endMessage();

  return r;
}

RESULT PSX_sendMessage(uint8_t bytes[], int length)
{
  uint8_t response[64];

  return PSX_exchangeMessage(response, sizeof(response), bytes, length);
}

RESULT PSX_sendConfigurationModeMessage43(boolean enter)
{
  uint8_t ModeChange[] = {0x01, 0x43, 0x00, enter ? 0x01 : 0x00 };

  DO_R(PSX_sendMessage( ModeChange, sizeof(ModeChange) ), 6)
  return error;
}

RESULT PSX_sendActuatorMappingMessage4D(boolean enable)
{
  uint8_t ActuatorsMap[] = {0x01, 0x4D, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

  if (enable)
  {
    ActuatorsMap[3] = 0x00; // Map output byte 3 to actuator 00
    ActuatorsMap[4] = 0x01; // Map output byte 4 to actuator 01
  }

  DO_R(PSX_sendMessage( ActuatorsMap, sizeof(ActuatorsMap) ), 7)
  return error;
}

RESULT PSX_sendResponseFormatMessage41()
{
  uint8_t ResponseMessage[] = {0x01, 0x41, 0x00, 0x5AF, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A };
  uint8_t ResponseFormat[9];

  DO_R(PSX_exchangeMessage( ResponseFormat, sizeof(ResponseFormat), ResponseMessage, sizeof(ResponseMessage) ), 7)

  uint32_t fmt = ResponseFormat[5];
  fmt = (fmt<<8)| ResponseFormat[4]; 
  fmt = (fmt<<8)| ResponseFormat[3]; 

  return fmt;
}

#define VALID_MASK  0x3FFFFL
#define FORCED_BITS 0x0003FL
RESULT PSX_sendResponseBytesMessage4F(uint32_t bitField)
{
  uint8_t ResponseBytesMessage[] = {0x01, 0x4F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

  bitField = (bitField | FORCED_BITS) & VALID_MASK;

  ResponseBytesMessage[3] = (bitField) & 0xFF;
  ResponseBytesMessage[4] = (bitField >> 8) & 0xFF;
  ResponseBytesMessage[5] = (bitField >> 16) & 0x03;

  DO_R(PSX_sendMessage( ResponseBytesMessage, sizeof(ResponseBytesMessage) ), 10)
  return error;
}

RESULT PSX_exchangeControllerStatus42(uint8_t result[], uint8_t reslen, uint8_t small, uint8_t big)
{
  uint8_t ShockString4[] = {0x01, 0x42, 0x00, small, big, 0x01 };

  DO_R(PSX_exchangeMessage( result, reslen, ShockString4, sizeof(ShockString4) ), 8)
  return error;
}

RESULT PSX_sendControllerModeMessage44(boolean analog, boolean lock)
{
  uint8_t Mode[] = {0x01, 0x44, 0x00, analog ? 0x01 : 0x00, lock ? 0x03 : 0x00, 0x00, 0x00, 0x00, 0x00 };

  DO_R(PSX_sendMessage( Mode, sizeof(Mode) ), 9)
  return error;
}

RESULT PSX_requireConfigurationMode(boolean enabled)
{
  if (isConfigurationMode != enabled)
    return PSX_sendConfigurationModeMessage43(enabled);
}

RESULT PSX_enableVibration()
{
  DO(PSX_requireConfigurationMode(true))
  DO(PSX_sendActuatorMappingMessage4D(true))
  return STATUS_SUCCESS;
}

RESULT PSX_disableVibration()
{
  /* ignore failures? */
  uint8_t data[64];
  PSX_exchangeControllerStatus42(data, sizeof(data), 0, 0);

  DO(PSX_requireConfigurationMode(true))
  DO(PSX_sendActuatorMappingMessage4D(false))
  return STATUS_SUCCESS;
}

RESULT PSX_changeControllerMode(boolean analog, boolean lock)
{
  DO(PSX_requireConfigurationMode(true))
  DO(PSX_sendControllerModeMessage44(analog, lock))
  return STATUS_SUCCESS;
}

RESULT PSX_changeAnalogButtonsMode(uint32_t buttonMask)
{
  DO(PSX_requireConfigurationMode(true))
  DO(PSX_sendResponseBytesMessage4F(buttonMask))
  return STATUS_SUCCESS;
}

RESULT PSX_getCurrentResponseBytes()
{
  DO(PSX_requireConfigurationMode(true))
  return PSX_sendResponseFormatMessage41();
}

RESULT PSX_initialize(boolean analog, boolean lock, boolean enableDualShock, int pressureMask)
{
  //uint8_t data[64];
  initializationDelayHack = true;
  //DO(PSX_exchangeControllerStatus42(data, sizeof(data), 0, 0));
  DO(PSX_requireConfigurationMode(true))
  initializationDelayHack = false;
  if (analog)
    DO(PSX_sendResponseBytesMessage4F(pressureMask))
  DO(PSX_sendControllerModeMessage44(analog, lock))
  DO(PSX_sendActuatorMappingMessage4D(enableDualShock))
  return STATUS_SUCCESS;
}

int HOST_handleButton(int button, int newValue, int oldValue)
{
  if (newValue != oldValue)
  {
    XInput.setButton(button, newValue);
    return 1;
  }
  return 0;
}

int HOST_handleTriggerDigital(int trigger, int newValue, int oldValue)
{
  if (newValue != oldValue)
  {
    XInput.setTrigger(trigger, newValue ? 255 : 0);
    return 1;
  }
  return 0;
}

int HOST_handleTriggerAnalog(int trigger, int newValue, int oldValue)
{
  if (newValue != oldValue)
  {
    XInput.setTrigger(trigger, newValue);
    return 1;
  }
  return 0;
}

int HOST_handleAxisAnalog(int joystick, int axis, int newValue, int oldValue, boolean invert = false)
{
  if ( newValue != oldValue )
  {
    if (axis == 0)
      XInput.setJoystickX(joystick, invert ? 255 - newValue : newValue);
    else
      XInput.setJoystickY(joystick, invert ? 255 - newValue : newValue);
    return 1;
  }
  return 0;
}

#if USE_DEADZONE
void UTIL_adjustDeadzoneDrop(uint8_t *axisX, uint8_t *axisY) {
  uint8_t valueX = *axisX;
  uint8_t valueY = *axisY;
  int distanceX = valueX - 128;
  int distanceY = valueY - 128;
  int distance = distanceX * distanceX + distanceY * distanceY;
  int threshold = DEADZONE * DEADZONE;
  if (distance < threshold) {
    valueX = 128;
    valueY = 128;
  }
  *axisX = valueX;
  *axisY = valueY;
}

void UTIL_adjustDeadzoneTrig(uint8_t *axisX, uint8_t *axisY) {
  uint8_t valueX = *axisX;
  uint8_t valueY = *axisY;

  int distanceX = valueX - 128;
  int distanceY = valueY - 128;
  if (distanceX != 0 || distanceY != 0) {
    int distanceSq = distanceX * distanceX + distanceY * distanceY;

    float distance = sqrt(distanceSq);
    float angle0 = atan2(distanceY / 128.0f, distanceX / 128.0f);

    distance = (max(0, distance - DEADZONE) * 128) / (128 - DEADZONE);

#if false
    distance = min(128, distance); // prevent the outputs from going outside the "circle"
#endif

    distanceX = (int)(cos(angle0) * distance);
    distanceY = (int)(sin(angle0) * distance);

    *axisX = min(max(distanceX + 128, 0), 255);
    *axisY = min(max(distanceY + 128, 0), 255);
  }
}

#if DEADZONE_TRIG
#define UTIL_adjustDeadzone UTIL_adjustDeadzoneTrig
#else
#define UTIL_adjustDeadzone UTIL_adjustDeadzoneDrop
#endif
#endif

#if DEBUG_PSX
void DEBUG_printPsx(struct PSX_t * data) {
  char text[200];
  sprintf(text,
          "MODE: %d ||DPAD: {%d,%d,%d,%d} || BTN: {%d,%d,%d,%d},{%d,%d},{%d,%d},{%d,%d},{%d,%d} || AXIS: {%02x,%02x},{%02x,%02x} || ABTN: {%02x,%02x,%02x,%02x},{%02x,%02x,%02x,%02x},{%02x,%02x},{%02x,%02x}\n",
          data->Mode,
          data->Buttons.Up, data->Buttons.Rt, data->Buttons.Dn, data->Buttons.Lt,
          data->Buttons.X, data->Buttons.C, data->Buttons.S, data->Buttons.T,
          data->Buttons.Sl, data->Buttons.St,
          data->Buttons.L1, data->Buttons.R1,
          data->Buttons.L2, data->Buttons.R2,
          data->Buttons.L3, data->Buttons.R3,
          data->Axes.LX, data->Axes.LY,
          data->Axes.RX, data->Axes.RY,
          data->AnalogButtons.Up, data->AnalogButtons.Rt, data->AnalogButtons.Dn, data->AnalogButtons.Lt,
          data->AnalogButtons.X, data->AnalogButtons.C, data->AnalogButtons.S, data->AnalogButtons.T,
          data->AnalogButtons.L1, data->AnalogButtons.R1,
          data->AnalogButtons.L2, data->AnalogButtons.R2
         );
  SERIAL.print(text);
}
#endif

void DEBUG_displayErrorBlink()
{
#if DEBUG_SERIAL
  switch (_error_source) {
    case 1:
      SERIAL.println("Error 1");
      break;
    case 2:
      SERIAL.println("Error reading first byte of message header");
      break;
    case 3:
      SERIAL.println("Error reading second byte of message header");
      break;
    case 4:
      SERIAL.println("Error reading third byte of message header");
      break;
    case 5:
      SERIAL.println("Error reading message data");
      break;
    case 6:
      SERIAL.println("Error sending mode change");
      break;
    case 7:
      SERIAL.println("Error sending actuator mappings");
      break;
    case 8:
      SERIAL.println("Error exchanging controller status");
      break;
    case 9:
      SERIAL.println("Error setting controller mode");
      break;
  }
#else

  TXLED0;
  delay(100);
  //for (int i = 0; i < 10; i++)
  {
    TXLED1;
    delay(200);
    TXLED0;
    delay(200);
    TXLED1;
    delay(200);
    TXLED0;
    delay(200);
    TXLED1;
    delay(200);
    TXLED0;
    delay(200);
    TXLED1;
    delay(200);
    TXLED0;
    delay(1000);
    for (int j = 0; j < _error_source; j++)
    {
      TXLED1;
      delay(500);
      TXLED0;
      delay(500);
    }
    delay(1000);
  }
#endif
}

boolean disableDualshock2 = false;
uint32_t wantedMask = ANALOG_BUTTONS;
uint32_t bytesMask = FORCED_BITS;
void setup() {
  // Setup pins
  pinMode(PIN_ACK,  INPUT_PULLUP);
  pinMode(PIN_CS,  OUTPUT);
  pinMode(PIN_MISO, INPUT_PULLUP);
  pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_CLK,  OUTPUT);

  digitalWrite(PIN_CS, 1);
  digitalWrite(PIN_CLK, 1);
  digitalWrite(PIN_MOSI, 0);

  delay(100);

  // Custom hardware button, maps to the xbox logo action
  pinMode(PIN_EXTRA, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_ACK), PSX_ackReceived, FALLING);

  // Turn off transfer leds
  digitalWrite(17, 0);
  TXLED0;

  //XInput.setAutoSend(false);  // disable automatic output
  XInput.setRange(JOY_LEFT, 0, 255);
  XInput.setRange(JOY_RIGHT, 0, 255);
  XInput.setTriggerRange(0, 255);
  XInput.begin();


#if DEBUG_SERIAL
  // set the data rate for the SoftwareSerial port
  SERIAL.begin(38400);
  SERIAL.println("Hello, world?");
#endif

  bytesMask = ANALOG_BUTTONS;
  if (PSX_initialize(true, false, ENABLE_RUMBLE, bytesMask) < 0)
  {
    DEBUG_displayErrorBlink();
  }

  delay(50);

  cyclesWithoutActivity = 0;
}

int startupWait = 0;
int lastMode = 0;
void loop()
{
  uint8_t data0[64] = { 0 };
  uint8_t data[64] = { 0 };
  uint8_t bigRumble = 0;
  uint8_t smallRumble = 0;

#if ENABLE_RUMBLE
  if (startupWait <= 10) startupWait++;
  if (startupWait == 10)
  {
    smallMotorPulse = 1;
    bigMotorPulse = 1;
  }

  bigRumble = XInput.getRumbleLeft();
  smallRumble = XInput.getRumbleRight();

#if DEBUG_RUMBLE
  bigRumble = max(bigRumble, 2 * (127 - psx.Axes.LY));
  smallRumble = max(smallRumble, psx.Axes.RY < 30 ? 255 : 0);
#endif

#define PULSE_SMALL_TIME 16
  if (smallMotorPulse > 0) {
    if (smallMotorPulse >= PULSE_SMALL_TIME) {
      smallMotorPulse = 0;
    } else {
      smallRumble = 255;
      smallMotorPulse++;
    }
  }

#define PULSE_BIG_TIME 16
  static uint8_t pulse_values[16] = {1 * 255 / 7, 2 * 255 / 7, 3 * 255 / 7, 4 * 255 / 7, 5 * 255 / 7, 6 * 255 / 7, 7 * 255 / 7, 6 * 255 / 7, 5 * 255 / 7, 4 * 255 / 7, 3 * 255 / 7, 2 * 255 / 7, 1 * 255 / 7, 0};
  if (bigMotorPulse > 0) {
    if (bigMotorPulse >= PULSE_BIG_TIME) {
      bigMotorPulse = 0;
    } else {
      bigRumble = max(bigRumble, pulse_values[bigMotorPulse * 16 / PULSE_BIG_TIME]);
      bigMotorPulse++;
    }
  }

#endif

  if (PSX_requireConfigurationMode(false) < 0)
  {
    DEBUG_displayErrorBlink();
  }

int dataL = PSX_exchangeControllerStatus42(data0, sizeof(data0), smallRumble > 127 ? 255 : 0, bigRumble);
  if (dataL < 0)
  {
    DEBUG_displayErrorBlink();
    for (int i = 0; i < 21; i++)
    {
      data[i] = 0;
    }
  }
  else
  {
    int i,j;
    for (i = 0, j = 0; i < 21; i++)
    {
      if (i < 3 || ((bytesMask >> (i - 3)) & 1) != 0)
        data[i] = data0[j++];
      else
        data[i] = 0;
    }
    dataL = j;
  }

#if DEBUG_SERIAL
  DBGPRINT("Data1 (%lx): ", bytesMask);
  for (int i = 0; i < 21; i++) {
    static char dbg[10];
    sprintf(dbg, "%02x ", data[i]);
    SERIAL.print(dbg);
  }
  SERIAL.println();
  delay(5);
#endif

  int mode = psx.Mode = (data[1]) >> 4;

  if (mode != oldPsx.Mode)
  {
    if (mode == 7)
    {
      bytesMask = wantedMask | FORCED_BITS;
      if (PSX_changeAnalogButtonsMode(bytesMask) < 0)
      {
        DEBUG_displayErrorBlink();
      }
      #if 0
      int32_t mask = PSX_getCurrentResponseBytes(); // if setting the analog triggers didn't work, then continue as if this was a ps1 controller
      if (mask < 0)
      {
        DBGPRINT("Error setting byte mask!\n");
        disableDualshock2 = true;
        bytesMask = FORCED_BITS;
      }
      else if (mask != bytesMask)
      {
        bytesMask = mask | FORCED_BITS;
      }
      #endif
    }
  }

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

    psx.AnalogButtons.Rt = data[9];
    psx.AnalogButtons.Lt = data[10];
    psx.AnalogButtons.Up = data[11];
    psx.AnalogButtons.Dn = data[12];
    psx.AnalogButtons.T = data[13];
    psx.AnalogButtons.C = data[14];
    psx.AnalogButtons.X = data[15];
    psx.AnalogButtons.S = data[16];
    psx.AnalogButtons.L1 = data[17];
    psx.AnalogButtons.R1 = data[18];
    psx.AnalogButtons.L2 = data[19];
    psx.AnalogButtons.R2 = data[20];
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

#if USE_DEADZONE
  UTIL_adjustDeadzone(&(psx.Axes.LX), &(psx.Axes.LY));
  UTIL_adjustDeadzone(&(psx.Axes.RX), &(psx.Axes.RY));
#endif

  int changes = 0;

  // Dpad buttons
  changes += HOST_handleButton(DPAD_LEFT,  psx.Buttons.Lt, oldPsx.Buttons.Lt);
  changes += HOST_handleButton(DPAD_RIGHT, psx.Buttons.Rt, oldPsx.Buttons.Rt);
  changes += HOST_handleButton(DPAD_UP,    psx.Buttons.Up, oldPsx.Buttons.Up);
  changes += HOST_handleButton(DPAD_DOWN,  psx.Buttons.Dn, oldPsx.Buttons.Dn);

  // Face buttons
  changes += HOST_handleButton(BUTTON_A, psx.Buttons.X, oldPsx.Buttons.X);
  changes += HOST_handleButton(BUTTON_B, psx.Buttons.C, oldPsx.Buttons.C);
  changes += HOST_handleButton(BUTTON_X, psx.Buttons.S, oldPsx.Buttons.S);
  changes += HOST_handleButton(BUTTON_Y, psx.Buttons.T, oldPsx.Buttons.T);

  // Shoulder buttons
  changes += HOST_handleButton(BUTTON_LB, psx.Buttons.L1, oldPsx.Buttons.L1);
  changes += HOST_handleButton(BUTTON_RB, psx.Buttons.R1, oldPsx.Buttons.R1);
  if (bytesMask & RESPONSE_BIT_ANALOG_L2)
    changes += HOST_handleTriggerAnalog(TRIGGER_LEFT,  psx.AnalogButtons.L2, oldPsx.AnalogButtons.L2);  // TODO: Analog triggers in DS2 mode
  else
    changes += HOST_handleTriggerDigital(TRIGGER_LEFT,  psx.Buttons.L2, oldPsx.Buttons.L2);  // TODO: Analog triggers in DS2 mode
  if (bytesMask & RESPONSE_BIT_ANALOG_R2)
    changes += HOST_handleTriggerAnalog(TRIGGER_RIGHT, psx.AnalogButtons.R2, oldPsx.AnalogButtons.R2);
  else
    changes += HOST_handleTriggerDigital(TRIGGER_RIGHT, psx.Buttons.R2, oldPsx.Buttons.R2);

  // Center buttons and stick buttons
  changes += HOST_handleButton(BUTTON_L3, psx.Buttons.L3, oldPsx.Buttons.L3);
  changes += HOST_handleButton(BUTTON_R3, psx.Buttons.R3, oldPsx.Buttons.R3);
  changes += HOST_handleButton(BUTTON_BACK, psx.Buttons.Sl, oldPsx.Buttons.Sl);
  changes += HOST_handleButton(BUTTON_START, psx.Buttons.St, oldPsx.Buttons.St);

  // Axes
  changes += HOST_handleAxisAnalog(JOY_LEFT, 0, psx.Axes.LX, oldPsx.Axes.LX);
  changes += HOST_handleAxisAnalog(JOY_LEFT, 1, psx.Axes.LY, oldPsx.Axes.LY, true);
  changes += HOST_handleAxisAnalog(JOY_RIGHT, 0, psx.Axes.RX, oldPsx.Axes.RX);
  changes += HOST_handleAxisAnalog(JOY_RIGHT, 1, psx.Axes.RY, oldPsx.Axes.RY, true);

  // Special
  changes += HOST_handleButton(BUTTON_LOGO, extraButton, oldExtraButton);

  oldPsx = psx;
  oldExtraButton = extraButton;
  if (changes > 0)
  {
    XInput.send();
    cyclesWithoutActivity = 0;
  }
  else if (cyclesWithoutActivity <= SLOWMODE_DELAY)
  {
    cyclesWithoutActivity++;
  }

#if DEBUG_PSX
  if (bigRumble != oldBigRumble || smallRumble != oldSmallRumble)
  {
    DBGPRINT("RUMBLE! %d %d\n", smallRumble, bigRumble);
  }
  oldBigRumble = bigRumble;
  oldSmallRumble = smallRumble;

  if (changes > 0)
    DEBUG_printPsx(&psx);
#endif

  if (cyclesWithoutActivity >= SLOWMODE_DELAY)
    delay(100/*ms*/);
  else
    delay(1/*ms*/);
}
