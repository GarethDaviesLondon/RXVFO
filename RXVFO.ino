/**************************************************************************


 Credits to: 
 
  Limor Fried/Ladyada for Adafruit Industries for OLED code

save

  
 

 **************************************************************************/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Rotary.h>

#include "CommandLine.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
    int underBarX;
    int underBarY;

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//Declare the pins for the rotary encoder and switch
#define ROTARYLEFT 2
#define ROTARYRIGHT 3
#define PUSHSWITCH 4
#define LONGPRESS 500
#define SHORTPRESS 0
#define DEBOUNCETIME 100


//Setup PINS for use with AD9850
#define W_CLK 8   // Pin 8 - connect to AD9850 module word load clock pin (CLK)
#define FQ_UD 9   // Pin 9 - connect to freq update pin (FQ)
#define DATA 10   // Pin 10 - connect to serial data load pin (DATA)
#define RESET 11  // Pin 11 - connect to reset pin (RST) 
#define pulseHigh(pin) {digitalWrite(pin, HIGH); digitalWrite(pin, LOW); }
#define IFFREQ 455000
#define STARTFREQ 14000000

long tuneStep;
long ifFreq = IFFREQ;
double rx;

//Set up rotary encoder
Rotary r = Rotary(ROTARYLEFT, ROTARYRIGHT); //This sets up the Rotary Encoder including pin modes.


void setup() {
  Serial.begin(9600);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  
  //Set up for AD9850
  pinMode(FQ_UD, OUTPUT);
  pinMode(W_CLK, OUTPUT);
  pinMode(DATA, OUTPUT);
  pinMode(RESET, OUTPUT); 
  pulseHigh(RESET);
  pulseHigh(W_CLK);
  pulseHigh(FQ_UD);  // this pulse enables serial mode on the AD9850 - Datasheet page 12.



  //Set up for Rotary Encoder
  r.begin();
  pinMode(PUSHSWITCH,INPUT_PULLUP);

  ///
  rx=STARTFREQ;
  tuneStep=1000;
  setTuneStepIndicator();
  displayFrequency(rx);
  sendFrequency(rx+ifFreq);
  
}

void loop() {
  if (getCommandLineFromSerialPort(CommandLine) )
          {
            DoCommand(CommandLine);
          }
  int result = r.process();
  if (result)
  {
    if (result == DIR_CW) {
        rx+=tuneStep;
        displayFrequency(rx);
        sendFrequency(rx);
      } else {
        rx-=tuneStep;
        displayFrequency(rx);
        sendFrequency(rx);      
      }
  }
  if (digitalRead(PUSHSWITCH)==LOW){
    doMainButtonPress();
  }
  
}

void changeFeqStep()
{
  while(digitalRead(PUSHSWITCH)==HIGH)
  {
    int result = r.process();
    if (result)
    {
      if (result == DIR_CW) {
          if (tuneStep>1)  { tuneStep=tuneStep/10;}
      } else {
          if (tuneStep<10000000)  {tuneStep=tuneStep*10;}
      }
      setTuneStepIndicator();
      displayFrequency(rx);
    }
  }
  waitStopBounce();
}


void waitStopBounce()
{
  bool state = digitalRead(PUSHSWITCH);
  bool delayDone=false;
  long int startTime=millis();
  while (!delayDone)
  {
    if (digitalRead(PUSHSWITCH)==state)
    {
      if (millis()-startTime > DEBOUNCETIME)
      {
        delayDone=true;
        break;
      }
      state=!state;
    }
  }
}

void doMainButtonPress(){
  
    long int pressTime = millis();
    waitStopBounce();
    
    while (digitalRead(PUSHSWITCH)==LOW)
    {
      delay(1);
    }
    
    pressTime=millis()-pressTime;
    Serial.println(pressTime);
    
    if (pressTime > LONGPRESS)
    {

      //Do long press operation
      for (int a=0;a<5;a++)
      {
        digitalWrite(LED_BUILTIN,HIGH);
        delay(500);
        digitalWrite(LED_BUILTIN,LOW);
        delay(500);
      }

    }
    else
    {
      //Do short press operation
      changeFeqStep();
    }
}




// frequency calc from datasheet page 8 = <sys clock> * <frequency tuning word>/2^32
/*
 * The send Frequency code comes from 
 * https://create.arduino.cc/projecthub/mircemk/arduino-dds-vfo-with-ad9850-module-be3d5e
 * Credit to Mirko Pavleski
 */


