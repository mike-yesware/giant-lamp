#include <Arduino.h>

/*
    Giant Lamp, heavily based on:
      Chandelier2016 by Daniel Wilson
*/

#define USE_OCTOWS2811
#define ETHERNET_BUFFER 540
#include <Artnet.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>
#include <OctoWS2811.h>
#include <FastLED.h>
#include <Audio.h>
#include <Wire.h>
#include <Bounce2.h>
#include "palettes.h"

typedef void(*FunctionPointer)();    // function pointer type

// global definitions
#define numStrip 3          // strips are contiguous physical LED chains
#define numStrand 6         // strands are the vertical "strands" that make up the chandelier
#define numLedStrip 120     // Strip length
#define numLedStrand 60     // Strand length (half strand)
#define numLed 360          // Total LEDs
#define led 13              // Pin
#define serial Serial       // if USB define as Serial, if Bluetooth define as Serial1 (hardware)
uint8_t numStrandStrip = numStrand / numStrip; // TODO change to #define as 2?

// General defs
void checkAndUpdate();
void buttonSetup();
void incrementCurrentKnob();
void decrementCurrentKnob();
void changeProgram();
void runCommand(char);
void printKnob(float, float);
void helpMenu();
void black();
CHSV twinkle_color( int );
void sparkle();
float maxarr(float arr[]);
void computeVerticalLevels();
void transform(CRGB ledMatrix[numLedStrand][numStrand]);
void spin();
void printFFT();
void fadeleds();
void fadeTempLeds();
void fireColumn(uint8_t);

// Program defs
void twinkle();
void spectrum();
void pendulum();
void fireworks();
void glitter();
void rainbowColumns();
void whitePurpleColumns();
void columnsAndRows();
void america();
void fire();
void artnetDisplay();

uint8_t brightness = 128;
uint8_t maxBrightness = 255;
uint8_t increment = 4;
uint8_t maxKnob = 255;
uint8_t* currentKnob = &brightness;  // this is a knob pointer that points to the value I want to increment

#define ROTATION_TIME 300000         // ms for auto rotation
bool rotationMode = 0;               // flag that auto rotation is enabled
elapsedMillis rotationTimerMillis;   // time since last rotation

bool programChanged;                 // flag that a program change has occured
#define PROGRAM_COUNT 11             // number of programs in total
FunctionPointer currentProgram;      // this is a function pointer that points to the animation I want to run
int currentProgramIndex;             // Index of the current running program in the programs array
FunctionPointer programs[PROGRAM_COUNT] = {
  twinkle,
  spectrum,
  pendulum,
  fireworks,
  glitter,
  rainbowColumns,
  whitePurpleColumns,
  columnsAndRows,
  america,
  fire,
  artnetDisplay
}; // Function pointer array of all programs

CRGB currentColor = CRGB::Green;

char mode = 't'; //start on twinkle mode

//for TwinkleSparkle
#define COOLING            3         // controls how quickly LEDs dim
#define TWINKLING          75        // controls how many new LEDs twinkle
#define FLICKER            50        // controls how "flickery" each individual LED is
unsigned int seeds =       0;
unsigned int sparklesEntered =      0;
static int heat[numLed];

//for spectrum
int spins = 0;
unsigned int theta = 0;
unsigned int spinSpeed = 15;          //inversely proportional to spin speed
float maxLevel = 0.0 ;
unsigned int freqBin, x ;
float peakLevelSpectrum = 0.0;

//for dimming
int levelForget = 1000;
float levels[numStrand] = {0};
float relativeLevels[numStrand] = {0};
float peakLevel = 0.0;

//for pendulum
int timescale = 100; // approximate voidloop steps per second
int offset = 200;
int t = 1;
int hue = 0;
int tchaos = 120 * timescale; // seconds to flip animation
int bias = 1;
int sinelevel = 0;
float relativeLevel = 0.0;
//int timeConstants[numStrand] = {
//  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
//  17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2
//};   This is what it was before adjusting for new lengths
int timeConstants[numStrand] = {
  1, 2, 3, 4, 3, 2
};

// for America
#define SPARKLER_SPARKING           240
#define SPARKLER_CHILL_SPARKING     10
#define SPARKLER_COOLING            200

// for Arnet
Artnet artnet;
const int startUniverse = 0; // CHANGE FOR YOUR SETUP most software this is 1, some software send out artnet first universe as zero.
const int numberOfChannels = numLedStrip * numStrip * 3; // Total number of channels you want to receive (1 led = 3 channels)
byte channelBuffer[numberOfChannels]; // Combined universes into a single array

