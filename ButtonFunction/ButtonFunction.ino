#include <MultiButtons.h>
#include <Wire.h>
#include <math.h>
#include <HardwareSerial.h>
#include <Arduino.h>
#include "millisDelay.h"
#include "Thermal_Printer.h"

#define I2C_DEV_ADDR 0x55
#define DISPLAY_ADD 0x56

#define BAUD_RATE 115200

// UI Button Order
#define UP_BTN    0
#define RED_BTN   1
#define DOWN_BTN  2
#define GREEN_BTN 3

/* --- CALIBRATION PROCEDURE --- */

/* Fluid Calibration Values
 *  
 * Update the following values after calculating the motor on-time per ml 
 * dispensed of each product. The calibration process is documented here:
 * https://docs.google.com/spreadsheets/d/1RFrTEQL8gR5JihkoM7z2qM6ESyBOF3Ku0LrqBt9l5YI/edit#gid=556128175 
 */
double  product1TimePerML = 0.34; // CALIBRATE: time in seconds to dispense 1ml of Sunsilk Pink
double  product2TimePerML = 0.34; // CALIBRATE: time in seconds to dispense 1ml of Sunsilk Black
double  product3TimePerML = 0.34; // CALIBRATE: time in seconds to dispense 1ml of Lifebuoy Handwash
double  product4TimePerML = 0.34; // CALIBRATE: time in seconds to dispense 1ml of Dove Hairfall Rescue

/* Button Voltages
 *  
 * Button circuits are arranged such that each button is registered within a unique
 * analog voltage range on the corresponding pin. The Analog-to-digital (ADC) converter
 * converts these voltages to values from 0 (0.0V) to 4096 (3.3V). The ADC ranges are
 * defined in the following format:
 * 
 * {ADC_MIN, ADC_MAX}
 * 
 * for buttons 0 to 3 respectively. 
 * 
 * Tip:
 * View serial output messages to identify the true ADC readings of each button, then
 * change the ADC min and max values accordingly.
 */

// User Interface Buttons
int voltageRanges[][2] = {
  {600,800},      // Button 0 - UP
  {1390,1590},    // Button 1 - RED
  {2190,2500},    // Button 2 - DOWN
  {3000,3350}     // Button 3 - GREEN
}; 

// Limit Switches
int limitSwitchVoltageRanges[][2] = {
  {400,800},    // Limit Switch 0 (Dispenser 1 - Sunsilk Pink)
  {1300,1550},  // Limit Switch 1 (Dispenser 2 - Sunsilk Black)
  {2100,2500},  // Limit Switch 2 (Dispenser 3 - Lifebuoy Handwash)
  {3000,3250}   // Limit Switch 3 (Dispenser 4 - Dove Hairfall Rescue)
};

/* --- END OF CALIBRATION --- */

int btnCount = 4; // used to specify the number of buttons per dispenser (DO NOT CHANGE)

millisDelay SystemDelay;
HardwareSerial ESPButton(1);

/* --- Pin Configurations --- */
// button control pins
const uint8_t dispenser1Pin     = 34;
const uint8_t dispenser2Pin     = 35;
const uint8_t dispenser3pin     = 32;
const uint8_t dispenser4pin     = 33;
const uint8_t limitSwitchesPin  = 25;

// motor pins
const uint8_t motor1Pin1 = 27;
const uint8_t motor1Pin2 = 26;

// motor enable pins
const uint8_t enable1Pin = 17;
const uint8_t enable2Pin = 16;
const uint8_t enable3Pin = 14;
const uint8_t enable4Pin = 15;

/* --- PWM Properties --- */
const int freq = 30000;
const int pwmChannel1 = 0;
const int pwmChannel2 = 1;
const int pwmChannel3 = 2;
const int pwmChannel4 = 3;
const int resolution = 8;

/* --- Misc Variables --- */
String BatchNumber = "12B";
String EXPDate = "12-01-2025";

int state = 0;
int activeDispenser = 0;
int enteredAmount = 0;
int currentDispensedAmount = 0;
int amountTK = 40; // entered amount in TK, default is 40

/* --- Buzzer Variables --- */
int tempo = 180;
int buzzerPin = 23;
int melody[] = {659, 8, 3951, 4, 3322, 8};
// sizeof gives the number of bytes, each int value is composed of two bytes (16 bits)
// there are two values per note (pitch and duration), so for each note there are four bytes
int notes = sizeof(melody) / sizeof(melody[0]) / 2;
// this calculates the duration of a whole note in ms
int wholenote = (60000 * 4) / tempo;
int divider = 0, noteDuration = 0;