void sendFrequency(double frequency) {
  frequency = frequency+ifFreq;  //IF Offset
  int32_t freq = frequency * 4294967295/125000000;  // note 125 MHz clock on 9850.  You can make 'slight' tuning variations here by adjusting the clock frequency.
  for (int b=0; b<4; b++, freq>>=8) {
    tfr_byte(freq & 0xFF);
  }
  tfr_byte(0x000);   // Final control byte, all 0 for 9850 chip
  pulseHigh(FQ_UD);  // Done!  Should see output
}
// transfers a byte, a bit at a time, LSB first to the 9850 via serial DATA line
void tfr_byte(byte data)
{
  for (int i=0; i<8; i++, data>>=1) {
    digitalWrite(DATA, data & 0x01);
    pulseHigh(W_CLK);   //after each bit sent, CLK is pulsed high
  }
}



void displayFrequency(double hzd)
{
    long hz = long(hzd/1);
    long millions = int(hz/1000000);
    long hundredthousands = ((hz/100000)%10);
    long tenthousands = ((hz/10000)%10);
    long thousands = ((hz/1000)%10);
    long hundreds = ((hz/100)%10);
    long tens = ((hz/10)%10);
    long ones = ((hz/1)%10);

  
  display.clearDisplay();
  display.setTextSize(2); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
 /*
    Serial.print(millions);
    Serial.print(".");
    Serial.print(hundredthousands);
    Serial.print(tenthousands);
    Serial.print(thousands);
    Serial.print(".");
    Serial.print(hundreds);
    Serial.print(tens);
    Serial.print(ones);
  */
    
    if (millions<10)
    {
      display.print("0");
    }
    display.print(millions);
    display.print(".");
    display.print(hundredthousands);
    display.print(tenthousands);
    display.print(thousands);
    display.setTextSize(1); // Draw 1X-scale text
    display.print(".");
    display.print(hundreds);
    display.print(tens);
    display.print(ones);
    display.setTextSize(1); // Draw 1X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(underBarX,underBarY);
    display.print("-");
    //display.setCursor(95, 25);
    //display.print("G0CIT");
    display.setCursor(0, 25);
    display.print("GARETH - G0CIT");

    display.display();      // Show initial text
}



/** Command Line Interface Routines
 *  
 *  Sorry I can't remember where I cribbed this from, but thanks whoever donated this part.
 *  
 */

 
void printHelp()
{
   Serial.println("");
   Serial.println("Commands");
   Serial.println("set [long integer] (set operating frequency)");
   Serial.println("step [long integer] (set tuning step)");
   Serial.println("setif [integer] (test y position of underbar)");
   Serial.println("y (test y position of underbar)");
   Serial.println("x (test x position of underbar)");
   Serial.println("Help | ?");
}

void setTuneStepIndicator()
{
    underBarY = 15;
    if (tuneStep==10000000) underBarX=13;
    if (tuneStep==1000000) underBarX=22;
    if (tuneStep==100000) underBarX=48;
    if (tuneStep==10000) underBarX=60;
    if (tuneStep==1000) underBarX=72;
    if (tuneStep<1000) underBarY=7;
    if (tuneStep==100) underBarX=88;
    if (tuneStep==10) underBarX=95;
    if (tuneStep==1) underBarX=100;
}


/*************************************************************************************************************
     your Command Names Here
*/
const char *helpCommandToken = "?";
const char *helpCommandToken2 = "help";
const char *setToken = "set";
const char *setToken2 = "s";
const char *stepToken = "step";
const char *stepToken2 = "st";
const char *Ytoken = "y";
const char *Xtoken = "x";
const char *IFSHIFT= "setif";

/****************************************************
   DoMyCommand
*/

bool DoCommand(char * commandLine) {

  bool commandExecuted=false;
  char * ptrToCommandName = strtok(commandLine, delimiters);

   //HELP COMMAND /////////
   if ((strcmp(ptrToCommandName, helpCommandToken) == 0) | strcmp(ptrToCommandName, helpCommandToken2)==0)  { 
     printHelp();
     commandExecuted=true;
   }
   
   if ((strcmp(ptrToCommandName, setToken) == 0) | strcmp(ptrToCommandName, setToken2)==0)  { 
      long value = readNumber();
      rx=value;
      displayFrequency(rx);
      sendFrequency(rx);
      commandExecuted=true;
   }
   
   if ((strcmp(ptrToCommandName, stepToken) == 0) | strcmp(ptrToCommandName, stepToken2)==0)  { 
     long value = readNumber();
     tuneStep=value;
     setTuneStepIndicator();
     displayFrequency(rx);
     commandExecuted=true;
   }

 if ((strcmp(ptrToCommandName, Xtoken) == 0) )  { 
     long value = readNumber();
     underBarX=(int)value;
     displayFrequency(rx);
     commandExecuted=true;
   }

   if ((strcmp(ptrToCommandName, Ytoken) == 0) )  { 
     long value = readNumber();
     underBarY=(int)value;
     displayFrequency(rx);
     commandExecuted=true;
   }

  if ((strcmp(ptrToCommandName, IFSHIFT) == 0) )  { 
     long value = readNumber();
     ifFreq=(int)value;
     displayFrequency(rx);
     displayFrequency(rx);
     commandExecuted=true;
   }

   
   if (!commandExecuted)
   {
      Serial.println("Error");
      Serial.println("\n");
      printHelp();
   }

}






