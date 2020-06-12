/**************************************************************************

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

#include <SPI.h>              //This is needed for the OLED display which is SPI interface
#include <Wire.h>             //Needed by the SPI library
#include <Adafruit_GFX.h>     //Used for the OLED display, called by the SSD1306 Library
#include <Adafruit_SSD1306.h> //This is the OLED driver library
#include <Rotary.h>           //Used for the rotary encoder
#include <EEPROM.h>           //Library needed to read and write from the EEPROM

//////////////////////////////////////////////////////////////////////////////////////////////////////

//#define DEBUG 1               //Uncomment this to enable debugging features
#define CLI                   //Uncomment this to enable a command line interface, usefull for development

#ifdef CLI
  #include "CommandLine.h"    //This is the command line interface code, shamelessly borrowed.
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////

//THis is for the second line of text. Use either Display IF Frequency, or a banner. Your choice
//Might as well put the banner here, because you'll change it for your callsign!
//Just raise a pint in my direction when you tell everyone that you wrote this :-)

#define DISPLAYIFFREQUENCY //This displays the IFFREQ. 
//#define USEBANNER //bit of fun with a banner message, comment out if you want to turn it off
//#define BANNERMESSAGE "AD9850 VFO V2.1 G0CIT"
#define BANNERX 0
#define BANNERY 25

//////////////////////////////////////////////////////////////////////////////////////////////////////

//Defaults for the code writing to the EEPROM
#define SIGNATURE 0xAABB //Used to check if the EEPROM has been initialised
#define SIGLOCATION 0    //Location where SIGNATURE IS STORED
#define FREQLOCATION 4   //Location where Current Frequency is stored
#define STEPLOCATION 8   //Location where Current Step size is stored

#define DEFAULTFREQ 32768 //Set default frequency to 7Mhz. Only used when EEPROM not initialised
#define DEFAULTSTEP 1    //Set default tuning step size to 1Khz. Only used when EEPROM not initialised
#define UPDATEDELAY 5000    //When tuning you don't want to be constantly writing to the EEPROM. So wait
                            //For this period of stability before storing frequency and step size.
                            
//////////////////////////////////////////////////////////////////////////////////////////////////////

//These are used for the OLED Screen
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)

int underBarX;  //This is the global X value that set the location of the underbar
int underBarY;  //This is the global Y value that set the location of the underbar

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); //global handle to the display

//////////////////////////////////////////////////////////////////////////////////////////////////////

//ROTARY ENCODER parameters
#define ROTARYLEFT 2  //Pin the left turn on the encoder is connected to Arduino
#define ROTARYRIGHT 3 //Pin the right turn on encoder is conneted to on Arduino
#define PUSHSWITCH 4  //Pin that the the push switch action is attached to
  
#define LONGPRESS 500 //Milliseconds required for a push to become a "long press"
#define SHORTPRESS 0  //Milliseconds required for a push to become a "short press"
#define DEBOUNCETIME 100  //Milliseconds of delay to ensure that the push-switch has debounced
#define BACKTOTUNETIME 5000 //Milliseconds of idle time before exiting a button push state

Rotary r = Rotary(ROTARYLEFT, ROTARYRIGHT); //This sets up the Rotary Encoder including pin modes.

//////////////////////////////////////////////////////////////////////////////////////////////////////

//Setup PINS for use with AD9850
#define W_CLK 8   // Pin 8 - connect to AD9850 module word load clock pin (CLK)
#define FQ_UD 9   // Pin 9 - connect to freq update pin (FQ)
#define DATA 10   // Pin 10 - connect to serial data load pin (DATA)
#define RESET 11  // Pin 11 - connect to reset pin (RST) 
#define pulseHigh(pin) {digitalWrite(pin, HIGH); digitalWrite(pin, LOW); } //routine for putting a clock onto the clock line

//The following values need to be positive as they are used as "unsigned long int" types in the EEPROM routines

#define IFFREQ 0 //IF Frequency - offset between displayed and produced signal
#define IFFERROR 0 //Observed error in BFO
#define MAXFREQ 50000  //Sets the upper edge of the frequency range (30Mhz)
#define MINFREQ 10000    //Sets the lower edge of the frequency range (100Khz)

long tuneStep;        //global for the current increment - enables it to be changed in interrupt routines
long ifFreq = IFFREQ+IFFERROR; //global for the receiver IF. Made variable so it could be manipulated by the CLI for instance
double rx;            //global for the current receiver frequency


//////////////////////////////////////////////////////////////////////////////////////////////////////

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


  readDefaults();         //check EEPROM for startup conditions
  setTuneStepIndicator(); //set up the X&Y for the step underbar 
  displayFrequency(rx);   //display the frequency on the OLED
  sendFrequency(rx+ifFreq); //send the command to the 9850 Module, adjusted up by the IF Frequency
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

/*

This is the main loop which polls the rotary switch and the press button.
I found that when the SPI code is used it can be awkward to use Interrupt Services which might be 
better. With the short cycle of this loop code though there are no issues with missed events

*/ 


unsigned long int lastMod; //This records the time the last modification was made. 
                           //It is used to know when to confirm the EEPROM update