/* --- Product Variables --- */
String  product1 = "Sunsilk Pink";
String  product1Ingredients = "Water, Sodium laureth sulfate, Dimethiconol (and) TEA-dodecylbenzenesulfonate, Cocamidopropyl betaine,Sodium chloride, Perfume, Lysine Hydrochloride, Carbomer, Guar hydroxypropyltrimonium chloride, Panthenol, Hydrolyzed keratin,Yogurt powder, Sodium hydroxide, Disodium EDTA, DMDM Hydantoin, MICA and Titanium Dioxide, Citric acid, Methylchloroisothiazolinone and Methylisothiazolinone";
String  product1Cat = "Liquid Shampoo";
double  product1TKPerML = 0.5;
double  product1MLPerTK = 2.0;
int     product1TKLimit = 50;

String  product2 = "Sunsilk Black";
String  product2Ingredients = "Water, Sodium laureth sulfate, Dimethiconol( and) TEA-dodecylbenzenesulfonate, Cocamidopropyl betaine, perfume,Sodium chloride, Carbomer, Guar hydroxypropyltrimonium chloride, Sodium hydroxide, Disodium EDTA, DMDM Hydantoin, MICA and Titanium Dioxide, Lysine Hydrochloride, panthenol, Ethylhxyl methoxyxinnamate, Hydrolyzed conchiolin protein (Pearl extract), Phyllanthus emblica (Amla) fruits extract, Argania spinosa (Argan) kernel oil, Olea europaea (Olive) fruit oil, Camellia Oleifera (Camellia) seed oil, Prunus dulcis (sweet almond) oil, Simmondsia chinensis (Jojoba) seed oil, Citric acid, Methylchloroisothiazolinone and Methylisothiazolinone, CI 77266";
String  product2Cat = "Liquid Shampoo";

double  product2TKPerML = 0.5;
double  product2MLPerTK = 2.0;
int     product2TKLimit = 50;

String  product3 = "Lifebuoy Handwash";
String  product3Ingredients = "Aqua, sodium laureth sulfate, Sodium Chloride, Cocamide MEA, glycol distearate, Perfume, Citric Acid, Acrylate copolymer, Sodium benzoate, Hydroxy Stearic Acid, Glycerin, Tetrasodium EDTA, Sodium Hydroxide, Stearic Acid, Niacinamide, Tocopheryl Acetate, Sodium Ascorbyl Phosphate, Terpineol, VP/VA Copolymer, Thymol, Palmitic Acid, Sodium Carbonate, Sodium Glycolate, Silver Oxide, Lauric acid, Sodium Sulphate, CI 45100";
String  product3Cat = "Liquid Soap";

double  product3TKPerML = 0.4;
double  product3MLPerTK = 2.5;
int     product3TKLimit = 40;

String  product4 = "Dove Hairfall Rescue";
String  product4Ingredients = "Water, Sodium Laureth Sulfate, Dimethiconol, Cocamidopropyl Betaine, Glyceryl stearate, Sodium Chloride, Glycol Distearate, Perfume, Carbomer, Guar Hydroxypropyltrimonium Chloride, TEA-Dodecylbenzenesulfonate, Mica, Disodium EDTA, Citric Acid, DMDM Hydantoin, Stearalkonium Bentonite, Cetrimonium Chloride, Titanium Dyoxide, Sodium Benzoate, Magnesium Nitrate, Zinc Gluconate, Climbazole, Lysine HCI, Helianthus ANNUUS (Sunflower) Seed Oil, Methylchloroisothiazolinone, Magnesium Chloride, Methylisothiazolinone, CI 15985, CI 19140";
String  product4Cat = "Liquid Shampoo";

double  product4TKPerML = 0.66667;
double  product4MLPerTK = 1.5;
int     product4TKLimit = 65;

/* --- BT Printer --- */
uint8_t address[6] = {0x66, 0x22, 0x21, 0x67, 0x34, 0xC8};
String PrinterName = "BlueTooth Printer";
const char *pin = "0000";
bool isBTConnected;

/* --- Timers --- */
hw_timer_t *timer = NULL;
hw_timer_t *dispense_timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

bool timerEn = false;
bool dispenseTimerBool = false;

static bool i2cReady = true;

// Serial Connection
bool timeWritten = false;

/* --- Button Handler Functions --- */
// Must declare buttonHandler callback function and used 
// variables before instantiating MultiButtons object
void LimitSwitchHandler(MultiButtons *mb, int btnIndex)
{
  Serial.print("Limit Switch " + String(btnIndex) + " pressed.");
  Serial.print(" ADC Reading: ");
  MultiButtons::printReading(limitSwitchesPin);
  Serial.println();
}


/* --- UI Button Handler Functions (MultiButtons library) --- */
void Dispenser1Handler(MultiButtons *mb, int btnIndex)
{
  // Uncomment for debug messages
  String btnName;
  if      (btnIndex == GREEN_BTN) btnName = "GREEN";
  else if (btnIndex == RED_BTN)   btnName = "RED";
  else if (btnIndex == UP_BTN)    btnName = "UP";
  else if (btnIndex == DOWN_BTN)  btnName = "DOWN";

  Serial.println();
  Serial.println("Button Pressed: " + btnName);
  Serial.print("Button ADC Reading: ");
  MultiButtons::printReading(34);
  
  activeDispenser = 1;
  handleStateChange(activeDispenser, product1, btnIndex, product1TKLimit, mb);
}

