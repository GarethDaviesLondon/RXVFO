# RXVFO

Gareth Davies, G0CIT
Feb 2020 

VFO code to drive an AD9850 module, has input with rotary encoder with push
switch and output frequency display on a small OLED.

Features include:

Selectable tuning steps (short press on shaft changes mode) from 1hz to 10Mhz steps.
IF offset (tested at 455 Khz) and modified by a #define at compile time
EEPROM stores the last frequency and tuning step selected


The code used to drive the AD9850
https://create.arduino.cc/projecthub/mircemk/arduino-dds-vfo-with-ad9850-module-be3d5e
Credit to Mirko Pavleski

The code for the OLED comes from the adafruit libraries.

**************************************************************************/