// Check if we got all universes
const int maxUniverses = numberOfChannels / 512 + ((numberOfChannels % 512) ? 1 : 0);
bool universesReceived[maxUniverses];
bool sendFrame = 1;
int ledPos = 0;

byte ip[] = {192, 168, 0, 10};
byte mac[] = {0x04, 0xE9, 0xE5, 0x04, 0x86, 0x45};

//Audio library objects
AudioInputAnalog         adc1(A14);       //xy=99,55
AudioAnalyzeFFT1024      fft;             //xy=265,75
AudioConnection          patchCord1(adc1, fft);
//int frequencyBinsHorizontal[numStrand + 1] = {
//  1,   1,  1,  1,  1,  1,  2,  2,
//  2,   3,  3,  4,  5,  5,  6,  7,
//  8,  10, 11, 13, 15, 17, 20, 23,
//  26, 30, 35, 40, 46, 53, 61, 70
//};
int frequencyBinsHorizontal[numStrand + 1] = {
  1, 1, 3, 7, 15, 31, 70
}; // Don't understand the +1 on numStrand, not accounted for in the original design

//define the leds as a matrix for doing animations, then as an array for final display
CRGB leds[numLedStrand][numStrand];      // Outer array of strands, inner array of LEDs per strand
CRGB tempLeds[numLedStrand][numStrand];  // Same as above
CRGB showLeds[numLed];                   // Array of total project LEDs
CRGB SparklerColor(int temperature);    // TODO unknown

// Button settings
// OctoWS2811 PIN access: 0(RX1), 1(TX1), 23(A), 22(A), 19(A), 18(A), 17(A)
#define DOWN 0
#define UP 1
#define BUTTON_A_PIN 0
#define BUTTON_B_PIN 1
#define BUTTON_A_LED_PIN 23
#define BUTTON_B_LED_PIN 22
#define BUTTON_DEBOUNCE_INTERVAL 10  // ms
#define BUTTON_LONG_PRESS_DELAY 1000  // ms
#define BUTTON_LONG_PRESS_INTERVAL 50  // ms

Bounce buttonA = Bounce();
Bounce buttonB = Bounce();

boolean buttonAState = UP;
boolean buttonALongPressState = UP;
boolean buttonBState = UP;
boolean buttonBLongPressState = UP;
boolean buttonABLongPressState = UP;

elapsedMillis buttonAPressedMillis;
unsigned long buttonAPressedTimeStamp;
elapsedMillis buttonALongPressedMillis;
unsigned long buttonALongPressedTimeStamp;

elapsedMillis buttonBPressedMillis;
unsigned long buttonBPressedTimeStamp;
elapsedMillis buttonBLongPressedMillis;
unsigned long buttonBLongPressedTimeStamp;

elapsedMillis buttonABLongPressedMillis;
unsigned long buttonABLongPressedTimeStamp;

void setup() {
  buttonSetup();
  
  currentProgramIndex = 0;   // start on twinkle  |  Note that these need to
  currentProgram = &twinkle; // start on twinkle  |  stay in sync!!

  AudioMemory(12);

  LEDS.addLeds<OCTOWS2811>(showLeds, numLedStrip).setCorrection( TypicalSMD5050 );  // TODO is this the right correction?
  LEDS.setBrightness(brightness);

  serial.begin(57600);

  pinMode(led, OUTPUT);

  for (int i = 0; i < numStrand; i++)  // Loop over each strand
  {
    timeConstants[i] += offset;
  }

  artnet.begin(mac, ip);
  artnet.setArtDmxCallback(artnetCallback);

  delay(2500); // 2.5 sec boot delay
}

void buttonSetup() {
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_A_LED_PIN, OUTPUT);
  pinMode(BUTTON_B_LED_PIN, OUTPUT);

  buttonA.attach(BUTTON_A_PIN);
  buttonB.attach(BUTTON_B_PIN);

  buttonA.interval(BUTTON_DEBOUNCE_INTERVAL);
  buttonB.interval(BUTTON_DEBOUNCE_INTERVAL);

  analogWrite(BUTTON_A_LED_PIN, 128);
  analogWrite(BUTTON_B_LED_PIN, 128);
}

void loop() {
  while ( serial.available() > 0 ) {
    runCommand( serial.read() );
  }

  checkAndUpdate();

  if ( programChanged ) {
    black();
    programChanged = false;
  }

  (*currentProgram)();
}