void Dispenser2Handler(MultiButtons *mb, int btnIndex)
{
  activeDispenser = 2;
  handleStateChange(activeDispenser, product2, btnIndex, product2TKLimit, mb);
}

void Dispenser3Handler(MultiButtons *mb, int btnIndex)
{
  activeDispenser = 3;
  handleStateChange(activeDispenser, product3, btnIndex, product3TKLimit, mb);
}

void Dispenser4Handler(MultiButtons *mb, int btnIndex)
{
  activeDispenser = 4;
  handleStateChange(activeDispenser, product4, btnIndex, product4TKLimit, mb);
}

/* --- Must declare MultiButtons library objects after hander function declarations --- */
MultiButtons dis1(dispenser1Pin, btnCount, voltageRanges, Dispenser1Handler, 4095, BTN_TRIGGER_EDGE_PRESS);
MultiButtons dis2(dispenser2Pin, btnCount, voltageRanges, Dispenser2Handler, 4095, BTN_TRIGGER_EDGE_PRESS);
MultiButtons dis3(dispenser3pin, btnCount, voltageRanges, Dispenser3Handler, 4095, BTN_TRIGGER_EDGE_PRESS);
MultiButtons dis4(dispenser4pin, btnCount, voltageRanges, Dispenser4Handler, 4095, BTN_TRIGGER_EDGE_PRESS);
MultiButtons limitSwitches(limitSwitchesPin, btnCount, limitSwitchVoltageRanges, LimitSwitchHandler, 4095, BTN_TRIGGER_EDGE_PRESS);

/* 
 *  Resets program variables
*/
void reset()
{
  enteredAmount = 0;
  activeDispenser = 0;
  state = 0;
  currentDispensedAmount = 0;
  amountTK = 40;
  activeDispenser = 0;
  turnOffMotor();
}

/*
 * Plays a sound of duration ~1s on the buzzer.
 * 
 * Used to denote the machine being ready upon powerup, as well as
 * to inform the user when a dispense is completed.
 */
void playSong()
{
  // iterate over the notes of the melody.
  // Remember, the array is twice the number of notes (notes + durations)
  for (int thisNote = 0; thisNote < notes * 2; thisNote = thisNote + 2)
  {

    // calculates the duration of each note
    divider = melody[thisNote + 1];
    if (divider > 0)
    {
      // regular note, just proceed
      noteDuration = (wholenote) / divider;
    }
    else if (divider < 0)
    {
      // dotted notes are represented with negative durations!!
      noteDuration = (wholenote) / abs(divider);
      noteDuration *= 1.5; // increases the duration in half for dotted notes
    }

    // we only play the note for 90% of the duration, leaving 10% as a pause
    tone(buzzerPin, melody[thisNote], noteDuration * 0.9);

    // Wait for the specief duration before playing the next note.
    delay(noteDuration);

    // stop the waveform generation before the next note.
    noTone(buzzerPin);
  }
}

/* 
 *  Prints a receipt for the dispense via the BlueTooth Printer.
 *  Also sends the dispense telemetry to the Comms ESP32 module 
 *  via I2C and sounds the buzzer upon completion.
 *  
 *  @param dispenserIndex     The index of the currently active dispenser.
 *  @param choice             The name of the chosen product.
 *  @param amountDispensedTK  The amount that was actually dispensed, in Taka.
 *  @param enteredAmountTK    The amount that was entered by the user to dispense, in Taka.
 *  
 *  @notes  `amountDispensedTK` may not be equal to `enteredAmountTK`, since the user may 
 *          stop dispensing mid-way through. If the dispense times out, a receipt is 
 *          printed for the amount that was already dispensed at time-out.
 *  
 */