bool freqChanged = false;  //This is used to know if there has been an update, if so
                           //It is a candidate for writing to EEPROM if it was last done long enough ago

void loop() {

#ifdef CLI
  if (getCommandLineFromSerialPort(CommandLine) )  //Put a command line interface serivce routine in.
          {
            DoCommand(CommandLine);
          }
#endif


///CHECK IF WE NEED TO CHANGE FREQUENCY////////
  int result = r.process();       //This checks to see if there has been an event on the rotary encoder.
  if (result)
  {
    
    freqChanged=true; //used to check the EEPROM writing            
    lastMod=millis(); //used to check the EEPROM writing
    
    //Increment or decrement the frequency by the tuning step depending on direction of movement.
    if (result == DIR_CW) {
        rx+=tuneStep;
        if (rx>MAXFREQ) {rx = MAXFREQ;}
        displayFrequency(rx);
        sendFrequency(rx);
      } else {
        rx-=tuneStep;
        if (rx<MINFREQ) {rx = MINFREQ;}
        displayFrequency(rx);
        sendFrequency(rx);      
      }
  }
//////////

//See if we've pressed the button
  if (digitalRead(PUSHSWITCH)==LOW){
    doMainButtonPress();  //process the switch push
  }

/// See if we need to update the EEPROM

  if ((freqChanged) & (millis()-lastMod>UPDATEDELAY) )
  {
    commitEPROMVals();
    freqChanged=false;
  }
  
}


//////////////////////////////////////////////////////////////////////////////////////////////////////