void checkAndUpdate() {
  bool buttonAChanged = buttonA.update();
  bool buttonBChanged = buttonB.update();

  if ( buttonAChanged ) {
    int buttonAValue = buttonA.read();
    serial.print("Button A changed to ");
    serial.println(buttonAValue);

    if ( buttonAValue == DOWN ) {
      buttonAPressedMillis = 0;
      buttonAState = DOWN;
      buttonAPressedTimeStamp = millis();

    } else if (buttonAValue == UP ) {
      buttonAState = UP;
      buttonALongPressState = UP;
      buttonABLongPressState = UP;

      serial.print("Button A held for ");
      serial.print(buttonAPressedMillis);
      serial.println("ms");

      if ( buttonAPressedMillis < BUTTON_LONG_PRESS_DELAY ) {
        serial.println("Button A short press triggered");

        changeProgram();
      }
    }
  }

  if ( buttonBChanged ) {
    int buttonBValue = buttonB.read();
    serial.print("Button B changed to ");
    serial.println(buttonBValue);

    if ( buttonBValue == DOWN ) {
      buttonBPressedMillis = 0;
      buttonBState = DOWN;
      buttonBPressedTimeStamp = millis();

    } else if (buttonBValue == UP ) {
      buttonBState = UP;
      buttonBLongPressState = UP;
      buttonABLongPressState = UP;

      serial.print("Button B held for ");
      serial.print(buttonBPressedMillis);
      serial.println("ms");

      if ( buttonBPressedMillis < BUTTON_LONG_PRESS_DELAY ) {
        serial.println("Button B short press triggered");

        changeProgramToArtnet();
      }
    }
  }

  if (buttonAState == DOWN && buttonALongPressState != DOWN && buttonBState != DOWN) {
    if ( millis() - buttonAPressedTimeStamp >= BUTTON_LONG_PRESS_DELAY ) {
      buttonALongPressState = DOWN;
      buttonALongPressedTimeStamp = millis();

      serial.println("Button A was long pressed");
    }
  }

  if (buttonBState == DOWN && buttonBLongPressState != DOWN && buttonAState != DOWN) {
    if ( millis() - buttonBPressedTimeStamp >= BUTTON_LONG_PRESS_DELAY ) {
      buttonBLongPressState = DOWN;
      buttonBLongPressedTimeStamp = millis();

      serial.println("Button B was long pressed");
    }
  }

  if (buttonAState == DOWN && buttonBState == DOWN && buttonABLongPressState) {
    if ( millis() - buttonBPressedTimeStamp >= BUTTON_LONG_PRESS_DELAY ) {
      buttonABLongPressState = DOWN;
      buttonABLongPressedTimeStamp = millis();

      rotationMode = !rotationMode;
      rotationTimerMillis = 0;
      serial.println("Both buttons were long pressed");
    }
  }

  if (buttonALongPressState == DOWN) {
    if ( millis() - buttonALongPressedTimeStamp >= BUTTON_LONG_PRESS_INTERVAL ) {
      buttonALongPressedTimeStamp = millis();

      decrementCurrentKnob();
      LEDS.setBrightness(brightness);
      serial.println("Button A long event triggered");
    }
  }

  if (buttonBLongPressState == DOWN) {
    if ( millis() - buttonBLongPressedTimeStamp >= BUTTON_LONG_PRESS_INTERVAL ) {
      buttonBLongPressedTimeStamp = millis();

      incrementCurrentKnob();
      LEDS.setBrightness(brightness);
      serial.println("Button B long event triggered");
    }
  }

  if ( rotationMode && rotationTimerMillis >= ROTATION_TIME ) {
    rotationTimerMillis = 0;

    changeProgram();
    // TODO PaletteChange
  }
}

void incrementCurrentKnob() {
  if (*currentKnob >= (maxBrightness - increment) && currentKnob == &brightness ) {
    *currentKnob = maxBrightness ;
  }
  else if (*currentKnob >= maxKnob ) {
    *currentKnob = maxKnob ;
  }
  else {
    *currentKnob += increment ;
  }

  if (currentKnob == &brightness ) {
    printKnob(*currentKnob, maxBrightness);
  }
  else {
    printKnob(*currentKnob, maxKnob);
  }
}

void decrementCurrentKnob() {
  if (*currentKnob <= increment) {
    *currentKnob = 2 ;
  }
  else {
    *currentKnob -= increment ;
  }

  if (currentKnob == &brightness ) {
    printKnob(*currentKnob, maxBrightness);
  }
  else {
    printKnob(*currentKnob, maxKnob);
  }
}