void printReceipt(int dispenserIndex, String choice, int amountDispensedTK, int enteredAmountTK)
{
  // turn off motors
  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, LOW);
  
  sendtoSlave("Date");
  delay(1000);
  String date = "";
  uint8_t bytesReceived = Wire.requestFrom(I2C_DEV_ADDR, 10); // it makes the request here
  while (Wire.available())
  { // If received more than zero bytes
    uint8_t temp[bytesReceived];
    date = Wire.readStringUntil('\n');
  }

  int price = amountDispensedTK;
  int savings = price * 0.1;
  
  //int volDispensedML = (int)(getVolume(dispenserIndex, price));
  //int volEnteredML = (int)(getVolume(dispenserIndex, enteredAmountTK));
  int volDispensedML = ceil(getVolume(dispenserIndex, price));
  int volEnteredML = ceil(getVolume(dispenserIndex, enteredAmountTK));
  
  if (isBTConnected)
  {
    Serial.println("Printing");

    // product info
    tpPrint("");
    tpPrint("");
    tpPrint("");

    tpSetFont(0, 0, 0, 0, 1);
    tpPrint((char *)"Product Name: ");
    tpSetFont(0, 0, 0, 0, 0);

    char buffer[choice.length() + 1];
    choice.toCharArray(buffer, choice.length() + 1);
    const char *choicePointer = buffer;
    tpPrint((char *)choicePointer);
    tpPrint((char *)"\r");

    tpSetFont(0, 0, 0, 0, 1);
    tpPrint((char *)"Product Catergory:");
    tpSetFont(0, 0, 0, 0, 0);
    switch (activeDispenser)
    {
    case 1:
    {
      char catBuffer[product1Cat.length() + 1];
      product1Cat.toCharArray(catBuffer, product1Cat.length() + 1);
      const char *ProductCatPointer = catBuffer;
      tpPrint((char *)ProductCatPointer);
    }
    break;
    case 2:
    {
      char catBuffer[product2Cat.length() + 1];
      product2Cat.toCharArray(catBuffer, product2Cat.length() + 1);
      const char *ProductCatPointer = catBuffer;
      tpPrint((char *)ProductCatPointer);
    }
    break;
    case 3:
    {
      char catBuffer[product3Cat.length() + 1];
      product3Cat.toCharArray(catBuffer, product3Cat.length() + 1);
      const char *ProductCatPointer = catBuffer;
      tpPrint((char *)ProductCatPointer);
    }
    break;
    case 4:
    {
      char catBuffer[product4Cat.length() + 1];
      product4Cat.toCharArray(catBuffer, product4Cat.length() + 1);
      const char *ProductCatPointer = catBuffer;
      tpPrint((char *)ProductCatPointer);
    }
    break;
    }
    tpPrint((char *)"\r");

    // volume dispensed
    tpSetFont(0, 0, 0, 0, 1);
    tpPrint((char *)"Net Volume: ");
    tpSetFont(0, 0, 0, 0, 0);
    
    char volumeBuffer[10];
    itoa(volDispensedML, volumeBuffer, 10);
    const char *VolPointer = volumeBuffer;
    tpPrint((char *)VolPointer);
    tpPrint(" ml\r");

    // purchase date
    tpSetFont(0, 0, 0, 0, 1);
    tpPrint((char *)"Date of purchase: ");
    tpSetFont(0, 0, 0, 0, 0);

    char dateBuffer[date.length() + 1];
    date.toCharArray(dateBuffer, date.length() + 1);
    const char *datePointer = dateBuffer;
    tpPrint((char *)datePointer);
    tpPrint((char *)"\r");

    // expiry date
    tpSetFont(0, 0, 0, 0, 1);
    tpPrint((char *)"Expiry Date: ");
    tpSetFont(0, 0, 0, 0, 0);

    char expdateBuffer[EXPDate.length() + 1];
    EXPDate.toCharArray(expdateBuffer, EXPDate.length() + 1);
    const char *expdatePointer = expdateBuffer;
    tpPrint((char *)expdatePointer);
    tpPrint((char *)"\r");

    // batch number
    tpSetFont(0, 0, 0, 0, 1);
    tpPrint((char *)"Batch Number: ");
    tpSetFont(0, 0, 0, 0, 0);

    char batchBuffer[BatchNumber.length() + 1];
    BatchNumber.toCharArray(batchBuffer, BatchNumber.length() + 1);
    const char *batchPointer = batchBuffer;
    tpPrint((char *)batchPointer);
    tpPrint((char *)"\r");

    // ingredients
    tpPrint("\r");
    tpPrint("\r");
    tpSetFont(0, 0, 0, 0, 1);
    tpPrint((char *)"Ingredients: ");
    tpSetFont(0, 0, 0, 0, 0);
    
    switch (dispenserIndex)
    {
    case 1:
    {
      char p1Ing[product1Ingredients.length() + 1];
      product1Ingredients.toCharArray(p1Ing, product1Ingredients.length() + 1);
      const char *p1Pointer = p1Ing;
      tpPrint((char *)p1Pointer);
      tpPrint((char *)"\r");
    }
    break;
    case 2:
    {
      char p2Ing[product2Ingredients.length() + 1];
      product2Ingredients.toCharArray(p2Ing, product2Ingredients.length() + 1);
      const char *p2Pointer = p2Ing;
      tpPrint((char *)p2Pointer);
      tpPrint((char *)"\r");
    }
    break;
    case 3:
    {
      char p3Ing[product3Ingredients.length() + 1];
      product3Ingredients.toCharArray(p3Ing, product3Ingredients.length() + 1);
      const char *p3Pointer = p3Ing;
      tpPrint((char *)p3Pointer);
      tpPrint((char *)"\r");
    }
    break;
    case 4:
    {
      char p4Ing[product4Ingredients.length() + 1];
      product4Ingredients.toCharArray(p4Ing, product4Ingredients.length() + 1);
      const char *p4Pointer = p4Ing;
      tpPrint((char *)p4Pointer);
      tpPrint((char *)"\r");
    }
    break;
    }

    tpPrint((char *)"\r");
    tpPrint((char *)"\r");
    tpPrint((char *)"\r");
 
    tpSetFont(1, 0, 1, 1, 1);
    tpPrint((char *)"Price: ");

    itoa(price, volumeBuffer, 10);
    const char *pricePointer = volumeBuffer;

    tpPrint((char *)pricePointer);
    tpPrint(" Taka  \r");

    tpSetFont(1, 0, 1, 0, 0);

    tpPrint((char *)"\r");
    tpPrint((char *)"\r");
    tpPrint((char *)"\r");
    tpPrint((char *)"You saved ");
    itoa(savings, volumeBuffer, 10);
    const char *savingPointer = volumeBuffer;
    tpPrint((char *)savingPointer);
    tpPrint((char *)" Taka ");

    tpPrint((char *)"\r");
    tpPrint((char *)"\r");
    tpPrint((char *)"\r");

    tpSetFont(1, 0, 0, 0, 0);
    tpPrint((char *)"Please wash and dry your bottle regularly.Use clean water.");

    tpSetFont(0, 0, 0, 0, 0);
    tpPrint((char *)"\r");
    tpPrint((char *)"\r");
    tpPrint((char *)"\r");
    tpPrint((char *)"\r");
  }

  playSong();
  
  sendtoSlave("Finished:" + choice + ":" + String(volEnteredML) + ":" + String(volDispensedML) + ":" + String(price) + ":");
  delay(1000);
  sendtoDisplay("S:"+savings);
  delay(2000);
  sendtoDisplay("Finished");
  reset();
}

