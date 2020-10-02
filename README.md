# ArduinoGamepadAdapters
A small collection of Arduino programs that implement adapters for a couple of old console gamepads I happen to still have

## Folders

* SNESGamepad: An implementation of the SNES/SFC controller protocol, as a basic HID gamepad
* SNESGamepad-XInput: Same as SNESGamepad, but using the XInput library to simulate an Xbox360/XboxOne controller.
* PSXGamepad: A basic implementation of the PS1/PS2 dualshock gamepad protocol, as a basic HID gamepad. Does not support advanced features such as DualShock 2 pressure buttons.
* PSXGamepad-XInput: A slightly more advanced version of PSXGamepad which presents itself as an XInput controller (Xbox360/XboxOne). Supports rumble.

## TODO
* Support analog triggers when using a PS2 controller (DualShock 2) or compatible.