void changeProgram() {
  if ( currentProgramIndex >= PROGRAM_COUNT - 2) {  // Subtract one to account for 0 based arrays and another to skip artnet
    currentProgramIndex = 0;
  }
  else {
    currentProgramIndex++;
  }

  programChanged = true;
  currentProgram = programs[currentProgramIndex];
}

void changeProgramToArtnet() {
  programChanged = true;
  currentProgram = &artnetDisplay;
}

void runCommand(char command) {
  switch (command)
  {
    case '+':
    case '=':
      incrementCurrentKnob();
      break;

    case '-':
    case '_':
      decrementCurrentKnob();
      break;

    case 'b':
      currentKnob = &brightness; // change where the pointer points
      serial.println("brightness");
      break;

    case 'h':
      helpMenu();
      //serial.println("Winker");
      break;

    //programs
    case 'T':
      currentProgram = &twinkle;
      serial.println("Twinkle");
      break;

    case 'X':
      currentProgram = &spectrum;
      serial.println("Spectrum");
      break;

    case 'P':
      currentProgram = &pendulum;
      serial.println("Pendulum");
      break;

    case 'F':
      black();
      currentProgram = &fireworks;
      serial.println("Fireworks");
      break;

    case 'G':
      currentProgram = &glitter;
      serial.println("Glitter");
      break;

    case 'R':
      currentProgram = &rainbowColumns;
      serial.println("Rainbow");
      break;

    case 'B':
      currentProgram = &black;
      serial.println("Black");
      break;

    case 'Q':
      currentProgram = &whitePurpleColumns;
      serial.println("White Purple");
      break;

    case 'C':
      currentProgram = &columnsAndRows;
      serial.println("Columns and Rows");
      break;

    case 'A':
    currentProgram = &america;
    serial.println("America!");
    break;

    //flourishes
    case '\'':
      sparklesEntered = 1; //this is 1 because you has to enter "s" to get here
      while (Serial.available() > 0) {
        Serial.read();
        sparklesEntered++;
      }
      sparkle();
      serial.println("sparkle");
      break;
  }
}

void printKnob(float knobNow, float knobMax)
{
  serial.print("value is ");
  serial.print( int( 100.0 * knobNow / knobMax )  );
  serial.println("%");
}

void helpMenu()
{
  serial.println("------------------------");
  serial.println("WELCOME TO THE HELP MENU");
  serial.println("------------------------");

  serial.println("PROGRAMS");
  serial.println(" T twinkle (first dance)");
  serial.println(" X spinny rainbow with stars");
  serial.println(" P pendulum with blue and pink");
  serial.println(" F fireworks");
  serial.println(" G glitter sound reactive");
  serial.println(" R rainbow");
  serial.println(" W white");
  serial.println(" Q white and purple columns");


  serial.println("FLOURISHES");
  serial.println(" ' sparkle (first dance)");

  serial.println("SETTINGS");
  serial.println(" h help");
  serial.println(" b brightness");
  serial.println(" + increase");
  serial.println(" - decrease");
  serial.println("------------------------");
}

void black()  // Special blackout sketch for clearing the canvas
{
  for (int column = 0; column < numStrand; column++)
  {
    for (int row = 0; row < numLedStrand; row++)
    {
      leds[row][column] = CRGB::Black;
    }
  }
  fill_solid (showLeds, numLed, CRGB::Black);
  FastLED.show();  // FastLED.show used here to prevent checkAndUpdate loops
}

CHSV twinkle_color( int temperature)
{
  CHSV heatcolor;
  heatcolor.hue = 40;
  heatcolor.saturation = 175;
  heatcolor.value = temperature;
  return heatcolor;
}

void twinkle()
{
  // Step 1. Create a randome number of seeds
  random16_add_entropy( random()); //random16() isn't very random, so this mixes things up a bit
  seeds = random16(10, numLed - 10); //the seeds change how many sites will get added heat

  // Step 2. "Cool" down every location on the strip a little
  for ( unsigned int i = 0; i < numLed; i++) {
    heat[i] = qsub8( heat[i], COOLING);
  }

  // Step 3. Make the seeds into heat on the string
  for ( unsigned int j = 0 ; j < seeds ; j++) {
    if (random16() < TWINKLING) {
      //again, we have to mix things up so the same locations don't always light up
      random16_add_entropy( random());
      heat[random16(numLed)] = random16(50, 255);
    }
  }

  // Step 4. Add some "flicker" to LEDs that are already lit
  //         Note: this is most visible in dim LEDs
  for ( unsigned int k = 0 ; k < numLed ; k++ ) {
    if (heat[k] > 0 && random8() < FLICKER) {
      heat[k] = qadd8(heat[k] , 10);
    }
  }

  // Step 5. Map from heat cells to LED colors
  for ( unsigned int j = 0; j < numLed; j++) {
    showLeds[j] = twinkle_color( heat[j] );
  }

  FastLED.show();
}