/*
 * Dispense Handler
 * 
 * Controls the dispensing process. The dispense is performed in TK increments, with 
 * the motor on-time per TK dispensed calculated with @f$T=p*t$ where p is the price 
 * in TK per ml and t is the calibrated time per ml of the product in question. After 
 * each TK increment, the state of the corresponding limit switch is checked.
 * Dispensing continues if the limit switch is engaged, and otherwise pauses. A pause
 * of longer than 20 seconds results in a time-out, in which case printReceipt() is
 * called to print a receipt for the amount that was already dispensed, provided it
 * is non-zero.
 * 
 * 
 *  @param dispenserIndex     The index of the currently active dispenser.
 *  @param choice             The name of the chosen product.
 *  @param enteredAmountTK    The amount that was entered by the user to dispense, 
 *                            in Taka.
 *  @param mb                 Pointer to MultiButtons object to handle limit switch 
 *                            events mid-dispense.
 *                            
 */
void dispense(int dispenserIndex, String choice, int enteredAmountTK,  MultiButtons *mb)
{
  // the volume amount by which to calculate the dispense
  int enteredAmount = ceil(getVolume(dispenserIndex, enteredAmountTK));
  
  float timeTakenD = timerReadSeconds(timer);
  
  static bool writeUnder = false;
  static bool isMotorOn = false;
  
  bool breakout = false;
  
  limitSwitches.loop();
  if      (dispenserIndex == 1) dis1.loop();
  else if (dispenserIndex == 2) dis2.loop();
  else if (dispenserIndex == 3) dis3.loop();
  else if (dispenserIndex == 4) dis4.loop();
  
  timerRestart(dispense_timer);
  timerRestart(timer);
  
  delay(1000); 

  Serial.print(F("Entered Amount TK: "));
  Serial.println(enteredAmountTK);
  
  dispenseTimerBool = false;
  
  double totalTimeTaken = 0;
  int last_written_amount = -1;
  
  timerEn = false;
  
  for (int i = 0; i <= enteredAmountTK; i++)
  {
    if (breakout)
    {
      turnOffMotor();
      break;
    }
    
    unsigned long previousMillis = millis();
    
    while (!limitSwitches.isPressing(dispenserIndex - 1))
    { 
      i = currentDispensedAmount;
      timerRestart(dispense_timer);
      timerStop(dispense_timer);
      turnOffMotor();
      isMotorOn = false;

      dispenseTimerBool = false;
      
      if (!timerEn)
      {
        Serial.print("Limit Switch ADC Reading: ");
        MultiButtons::printReading(limitSwitchesPin);
        Serial.println();
        
        timerEn = true;
        sendtoDisplay("Push Bottle");
        
        if (currentDispensedAmount > 0)
        {
          turnMotorReverse(dispenserIndex);
          turnOffMotor();
        }
      }
      
      unsigned long currentMillis = millis();
      
      // timeout of 20 seconds
      if (currentMillis - previousMillis >= 20000)
      {
        sendtoSlave("Time Out:" + String(choice) + ":" + String(enteredAmountTK) + ":" + String(currentDispensedAmount) + ":");

        // print receipt for what was dispensed before timeout
        if (currentDispensedAmount > 0)
        {
          sendtoDisplay("Printing");
          printReceipt(dispenserIndex, choice, currentDispensedAmount, enteredAmountTK);
          breakout = true;
          break;
        }
        else
        {
          sendtoDisplay("Finished");
        }
        breakout = true;
        break;
      }
    }

    if (breakout) break;
    
    while (limitSwitches.isPressing(dispenserIndex - 1))
    {
      currentDispensedAmount++;
      timerEn = false;
      
      Serial.println(F("Pressing"));
      
      if (!dispenseTimerBool)
      {
        timerRestart(dispense_timer);
        timerStart(dispense_timer);
        dispenseTimerBool = true;
      }
      
      int dis_timeTaken = timerReadMilis(dispense_timer);
      
      String DisMsg = "D:";
      
      if (currentDispensedAmount < 10) DisMsg += "0";
      DisMsg += String(currentDispensedAmount) + ":";
      
      if (enteredAmountTK < 10) DisMsg += "0";
      DisMsg += String(enteredAmountTK) + ":";
      
      sendtoDisplay(DisMsg);

      // TODO: decrease the time spent in this blocking while loop at a time?
      // For example, change 1000 to 500 but increment "currentDispensedAmount" in halves
      while (dis_timeTaken < getTimePerTK(dispenserIndex) * 1000)
      {
        if (!isMotorOn)
        {
          turnOnMotor(dispenserIndex);
          isMotorOn = true;
        }

        dis_timeTaken = timerReadMilis(dispense_timer);
      }
      
      //Serial.print("Limit Switch ADC Reading: ");
      //MultiButtons::printReading(limitSwitchesPin);
      //Serial.println();

      dispenseTimerBool = false;
      totalTimeTaken += dis_timeTaken;

      if (currentDispensedAmount >= enteredAmountTK)
      {
        // 100ms in reverse to pump excess product back into tubing
        turnMotorReverse(dispenserIndex);
        turnOffMotor();
        isMotorOn = false;

        sendtoDisplay("Printing");
        printReceipt(dispenserIndex, choice, currentDispensedAmount, enteredAmountTK);
        breakout = true;
        break;
      }
    } // end while(limitSwitches.isPressing(dispenserIndex-1))
    if (breakout) break;
  } // end for
  turnOffMotor();
  reset();
}