/* This is old code from the adafruit library for the display. 
 *  Not currently used but here so I know where to look if I need it
 *  

void testdrawline() {
  int16_t i;

  display.clearDisplay(); // Clear display buffer

  for(i=0; i<display.width(); i+=4) {
    display.drawLine(0, 0, i, display.height()-1, SSD1306_WHITE);
    display.display(); // Update screen with each newly-drawn line
    delay(1);
  }
  for(i=0; i<display.height(); i+=4) {
    display.drawLine(0, 0, display.width()-1, i, SSD1306_WHITE);
    display.display();
    delay(1);
  }
  delay(250);

  display.clearDisplay();

  for(i=0; i<display.width(); i+=4) {
    display.drawLine(0, display.height()-1, i, 0, SSD1306_WHITE);
    display.display();
    delay(1);
  }
  for(i=display.height()-1; i>=0; i-=4) {
    display.drawLine(0, display.height()-1, display.width()-1, i, SSD1306_WHITE);
    display.display();
    delay(1);
  }
  delay(250);

  display.clearDisplay();

  for(i=display.width()-1; i>=0; i-=4) {
    display.drawLine(display.width()-1, display.height()-1, i, 0, SSD1306_WHITE);
    display.display();
    delay(1);
  }
  for(i=display.height()-1; i>=0; i-=4) {
    display.drawLine(display.width()-1, display.height()-1, 0, i, SSD1306_WHITE);
    display.display();
    delay(1);
  }
  delay(250);

  display.clearDisplay();

  for(i=0; i<display.height(); i+=4) {
    display.drawLine(display.width()-1, 0, 0, i, SSD1306_WHITE);
    display.display();
    delay(1);
  }
  for(i=0; i<display.width(); i+=4) {
    display.drawLine(display.width()-1, 0, i, display.height()-1, SSD1306_WHITE);
    display.display();
    delay(1);
  }

  delay(2000); // Pause for 2 seconds
}

void testdrawrect(void) {
  display.clearDisplay();

  for(int16_t i=0; i<display.height()/2; i+=2) {
    display.drawRect(i, i, display.width()-2*i, display.height()-2*i, SSD1306_WHITE);
    display.display(); // Update screen with each newly-drawn rectangle
    delay(1);
  }

  delay(2000);
}

void testfillrect(void) {
  display.clearDisplay();

  for(int16_t i=0; i<display.height()/2; i+=3) {
    // The INVERSE color is used so rectangles alternate white/black
    display.fillRect(i, i, display.width()-i*2, display.height()-i*2, SSD1306_INVERSE);
    display.display(); // Update screen with each newly-drawn rectangle
    delay(1);
  }

  delay(2000);
}

void testdrawcircle(void) {
  display.clearDisplay();

  for(int16_t i=0; i<max(display.width(),display.height())/2; i+=2) {
    display.drawCircle(display.width()/2, display.height()/2, i, SSD1306_WHITE);
    display.display();
    delay(1);
  }

  delay(2000);
}

void testfillcircle(void) {
  display.clearDisplay();

  for(int16_t i=max(display.width(),display.height())/2; i>0; i-=3) {
    // The INVERSE color is used so circles alternate white/black
    display.fillCircle(display.width() / 2, display.height() / 2, i, SSD1306_INVERSE);
    display.display(); // Update screen with each newly-drawn circle
    delay(1);
  }

  delay(2000);
}

void testdrawroundrect(void) {
  display.clearDisplay();

  for(int16_t i=0; i<display.height()/2-2; i+=2) {
    display.drawRoundRect(i, i, display.width()-2*i, display.height()-2*i,
      display.height()/4, SSD1306_WHITE);
    display.display();
    delay(1);
  }

  delay(2000);
}

void testfillroundrect(void) {
  display.clearDisplay();

  for(int16_t i=0; i<display.height()/2-2; i+=2) {
    // The INVERSE color is used so round-rects alternate white/black
    display.fillRoundRect(i, i, display.width()-2*i, display.height()-2*i,
      display.height()/4, SSD1306_INVERSE);
    display.display();
    delay(1);
  }

  delay(2000);
}

void testdrawtriangle(void) {
  display.clearDisplay();

  for(int16_t i=0; i<max(display.width(),display.height())/2; i+=5) {
    display.drawTriangle(
      display.width()/2  , display.height()/2-i,
      display.width()/2-i, display.height()/2+i,
      display.width()/2+i, display.height()/2+i, SSD1306_WHITE);
    display.display();
    delay(1);
  }

  delay(2000);
}

void testfilltriangle(void) {
  display.clearDisplay();

  for(int16_t i=max(display.width(),display.height())/2; i>0; i-=5) {
    // The INVERSE color is used so triangles alternate white/black
    display.fillTriangle(
      display.width()/2  , display.height()/2-i,
      display.width()/2-i, display.height()/2+i,
      display.width()/2+i, display.height()/2+i, SSD1306_INVERSE);
    display.display();
    delay(1);
  }

  delay(2000);
}

void testdrawchar(void) {
  display.clearDisplay();

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  // Not all the characters will fit on the display. This is normal.
  // Library will draw what it can and the rest will be clipped.
  for(int16_t i=0; i<256; i++) {
    if(i == '\n') display.write(' ');
    else          display.write(i);
  }

  display.display();
  delay(2000);
}

void testdrawstyles(void) {
  display.clearDisplay();

  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,0);             // Start at top-left corner
  display.println(F("Hello, world!"));

  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Draw 'inverse' text
  display.println(3.141592);

  display.setTextSize(2);             // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.print(F("0x")); display.println(0xDEADBEEF, HEX);

  display.display();
  delay(2000);
}

void testscrolltext(void) {
  display.clearDisplay();

  display.setTextSize(2); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.println(F("scroll"));
  display.display();      // Show initial text
  delay(100);

  // Scroll in various directions, pausing in-between:
  display.startscrollright(0x00, 0x0F);
  delay(2000);
  display.stopscroll();
  delay(1000);
  display.startscrollleft(0x00, 0x0F);
  delay(2000);
  display.stopscroll();
  delay(1000);
  display.startscrolldiagright(0x00, 0x07);
  delay(2000);
  display.startscrolldiagleft(0x00, 0x07);
  delay(2000);
  display.stopscroll();
  delay(1000);
}

void testdrawbitmap(void) {
  display.clearDisplay();

  display.drawBitmap(
    (display.width()  - LOGO_WIDTH ) / 2,
    (display.height() - LOGO_HEIGHT) / 2,
    logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);
  display.display();
  delay(1000);
}

#define XPOS   0 // Indexes into the 'icons' array in function below
#define YPOS   1
#define DELTAY 2

void testanimate(const uint8_t *bitmap, uint8_t w, uint8_t h) {
  int8_t f, icons[NUMFLAKES][3];

  // Initialize 'snowflake' positions
  for(f=0; f< NUMFLAKES; f++) {
    icons[f][XPOS]   = random(1 - LOGO_WIDTH, display.width());
    icons[f][YPOS]   = -LOGO_HEIGHT;
    icons[f][DELTAY] = random(1, 6);
    Serial.print(F("x: "));
    Serial.print(icons[f][XPOS], DEC);
    Serial.print(F(" y: "));
    Serial.print(icons[f][YPOS], DEC);
    Serial.print(F(" dy: "));
    Serial.println(icons[f][DELTAY], DEC);
  }

  for(;;) { // Loop forever...
    display.clearDisplay(); // Clear the display buffer

    // Draw each snowflake:
    for(f=0; f< NUMFLAKES; f++) {
      display.drawBitmap(icons[f][XPOS], icons[f][YPOS], bitmap, w, h, SSD1306_WHITE);
    }

    display.display(); // Show the display buffer on the screen
    delay(200);        // Pause for 1/10 second

    // Then update coordinates of each flake...
    for(f=0; f< NUMFLAKES; f++) {
      icons[f][YPOS] += icons[f][DELTAY];
      // If snowflake is off the bottom of the screen...
      if (icons[f][YPOS] >= display.height()) {
        // Reinitialize to a random position, just off the top
        icons[f][XPOS]   = random(1 - LOGO_WIDTH, display.width());
        icons[f][YPOS]   = -LOGO_HEIGHT;
        icons[f][DELTAY] = random(1, 6);
      }
    }
  }
}
*/