void sparkle() {
  for (unsigned int sparkles = 0 ; sparkles < sparklesEntered ; sparkles ++) {
    seeds = random16(numLed / 8, numLed / 2);
    for ( unsigned int i = 0 ; i < seeds ; i++) {
      unsigned int pos = random16(numLed);
      random16_add_entropy( random() );
      heat[pos] = qadd8(heat[pos] , random8(50, 150));
    }
    for ( unsigned int j = 0; j < numLed; j++) {
      showLeds[j] = twinkle_color( heat[j] );
    }
    checkAndUpdate();
    FastLED.show();
  }
}

void spectrum() {
  for ( theta = 0 ; theta < numStrand * spinSpeed ; theta ++)
  {
    unsigned int freqBin, x ;
    //float level;

    if (fft.available())
    {
      freqBin = 2; //ignore the first two bins which contain DC offsets
      for (x = 0; x < numStrand; x++) {
        levels[x] = fft.read(freqBin, freqBin + frequencyBinsHorizontal[x] - 1);
        freqBin = freqBin + frequencyBinsHorizontal[x];
      }
    }

    maxLevel = maxarr(levels) ;
    if ( maxLevel > peakLevelSpectrum)
    {
      peakLevelSpectrum = maxLevel;
    }

    Serial.print("max level = ");
    Serial.println(maxLevel);
    Serial.print("peak level = ");
    Serial.println(peakLevelSpectrum);

    //relativeLevels = levels / peakLevelSpectrum ;
    for (x = 0; x < numStrand; x++) {
      relativeLevels[x] = levels[x] / peakLevelSpectrum ;
    }
    peakLevelSpectrum = peakLevelSpectrum - peakLevelSpectrum / levelForget;

    // printFFT();

    for (int row = 0 ; row < numLedStrand ; row++ )
    {
      for (int column = 0 ; column < numStrand / 2 ; column++ )
      {
        if ( ((relativeLevels[2 * column] + relativeLevels[2 * column + 1]) / 2 * numLedStrand) > row)
        {
          leds[numLedStrand - row - 1][column] = CHSV(column * 16 - 8 , 255 - 3.4 * row , 255 - 3 * row);
          leds[numLedStrand - row - 1][numStrand - 1 - column] = CHSV(column * 16 - 8 , 255 - 3.4 * row , 255 - 3 * row);
        }
        else
        {
          if (random8(maxLevel / peakLevelSpectrum * 255, 255) > 253  && random8(maxLevel / peakLevelSpectrum * 255, 255) > 253 && random8() > 250)
          {
            leds[numLedStrand - row - 1][column] += CHSV(0, 0, random8(150, 255)) ;
            leds[numLedStrand - row - 1][numStrand - 1 - column] += CHSV(0, 0, random8(150, 255)) ;
          }
          else {}
        }
      }
    }
    spin();
    transform(tempLeds);
    checkAndUpdate();
    fadeleds();
    FastLED.show();
  }
}

void transform(CRGB ledMatrix[numLedStrand][numStrand])
{
  for (int column = 0; column < numStrand; column++)
  {
    int strip = floor(column / numStrandStrip);
    for (int row = 0; row < numLedStrand; row++)
    {
      if ((column - strip * numStrandStrip) % 2 == 1) //check to see if this is the strip that goes bottom to top
      {
        showLeds[column * numLedStrand + row] = ledMatrix[numLedStrand - row - 1][column];
      }
      else
      {
        showLeds[column * numLedStrand + row] = ledMatrix[row][column];
      }
    }
  }
}

void spin()
{
  for ( int row = 0 ; row < numLedStrand ; row ++ )
  {
    for ( int column = 0 ; column < numStrand ; column ++ )
    {
      int newColumn = (column + theta / spinSpeed) % numStrand;
      tempLeds[row][newColumn] = leds[row][column];
    }
  }
}

void fadeall()
{
  for (int i = 0; i < numLed; i++) {
    showLeds[i].nscale8(250);
  }
}

void fadeleds()
{
  for (int row = 0; row < numLedStrand; row++) {
    for (int column = 0; column < numStrand; column++) {
      leds[row][column].nscale8(245);
    }
  }
}