/* Handle State Changes  
 * 
 * Handles transitions between program states, comprising the following:
 * 
 * 0  ->  Initial state, waiting for any button press
 * 1  ->  Selecting the amount to dispense
 * 2  ->  Either waiting to dispense or dispensing/printing
 * 99 ->  Waiting for cancel confirmation
 * 
 * --- Message Formats ---
 * 
 * Slave Messages, i.e. sendToSlave() go to the Comms module. 
 * Format:
 * <eventType>:<productChoice>:<enteredAmount>:<dispensedAmount>:<price>
 * The comms module ignores the value 999 for enteredAmount, dispensedAmount and price
 *
 * Display Messages, i.e. sendToDisplay() go to the Display module.
 * Formats (X is placeholder for numerical digits):
 * 1. X (send just the index of the active dispenser)
 * 2. S:XX (send the savings value for the savings screen)
 * 3. N:XX:XXX: (number changes on UI menu, selecting amount. Order is N:<price>:<volume>:)
 * 4. D:XX:XX: (dispensing progress updates. Order is D:<currentAmount>:<totalAmount>:)
 * 5. MSG (send just a string message. Options: "Press Green", "Push Bottle", "Cancel", "Printing", "Finished")
 */
void handleStateChange(int dispenserIndex, String choice, int btnIndex, int amountTKMax, MultiButtons *mb)
{

  Serial.print(F("Entered 'handleStateChange()'. Initial State = "));
  Serial.println(state);
  Serial.print(F("Active Dispenser: "));
  Serial.println(dispenserIndex);
  
  switch (state)
  {
    // started new dispense
    case 0:
    {
      Serial.println(F(" (Started New Dispense)"));
      
      sendtoSlave("Started:" + String(choice) + ":999:999:999:");
      sendtoDisplay(String(dispenserIndex));

      // dispenser 4 (Dove Hairfall Rescue) has a default value of 50 TK
      if (dispenserIndex == 4) amountTK = 50;

      int amountML = ceil(getVolume(dispenserIndex, amountTK)); // calculate volume based on TK amount
      sendtoDisplay("N:" + String(amountTK) + ":" + String(amountML) + ":"); // format message and send to display
      state = 1;
    }
    break;
  
    // choosing amount
    case 1:
    {
      Serial.println(F(" (Choosing Amount)"));
      switch (btnIndex)
      {
        case GREEN_BTN:
        {
          if (amountTK > 4)
          { 
            int amountML = ceil(getVolume(dispenserIndex, amountTK));
            sendtoSlave("Amount Chosen:" + choice + ":" + String(amountML) + ":999:" + String(amountTK) + ":");
            sendtoDisplay("Press Green");
            state = 2;
            break;
          }
        }
        break;
        
        case RED_BTN:
        {
          state = 99;
          sendtoDisplay("Cancel");
          sendtoSlave("Cancel Init:" + choice + ":" + String(amountTK) + ":999:999:");
        }
        break;
  
        case DOWN_BTN:
        {
          // decrease amount
          while (mb->isPressing(DOWN_BTN) && amountTK > 5)
          {
            // decrement volume amount and price
            amountTK -= 5;
            int amountML = ceil(getVolume(dispenserIndex, amountTK));
            Serial.print(F("Amount: "));
            Serial.println(amountML);
            Serial.print(F("Price: "));
            Serial.print(amountTK);
            Serial.println(" TK");
            sendtoDisplay("N:" + String(amountTK) + ":" + String(amountML) + ":");
            delay(500);
          }
        }
        break;
      
        // Increase Amount
        case UP_BTN:
        {
          while (mb->isPressing(UP_BTN) && amountTK < amountTKMax)
          {
            // increment volume amount and price
            amountTK += 5;    
            int amountML = ceil(getVolume(dispenserIndex, amountTK));
            Serial.print(F("Amount: "));
            Serial.println(amountML);
            Serial.print(F("Price: "));
            Serial.print(amountTK);
            Serial.println(" TK");
            sendtoDisplay("N:" + String(amountTK) + ":" + String(amountML) + ":");
            delay(500);
          }
        }
        break;
      }
    }
    break;
  
    // Press Green Button to Dispense
    case 2:
    {
      Serial.println(F(" (Press Green to Start Disp.)"));
      switch (btnIndex)
      { 
        case GREEN_BTN:
        {
          dispense(dispenserIndex, choice, amountTK, mb);
        }
        break;
        
        case RED_BTN:
        {
          sendtoDisplay("Cancel");
          sendtoSlave("Cancel Init:" + choice + ":" + String(amountTK) + ":999:999:");
          state = 99;
        }
        break;
      }
    }
    break;
  
    // Amount Chosen Cancel State
    case 99:
    {
      Serial.println(F(" (Cancel? State)"));
      switch (btnIndex)
      {
        case RED_BTN:
        {
          sendtoDisplay("Finished");
          reset();
        }
        break;
    
        case GREEN_BTN:
        {
          state = 1;
          int amountML = ceil(getVolume(dispenserIndex, amountTK));
          sendtoDisplay("N:" + String(amountTK) + ":" + String(amountML) + ":");
        }
        break;        
      }
    }
    break;
  }
  Serial.print(F("Changed to State "));
  Serial.print(state);
}