void doMainButtonPress(){
  
    long int pressTime = millis();  //reord when we enter the routine, used to determine the length of the button
                                    //press
    
    waitStopBounce(PUSHSWITCH);               //wait until the switch noise has gone  
    


    while (digitalRead(PUSHSWITCH)==LOW) //Sit in this routine while the button is pressed
    {
      delay(1); //No operation but makes sure the compiler doesn't optimise this code away
    }
    
    pressTime=millis()-pressTime; //This records the duration of the button press

#ifdef DEBUG
    Serial.print("Button Press Duration (ms) : ");
    Serial.println(pressTime);
#endif
    
    if (pressTime > LONGPRESS) //Check against the defined length of a long press
    {
       //Do long press operations

#ifdef DEBUG
      Serial.println("Long Press Detected");
      for (int a=0;a<5;a++)
      {
        digitalWrite(LED_BUILTIN,HIGH);
        delay(500);
        digitalWrite(LED_BUILTIN,LOW);
        delay(500);
      }
#endif
    }
    
    else
    {
      //Do short press operations
      changeFeqStep();

      
#ifdef DEBUG
      Serial.println("Short Press Detected");
      for (int a=0;a<5;a++)
      {
        digitalWrite(LED_BUILTIN,HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN,LOW);
        delay(100);
      }
#endif
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

//Removes bounce on the input switch

//Simple delay version
void waitStopBounce(int pin)
{
  long int startTime=millis();
  while (millis()-startTime < DEBOUNCETIME)
  {
    delay(1);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////


void changeFeqStep()
{
  
  unsigned long int pauseTime=millis(); //record when we start this operation
  
  while(digitalRead(PUSHSWITCH)==HIGH) //This stays in this routine until the button is pressed again to exit.
  {
    int result = r.process();
    if (result)
    {
      pauseTime=millis();               //update the timer to show that we've taken action
      
      if (result == DIR_CW) {
          if (tuneStep>1)  { 
            if (tuneStep==1000) {tuneStep=500;}
            else{
              if (tuneStep==500) {tuneStep=100;}
              else
                {
                  tuneStep=tuneStep/10;     
                }
            }
          }
      } else {
          if (tuneStep<10000000)  {
            if (tuneStep==100) {tuneStep=500;}
            else{
              if (tuneStep==500) {tuneStep=1000;}
              else
                {
                  tuneStep=tuneStep*10;     
                }
            }
          }
      }
      setTuneStepIndicator();
      displayFrequency(rx);
      
    }
    

    //If no input for moving the dial step then just go back to normal
    //There is a possible - but unlikely - scenario that the button is pressed as this timeout occurs, that would result in
    //bouncy switch condition, hence the debounce requirement
    if (millis()-pauseTime > BACKTOTUNETIME) {
                 waitStopBounce(PUSHSWITCH);
                 return;
    } 
  }

  
  //make sure that the swith has stopped bouncing before returning to the main routine.
  //There is a bug possible here if we don't wait for the release before returning
  //Possible that a long press on the way out of this routine could see you return here
  //due to switch bounce, which would appear to the user that the routine didn't exit
  //also possible to go accidently into a long-press scenario
  
  waitStopBounce(PUSHSWITCH);
 
  while (digitalRead(PUSHSWITCH)==LOW) //to avoid exit bug when the user keeps the button pressed for a long period
  {
    delay(1);                 
  }
  
  waitStopBounce(PUSHSWITCH);                            
}


//////////////////////////////////////////////////////////////////////////////////////////////////////


// frequency calc from datasheet page 8 = <sys clock> * <frequency tuning word>/2^32


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

//////////////////////////////////////////////////////////////////////////////////////////////////////


void displayFrequency(double hzd)
{

//Decompose into the component parts of the frequency.
    long int hz = long(hzd/1);
    long int millions = int(hz/1000000);
    long int hundredthousands = ((hz/100000)%10);
    long int tenthousands = ((hz/10000)%10);
    long int thousands = ((hz/1000)%10);
    long int hundreds = ((hz/100)%10);
    long int tens = ((hz/10)%10);
    long int ones = ((hz/1)%10);

#ifdef DEBUG
//This checks the calculation for frequency worked.
    Serial.print(millions);
    Serial.print(".");
    Serial.print(hundredthousands);
    Serial.print(tenthousands);
    Serial.print(thousands);
    Serial.print(".");
    Serial.print(hundreds);
    Serial.print(tens);
    Serial.print(ones);
    Serial.println();
#endif
  
  
    display.clearDisplay();
    display.setTextSize(2); // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);
    if (millions<10)
    {
      display.print("0");
    }
    display.setTextSize(2); // Draw 2X-scale text
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
    display.setCursor(underBarX,underBarY);
    display.print("-");
    
#ifdef USEBANNER
    display.setCursor(BANNERX, BANNERY);
    display.print(BANNERMESSAGE);
#endif
#ifdef DISPLAYIFFREQUENCY
    display.setCursor(BANNERX, BANNERY);
    display.print(" IF = ");
    display.print(ifFreq/1000);
    display.print(".");
    display.print(ifFreq%1000);
#endif
    display.display();      // Show initial text
}

//////////////////////////////////////////////////////////////////////////////////////////////////////


void setTuneStepIndicator()
//This sets up the underbar X & Y locations based on the 
//value of tuneStep, which. Underlines the frequency display
//Values were found by trial and error using the CLI                 
{
    underBarY = 15;
    if (tuneStep==10000000) underBarX=13;
    if (tuneStep==1000000) underBarX=22;
    if (tuneStep==100000) underBarX=48;
    if (tuneStep==10000) underBarX=60;
    if (tuneStep==1000) underBarX=72;
    if (tuneStep<1000) underBarY=7;
    if (tuneStep==100) underBarX=88;
    if (tuneStep==500) underBarX=88;
    if (tuneStep==10) underBarX=95;
    if (tuneStep==1) underBarX=100;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////


void readDefaults()
{
  
  if (readEPROM(SIGLOCATION) != SIGNATURE)
    {
       //Means that there has not been any initialised sequence stored in EEPROM yet
       //Comes from a virgin processor, or a change in the SIGNATURE
        rx=DEFAULTFREQ;
        tuneStep=DEFAULTSTEP;
        setTuneStepIndicator();
        displayFrequency(rx);
    }
    else
    {
      readEPROMVals();
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

void readEPROMVals()
{
      rx=readEPROM(FREQLOCATION);
      tuneStep=readEPROM(STEPLOCATION);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

void commitEPROMVals()
{
      writeEPROM(SIGLOCATION,SIGNATURE);
      writeEPROM(FREQLOCATION,rx);
      writeEPROM(STEPLOCATION,tuneStep);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////


void writeEPROM(int addr, unsigned long int inp)
{
  byte LSB=inp;
  byte MSB=inp>>8;
  byte MMSB=inp>>16;
  byte MMMSB=inp>>24;
  EEPROM.update(addr,LSB);
  EEPROM.update(addr+1,MSB);
  EEPROM.update(addr+2,MMSB);
  EEPROM.update(addr+3,MMMSB);  
#ifdef DEBUG
  Serial.print("EEPROM LOC:");
  Serial.print(addr);
  Serial.print(" Write = ");
  Serial.println(inp);
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////


unsigned long int readEPROM(int addr)
{
  byte LSB=EEPROM.read(addr);
  byte MSB=EEPROM.read(addr+1);
  byte MMSB=EEPROM.read(addr+2);
  byte MMMSB=EEPROM.read(addr+3);
  unsigned long int OP=MMMSB;
  OP = (OP<<8);
  OP = OP|MMSB;
  OP = (OP<<8);
  OP = OP|MSB;
  OP = (OP<<8);
  OP = OP|LSB;
#ifdef DEBUG
  Serial.print("EEPROM LOC:");
  Serial.print(addr);
  Serial.print(" Read = ");
  Serial.println(OP);
#endif
  return OP;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef CLI

/** Command Line Interface Routines
 *  
 *  I can't remember where I cribbed this from, but thanks whoever donated this part.
 *  
 */

 
void printHelp()
{
   Serial.println("");
   Serial.println("Commands");
   Serial.println("set [long integer] (set operating frequency)");
   Serial.println("step [long integer] (set tuning step)");
   Serial.println("setif [integer] (Change the IF Frequency)");
   Serial.println("y (test y position of underbar)");
   Serial.println("x (test x position of underbar)");
   Serial.println("Help | ?");
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


#endif


//////////////////////////////////////////////////////////////////////////////////////////////////////



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