float maxarr(float arr[])
{
  float maxVal = 0 ;
  for (unsigned int i = 0 ; i < sizeof(arr) ; i++)
  {
    if (arr[i] > maxVal)
    {
      maxVal = arr[i] ;
    }
    else {}
  }
  return maxVal ;
}

void pendulum() {
  for ( theta = 0 ; theta < numStrand * spinSpeed ; theta ++)
  {
    float level = 0;
    if (fft.available()) {
      freqBin = 2;
      for (int freq = 2 ; freq < 7 ; freq++ )
      {
        level = level + fft.read(freqBin, freqBin + frequencyBinsHorizontal[freq] - 1);
      }
      if (level > peakLevel)
      {
        peakLevel = level;
      }
      relativeLevel = level / peakLevel;
    }
    for (int column = 0; column < numStrand; column++)
    {
      sinelevel = (sin((t / 120.0 - PI / 2) * offset / timeConstants[column]) + 1) / 2 * numLedStrand;
      for (int row = 0; row < numLedStrand; row++)
      {
        if ( (sinelevel) == row) {
          leds[row][column] += CHSV(165, 255, 200);
        }
        //make little stars
        if (relativeLevel * 35 > random8() + random8())
        {
          leds[row][column] += CHSV(235, 240, random8(150, 255)) ;
        }
      }
    }
    peakLevel = peakLevel * .999;

    if (t == tchaos || t == 0)
    {
      bias = bias * (-1);
      //hue = 64;
    }
    t += bias;
    // Serial.print("t = ");
    // Serial.println(t);
    // Serial.println();
    spin();
    transform(leds);
    checkAndUpdate();
    fadeleds();
    FastLED.show();
  }
}

void fadeTempLeds()
{
  for (int row = 0; row < numLedStrand; row++) {
    for (int column = 0; column < numStrand; column++) {
      tempLeds[row][column].nscale8(35);
    }
  }
}

void fireworks() {
  for (int column = 0; column < numStrand; column++)
  {
    for (int row = 0; row < numLedStrand; row++)
    {
      if (random8() > 250 && random8() > 250)
      {
        leds[row][column] = CHSV{random8(), random8(), 255};
      }

      if (row > 1 && row < numLedStrand - 2 && column > 0 && column < numStrand - 1 )
      {
        if (column == 0) {
          tempLeds[row - 1][numStrand - 1] += leds[row][column];
          tempLeds[row + 1][numStrand - 1] += leds[row][column];
          tempLeds[row - 1][column + 1] += leds[row][column];
          tempLeds[row + 1][column + 1] += leds[row][column];
          tempLeds[row - 2][numStrand - 1] += leds[row][column];
          tempLeds[row + 2][numStrand - 1] += leds[row][column];
          tempLeds[row - 2][column + 1] += leds[row][column];
          tempLeds[row + 2][column + 1] += leds[row][column];
        }

        else if (column == numStrand - 1) {
          tempLeds[row - 1][column - 1] += leds[row][column];
          tempLeds[row + 1][column - 1] += leds[row][column];
          tempLeds[row - 1][0] += leds[row][column];
          tempLeds[row + 1][0] += leds[row][column];
          tempLeds[row - 2][column - 1] += leds[row][column];
          tempLeds[row + 2][column - 1] += leds[row][column];
          tempLeds[row - 2][0] += leds[row][column];
          tempLeds[row + 2][0] += leds[row][column];
        }

        else {
          tempLeds[row - 1][column - 1] += leds[row][column];
          tempLeds[row + 1][column - 1] += leds[row][column];
          tempLeds[row - 1][column + 1] += leds[row][column];
          tempLeds[row + 1][column + 1] += leds[row][column];
          tempLeds[row - 2][column - 1] += leds[row][column];
          tempLeds[row + 2][column - 1] += leds[row][column];
          tempLeds[row - 2][column + 1] += leds[row][column];
          tempLeds[row + 2][column + 1] += leds[row][column];
        }
        tempLeds[row][column] += leds[row][column];
      }
    }
  }
  fadeTempLeds();
  for (int column = 0; column < numStrand; column++)
  {
    for (int row = 0; row < numLedStrand; row++)
    {
      leds[row][column] = tempLeds[row][column] ;
      tempLeds[row][column] = CRGB::Black;
    }
  }
  transform(leds);
  fadeleds();
  FastLED.show();
  delay(50);
}