/* --- Motor Control --- */
void turnMotorReverse(int dispenserIndex)
{
  if      (dispenserIndex == 1) analogWrite(enable1Pin, 255);
  else if (dispenserIndex == 2) analogWrite(enable2Pin, 255);
  else if (dispenserIndex == 3) analogWrite(enable3Pin, 255);
  else if (dispenserIndex == 4) analogWrite(enable4Pin, 255);

  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, HIGH);
  
  delay(100);
}

void turnOnMotor(int dispenserIndex)
{
  if      (dispenserIndex == 1) analogWrite(enable1Pin, 255);
  else if (dispenserIndex == 2) analogWrite(enable2Pin, 255);
  else if (dispenserIndex == 3) analogWrite(enable3Pin, 255);
  else if (dispenserIndex == 4) analogWrite(enable4Pin, 255);

  digitalWrite(motor1Pin1, HIGH);
  digitalWrite(motor1Pin2, LOW);
}

void turnOffMotor()
{
  analogWrite(enable1Pin, 0);
  analogWrite(enable2Pin, 0);
  analogWrite(enable3Pin, 0);
  analogWrite(enable4Pin, 0);

  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, LOW);
}

double getPrice(int dispenserIndex, double amountML)
{
  if      (dispenserIndex == 1) return product1TKPerML * amountML;
  else if (dispenserIndex == 2) return product2TKPerML * amountML;
  else if (dispenserIndex == 3) return product3TKPerML * amountML;
  else if (dispenserIndex == 4) return product4TKPerML * amountML;
  
  return amountML;
}

double getTimePerML(int dispenserIndex)
{
  if      (dispenserIndex == 1) return product1TimePerML;
  else if (dispenserIndex == 2) return product2TimePerML;
  else if (dispenserIndex == 3) return product3TimePerML;
  else if (dispenserIndex == 4) return product4TimePerML;
  
  return 1.0;
}

