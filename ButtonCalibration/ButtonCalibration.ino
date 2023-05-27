#include <MultiButtons.h>

/*  --- Button Pin --- 
 *  Set the pin depending on which set of buttons/limit switches are being checked:
 *  
 *  Dispenser 1 Buttons - pin 34
 *  Dispenser 2 Buttons - pin 35
 *  Dispenser 3 Buttons - pin 32
 *  Dispenser 4 Buttons - pin 33
 *  
 *  Limit Switches - pin 25
 *
 */
int buttonPin = 25;

/* --- --- */

int buffer[5];
int reading, sum, avg;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ;
  }
  Serial.println();
}

void loop() {
  static int i = 0;
  reading = MultiButtons::printReading(buttonPin);
  if (reading > 0) {
    buffer[i] = reading;
    if (i == 4) {
      for (int j = 0; j < 5; j++) {
        sum += buffer[j];
      }
      avg = sum / 5;
      Serial.print("Avarage Reading: ");
      Serial.println(avg);
    }
    i++;
    if (i > 4) {
      i = 0;
      sum = 0;
    }
  }
  delay(200);
}