void glitter() {
  unsigned int freqBin;
  float level = 0;

  if (fft.available()) {
    freqBin = 2;
    for (int freq = 2 ; freq < 7 ; freq++ )
    {
      level = level + fft.read(freqBin, freqBin + frequencyBinsHorizontal[freq] - 1);
    }
    if (level > peakLevel)
    {
      peakLevel = level;
    }
    peakLevel = peakLevel - peakLevel / levelForget;
    relativeLevel = level / peakLevel;

    for (int column = 0; column < numStrand; column++)
    {
      for (int row = 0; row < numLedStrand; row++)
      {
        if (relativeLevel * 255 * (row) / numLedStrand > random8() + random8())
        {
          leds[row][column] += CHSV(40, 150, random8(0, 255)) ;
        }
      }
    }
    transform(leds);
    fadeleds();
    checkAndUpdate();
    FastLED.show();
  }
}

void rainbowColumns()
{
  for (int hue = 255 ; hue >=  0 ; hue --) {
    for (int column = 0; column < numStrand; column++)
    {
      CRGB rainLeds[numLedStrand];
      fill_rainbow (rainLeds, numLedStrand, hue, 2 );
      for ( int row = 0; row < numLedStrand; row++ )
      {
        leds[row][column] = rainLeds[row];
      }
    }
    transform(leds);
    delay(20);
    checkAndUpdate();
    FastLED.show();
  }
}

void whitePurpleColumns()
{
    for (int pos = 255 ; pos >=  0 ; pos --) {
    for (int column = 0; column < numStrand; column++)
    {
      CRGB paletteLeds[numLedStrand];
      fill_palette (paletteLeds, numLedStrand, pos, 8, PurpleWhite_p, brightness, LINEARBLEND);
      for ( int row = 0; row < numLedStrand; row++ )
      {
        leds[row][column] = paletteLeds[row];
      }
    }
    transform(leds);
    delay(5);
    checkAndUpdate();
    FastLED.show();
  }
}

void columnsAndRows() {
  for (int column = 0; column < numStrand; column++)
  {
    for (int row = 0; row < numLedStrand; row++)
    {
      leds[row][column] = CRGB::White;
      transform(leds);
      checkAndUpdate();
      FastLED.show();
      //delay(10);
      leds[row][column] = CRGB::Black;
      fill_solid(showLeds, numLed, CRGB::Black);
      checkAndUpdate();
      FastLED.show();
      //delay(20);
    }
  }

  for (int row = 0; row < numLedStrand; row++)
  {
    for (int column = 0; column < numStrand; column++)
    {
      leds[row][column] = CRGB::White;
      transform(leds);
      checkAndUpdate();
      FastLED.show();
      //delay(10);
      leds[row][column] = CRGB::Black;
      fill_solid(showLeds, numLed, CRGB::Black);
      //delay(10);
      checkAndUpdate();
      FastLED.show();
    }
  }
}

void america()
{
  static uint16_t heat[numLed];

  random16_add_entropy(random());
  checkAndUpdate();
  FastLED.show();

  //Cool down every cell a little
  for ( int i = 0; i < numLed; i++) {
    heat[i] = qsub8( heat[i], SPARKLER_COOLING / 255 + 1);
  }

  // Randomly ignite new 'sparks'
  if ( random8() / 8 < SPARKLER_SPARKING ) {
    random16_add_entropy(random());
    int y = random16(numLed); //y is the position, make sparks in this range
    heat[y] = qadd8( heat[y], random8(150, 255) ) - 1 ; //the -1 is here because of the 255 == yellow bug
  }

  // Step 4.  Map from heat cells to LED colors
  for ( int j = 0; j < numLed; j++)
  {
    showLeds[j] = SparklerColor(heat[j]);
  }
}

CRGB SparklerColor(int temperature)
{
  CRGB heatcolor;

  // Scale 'heat' down from 0-255 to 0-191,
  // which can then be easily divided into three
  // equal 'thirds' of 64 units each.
  uint8_t t192 = scale8_video( temperature, 192);
 
  // calculate a value that ramps up from
  // zero to 255 in each 'third' of the scale.
  uint8_t heatramp = t192 & 0x3F; // 0..63
  heatramp <<= 2; // scale up to 0..252

  // now figure out which third of the spectrum we're in:
  if ( t192 & 0x80) {
    // we're in the hottest third - White
    heatcolor.r = heatramp; // full red
    heatcolor.g = heatramp; // full green
    heatcolor.b = heatramp; // ramp up blue

  }
  else if ( t192 & 0x40 ) {
    // we're in the middle third - Red
    heatcolor.r = heatramp; // full red
    heatcolor.g = heatramp * 50 / 255; // ramp up green
    heatcolor.b = heatramp * 25 / 255; // no blue

  }
  else {
    // we're in the coolest third - Blue
    heatcolor.r = heatramp * 20 / 255; // ramp up red
    heatcolor.g = heatramp * 50 / 255; // no green
    heatcolor.b = heatramp; // no blue
  }
  return heatcolor;
}