double getTimePerTK(int dispenserIndex)
{
  if      (dispenserIndex == 1) return product1TimePerML * product1MLPerTK;
  else if (dispenserIndex == 2) return product2TimePerML * product2MLPerTK;
  else if (dispenserIndex == 3) return product3TimePerML * product3MLPerTK;
  else if (dispenserIndex == 4) return product4TimePerML * product4MLPerTK;
  
  return 1.0;
}

double getVolume(int dispenserIndex, double priceTK)
{
  if      (dispenserIndex == 1) return product1MLPerTK * priceTK;
  else if (dispenserIndex == 2) return product2MLPerTK * priceTK;
  else if (dispenserIndex == 3) return product3MLPerTK * priceTK;
  else if (dispenserIndex == 4) return product4MLPerTK * priceTK;
  
  return priceTK;
}

/* --- I2C Functions --- */

// Write & send message to the slave
void sendtoSlave(String message)
{
  Wire.beginTransmission(I2C_DEV_ADDR);       // Address the slave
  Wire.print(message);                        // Add data to buffer
  uint8_t error = Wire.endTransmission(true); // Send buffered data
  //if (error != 0) Serial.printf("endTransmission error: %u\n", error); // Prints if there's an actual error
}

// Send i2c data to display
void sendtoDisplay(String message)
{
  Wire.beginTransmission(DISPLAY_ADD);        // Address the slave
  Wire.print(message);                        // Add data to buffer
  uint8_t error = Wire.endTransmission(true); // Send buffered data
  //if (error != 0) Serial.printf("endTransmission error: %u\n", error); // Prints if there's an actual error
}

// Request data from slave device
String requestData(int slaveAddress, int messageLength)
{
  String dataReceived = "";
  uint8_t bytesReceived = Wire.requestFrom(slaveAddress, messageLength); // it makes the request here
  
  // If received more than zero bytes
  if ((bool)bytesReceived)
  { 
    dataReceived = Wire.readStringUntil('}');
    return dataReceived;
  }

  return "failed request";
}

static void connect_to_BT_printer()
{
  Serial.println("Scanning for a BT printer...");
  if (tpScan())
  {
    Serial.println(F("Trying to connect to printer"));
    if (tpConnect())
    {
      Serial.println(F("Connected to BT Printer"));
      isBTConnected = true;
    }
    else
    {
      isBTConnected = false;
      Serial.println(F("Failed to connect to BT printer"));
    }
  }
  else Serial.println("Could not find a BT printer.");
}

void setup()
{
  Serial.begin(BAUD_RATE);
  Wire.begin(); // Starting Wire as Master
  Serial.println(F("ESP Button Board Init"));
  connect_to_BT_printer();

  pinMode(motor1Pin1, OUTPUT);
  pinMode(motor1Pin2, OUTPUT);
  
  pinMode(enable1Pin, OUTPUT);
  pinMode(enable2Pin, OUTPUT);
  pinMode(enable3Pin, OUTPUT);
  pinMode(enable4Pin, OUTPUT);

  timer = timerBegin(0, 80, true);
  dispense_timer = timerBegin(0, 80, true);
  timerStop(timer);
  timerStop(dispense_timer);

  // configure LED PWM functionalitites
  ledcSetup(pwmChannel1, freq, resolution);
  ledcSetup(pwmChannel2, freq, resolution);
  ledcSetup(pwmChannel3, freq, resolution);
  ledcSetup(pwmChannel4, freq, resolution);
  
  // attach the channel to the GPIO to be controlled
  ledcAttachPin(enable1Pin, pwmChannel1);
  ledcAttachPin(enable2Pin, pwmChannel2);
  ledcAttachPin(enable3Pin, pwmChannel3);
  ledcAttachPin(enable4Pin, pwmChannel4);

  dis1.begin(); // Prepare reading button state
  dis2.begin();
  dis3.begin();
  dis4.begin();
  limitSwitches.begin();
  
  reset();
  
  Serial.println(F("Ready."));
  playSong();
}

void loop()
{
  if (!i2cReady)
  {
    sendtoSlave("Ready");
    delay(1000);
    String results = requestData(I2C_DEV_ADDR, 16); // Receive data from Slave
    
    if (results == "Yes")
    {
      i2cReady = true;
      Serial.println(results);
    }
  }
  
  // Read button states with debouncing
  if (activeDispenser == 0)
  {
    dis1.loop(); 
    dis2.loop();
    dis3.loop();
    dis4.loop();
  }
  else if (activeDispenser == 1) dis1.loop();
  else if (activeDispenser == 2) dis2.loop();
  else if (activeDispenser == 3) dis3.loop();
  else if (activeDispenser == 4) dis4.loop();
  else activeDispenser = 0;
}