CRGBPalette16 firePallet = HeatColors_p;
#define FIRE_COOLING  55
#define FIRE_SPARKING 120

void fire() {
  for (uint8_t column = 0; column < numStrand; column++) {
    fireColumn(column);
  }

  FastLED.delay(1000/60);
  checkAndUpdate();
  FastLED.show();
}

void fireColumn(uint8_t column) {
  // Array of temperature readings at each simulation cell
  static byte heat[numStrand][numLedStrand];
  bool reverseDirection = false;

  random16_add_entropy(random());

  // Step 1.  Cool down every cell a little
    for( int i = 0; i < numLedStrand; i++) {
      heat[column][i] = qsub8( heat[column][i],  random8(0, ((FIRE_COOLING * 10) / numLedStrand) + 2));
    }

    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= numLedStrand - 1; k >= 2; k--) {
      heat[column][k] = (heat[column][k - 1] + heat[column][k - 2] + heat[column][k - 2] ) / 3;
    }

    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if( random8() < FIRE_SPARKING ) {
      int y = random8(7);
      heat[column][y] = qadd8( heat[column][y], random8(160,255) );
    }

    // Step 4.  Map from heat cells to LED colors
    for( int j = 0; j < numLedStrand; j++) {
      // Scale the heat value from 0-255 down to 0-240
      // for best results with color palettes.
      byte colorindex = scale8( heat[column][j], 240);
      CRGB color = ColorFromPalette(firePallet, colorindex);
      int pixelnumber;
      if( reverseDirection ) {
        pixelnumber = (numLedStrand-1) - j;
      } else {
        pixelnumber = j;
      }
      leds[pixelnumber][column] = color;
    }

    // Step 5. Transform into columns and show
    transform(leds);
    checkAndUpdate();
    FastLED.show();
}

void artnetDisplay() {
  artnet.read();
}

void artnetCallback(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)
{
  sendFrame = 1;
  ledPos = 0;

  // Store which universe has got in
  if (universe < maxUniverses)
    universesReceived[universe] = 1;

  for (int i = 0 ; i < maxUniverses ; i++)
  {
    if (universesReceived[i] == 0)
    {
      // Serial.println("Broke");
      sendFrame = 0;
      break;
    }
  }

  // read universe and put into the right part of the display buffer
  for (int i = 0 ; i < length ; i++)
  {
    int bufferIndex = i + ((universe - startUniverse) * length);
    if (bufferIndex < numberOfChannels) // to verify
      channelBuffer[bufferIndex] = byte(data[i]);
  }      

  // send to leds
  
  for (int i = 0; i < numLedStrip * numStrip; i++)
  {
    // https://docs.google.com/spreadsheets/d/1GOh2TqrGf6v1Wm-PcW-rKaocxsi4zyZKuBk08LY2E3o/edit#gid=0
    if (i < numLedStrand || i >= 5 * numLedStrand) {
      ledPos = i;
    } else if (i >= 1 * numLedStrand && i < 2 * numLedStrand) {
      ledPos = i + 1 * numLedStrand;
    } else if (i >= 2 * numLedStrand && i < 3 * numLedStrand) {
      ledPos = i + 2 * numLedStrand;
    } else if (i >= 3 * numLedStrand && i < 4 * numLedStrand) {
      ledPos = i - 2 * numLedStrand;
    } else if (i >= 4 * numLedStrand && i < 5 * numLedStrand) {
      ledPos = i - 1 * numLedStrand;
    }
    // Put the DMX data into a temp array, ready for flipping the other side of the strip
    // led[led postion][strip number]
    // led postion is always 0..max number of leds per strip, so modulo gets us the remainder when going over
    // diving the position by the total number of strands tells us which strand were on (even if upside down)
    leds[ledPos % numLedStrand][ledPos / numLedStrand].setRGB(channelBuffer[(i) * 3], channelBuffer[(i * 3) + 1], channelBuffer[(i * 3) + 2]);
  } 
  
  if (sendFrame)
  {
    // Flip over the upsidedown LEDs
    transform(leds);
    
    // Display
    checkAndUpdate();
    FastLED.show();

    // Reset universeReceived to 0
    memset(universesReceived, 0, maxUniverses);
  }
}