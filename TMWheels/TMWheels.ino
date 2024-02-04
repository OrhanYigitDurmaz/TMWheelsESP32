/*
 * ESP32 Bluetooth Support By Orhan Yigit Durmaz 2024 orhanyigitv2durmaz [at] gmail [dot] com 
 * 
 * Code by Insert Coin
 * V1.01 February 2022
 * Code is an enhanced version of Noel McCullagh's code (see https://www.noelmccullagh.com)
 * which he based on Taras' code at http://rr-m.org
 * So thank you both :-)
 * Enhancements: 
 *  - supports hot swapping of Thrustmaster R383, F1 and Ferrari 599xx wheels
 *  - all buttons are working
 *  - button numbering is the same as when the wheels are on a Thrustmaster base
 *    (paddles are always buttons 1 and 2 for example)
 *    
 * V1.01 October 2022: tiny bug fixed, 'button pressed' was sent continuously over USB as long as
 * a button was pressed (telling 'button pressed' once is enough)
 *
 / February 2024: Added ESP32 Bluetooth support. 
*/

/*
* On Arduino Pro Micro side it must be connected as follows:
 *          ----___----
 *        / 6   [ ]   5 \
 *       |      [_]      |   (as seen from the female front of the connector)
 *       | 4           3 |
 *        \___       ___/
 *            |2   1|
 *            \_____/
 *            
 * 1              -> not used (or can be connected to arduino MOSI pin 16)
 * 2 Green - GND  -> Arduino Pro Micro GND pin
 * 3 White - MISO -> Arduino Pro Micro pin 14
 * 4 Yellow - SS  -> Arduino Pro Micro pin 7
 * 5 Black - SCK  -> Arduino Pro Micro pin 15
 * 6 Red - +VCC   -> Arduino +5V pin (or RAW if USB current is +5V already)
 * 
 * Wheels and official Thrustmaster button numbers:
 * 
 * R383 wheel                     F1 wheel                       Ferrari 599xx
 * ----------------------------   ----------------------------   --------------------------------
 * Byte 1                         Byte 1                         Byte 1
 * 7 - constant 0                 7 - DRS (5)    (yes, odd)      7 - constant 1
 * 6 - constant 0                 6 - constant 0                 6 - constant 0
 * 5 - constant 1                 5 - constant 1                 5 - constant 1
 * 4 - constant 1                 4 - constant 1                 4 - left paddle (1)
 * 3 - constant 0                 3 - left paddle (1)            3 - right paddle (2)
 * 2 - constant 1                 2 - right paddle (2)           2 - PIT (blue upper left) (3)
 * 1 - constant 0                 1 - N (3)                      1 - WASH (blue down left) (4)
 * 0 - bottom right yellow (6)    0 - PIT (4)                    0 - RADIO (blue upper right) (5)
 * 
 * Byte 2                         Byte 2                         Byte 2
 * 7 - right paddle (2)           7 - START (13)                 7 - black down left (6)
 * 6 - top right black (5)        6 - 10+ (6)                    6 - MAIN left (7)
 * 5 - top right red (9)          5 - B0 (7)                     5 - MAIN right (8)
 * 4 - constant 0                 4 - WET (8)                    4 - SCROLL (red upper right) (9)
 * 3 - bottom left red (7)        3 - PL (9)                     3 - FLASH (red upper left) (10)
 * 2 - bottom right white (13)    2 - K (10)                     2 - constant 0
 * 1 - bottom right red (8)       1 - PUMP (11)                  1 - constant -
 * 0 - constant 0                 0 - 1- (12)                    0 - MAIN pushed in (13)
 * 
 * Byte 3                         Byte 3                         Byte 3
 * 7 - bottom left yellow (3)     7 - right Dpad up (18)         7 - Dpad down
 * 6 - top left red (10)          6 - left Dpad down             6 - Dpad right
 * 5 - Dpad left                  5 - left Dpad right            5 - Dpad left
 * 4 - Dpad up                    4 - left Dpad left             4 - Dpad up
 * 3 - Dpad right                 3 - left Dpad up               3 - constant 0
 * 2 - Dpad down                  2 - right Dpad down (19)       2 - constant 0
 * 1 - bottom left yellow (4)     1 - right Dpad right (20)      1 - constant 0
 * 0 - left paddle                0 - right Dpad left (21)       0 - constant 0
 * 
 * Byte 4                         Byte 4
 * 7 - 1 when Dpad pressed        7 - 
 * 6 - 1 when Dpad pressed        6 - CHRG down (16)
 * 5 - 1 when Dpad pressed        5 - DIF IN up (14)
 * 4 - 1 when Dpad pressed        4 - DIF IN down (15)
 * 3 - 1 when Dpad pressed        3 - CHRG up (17)
 * 2 - 1 when Dpad pressed        2 - 
 * 1 - 1 when Dpad pressed        1 - 
 * 0 - 1 when Dpad pressed        0 -
*/


#include <SPI.h>
#include <BleGamepad.h>


// 21 buttons, 1 hatswitch

  
const int slaveSelectPin = 15;
byte pos[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
byte currBytes[] = {0x00, 0x00, 0x00, 0x00, 0x00};
byte prevBytes[] = {0x00, 0x00, 0x00, 0x00, 0x00};
int wheelbyte, fourthbyte, fifthbyte, wheelID;
bool btnState, joyBtnState, prevJoyBtnState, buttonsreset, wheelIdentified;
const bool debugging = false; // Set to true to see wheel bits debug messages on the com port
int bit2btn[] = {-1,-1,-1,-1,-1,-1,-1,-1,  -1,-1,-1,-1,-1,-1,-1,-1,  -1,-1,-1,-1,-1,-1,-1,-1,  -1,-1,-1,-1,-1,-1,-1,-1}; // working array of buttons
int F599Btn[] = {-1,-1,-1,0,1,2,3,4,  5,6,7,8,9,-1,-1,12,  33,32,34,31,-1,-1,-1,-1,  -1,-1,-1,-1,-1,-1,-1,-1}; // button numbers 599xx wheel
int R383Btn[] = {-1,-1,-1,-1,-1,-1,-1,5,  1,4,8,-1,6,12,7,-1,  2,9,34,31,32,33,3,0,  -1,-1,-1,-1,-1,-1,-1,-1}; // button numbers R383 wheel
int F1Btn[] = {4,-1,-1,-1,0,1,2,3,  12,5,6,7,8,9,10,11,  17,33,32,34,31,18,19,20,  -1,15,13,14,16,-1,-1,-1}; // button numbers F1 wheel

//SPIClass SPI1(HSPI);

SPIClass * hspi = NULL;

BleGamepad bleGamepad("Thrustmaster Wheel ESP32", "OrhanYigitDurmaz", 100);
BleGamepadConfiguration bleGamepadConfig;

#define HSPI_MISO   12
#define HSPI_MOSI   13
#define HSPI_SCLK   14
#define HSPI_SS     15

void setup() {
  Serial.begin(115200);
  
  
  //SPI1.begin();
  //SPI1.beginTransaction(SPISettings(40000, MSBFIRST, SPI_MODE0));

  hspi = new SPIClass(HSPI);
  hspi->begin(HSPI_SCLK, HSPI_MISO, HSPI_MOSI, HSPI_SS);
  hspi->beginTransaction(SPISettings(40000, MSBFIRST, SPI_MODE0));

  //initialise hspi with default pins
  //SCLK = 14, MISO = 12, MOSI = 13, SS = 15                 
  pinMode(slaveSelectPin, OUTPUT);


  
  bleGamepadConfig.setAutoReport(false);
  bleGamepadConfig.setControllerType(CONTROLLER_TYPE_GAMEPAD);
  bleGamepadConfig.setButtonCount(21); //21 buttons, 1 hat
  bleGamepadConfig.setHatSwitchCount(1);
  bleGamepad.begin(&bleGamepadConfig);
}

// print byte as binary, zero padded if needed 
// "127" -> "01111111"
void printBinary(byte data) {
 for(int i=7; i>0; i--) {
   if (data >> i == 0) {
     Serial.print("0");
   } else {
     break;
   }
 }
 Serial.print(data,BIN);
 Serial.print(" ");
}

void loop() {

   if (bleGamepad.isConnected()) {
    // tell the wheel, that we are ready to read the data now
    digitalWrite(slaveSelectPin, LOW);
    // the chips in the wheel need some time to wake up
    delayMicroseconds(40);
  
    //read the wheel's 5 bytes
    for(int i = 0; i < 5; i++) {
      //currBytes[i] = ~SPI1.transfer(0x00); //spi->transfer(data);
      currBytes[i] = ~hspi->transfer(0x00);
      delayMicroseconds(40);
    }

    // release the wheel
    digitalWrite(slaveSelectPin, HIGH);
    delayMicroseconds(40);
  
    if (debugging) {
      for(int i = 0; i < 5; i++) {
        printBinary(currBytes[i]);
        Serial.print("\t");
      }
      Serial.println();
    }

    // Check for sane input: is the wheel plugged in?
    // Unplugged: first byte has all bits set or unset
    // When plugged in F1 and Ferrari 599xx wheel has bits 7, 6, 5 set as 101 (160 dec)
    // Sparco R383 has bits 7, 6, 5 set as 001 (32 dec)
    wheelbyte = currBytes[0] & B11100000;
    fourthbyte = currBytes[3] & B00100000;
    fifthbyte = currBytes[4] & B00001111;
    buttonsreset = false;
  
    while ((wheelbyte != 192) and (wheelbyte != 160) and (wheelbyte != 32)) {  // unknown wheel or wheel unplugged

      wheelIdentified = false;
        
     // Reset all buttons to avoid stuck buttons when unplugged
      if (buttonsreset == false) {
        bleGamepad.setHat1(HAT_CENTERED);  // release hatswitch
        for(int b = 0; b < 21; b++) {    // one button at a time 0 to 20
          setButton(b, 0);    // release the button
        }
        buttonsreset = true;           // do it just once
      }
     
      if (debugging) Serial.println("Wheel not plugged in, waiting...");

     // tell the wheel, that we are ready to read the data now
      digitalWrite(slaveSelectPin, LOW);
     // the chips in the wheel need some time to wake up
      delayMicroseconds(40); 
     
     // read the wheel's 5 bytes
      for(int i = 0; i < 5; i++) {
        //currBytes[i] = ~SPI1.transfer(0x00);
        currBytes[i] = ~hspi->transfer(0x00);
        delayMicroseconds(40);
      }

     // release the wheel
      digitalWrite(slaveSelectPin, HIGH);
      delayMicroseconds(40);

      if (debugging) for(int i=0; i<5; i++) {
        printBinary(currBytes[i]);
        Serial.print("\t");
      }

      if (debugging) Serial.println();

      wheelbyte = currBytes[0] & B11100000;  // same as above, for identifying the wheel below
      fourthbyte = currBytes[3] & B00100000;
      fifthbyte = currBytes[4] & B00001111;
     
     // Still no sane input? Wait...
      if ((wheelbyte != 192) and (wheelbyte != 160) and (wheelbyte != 32)) {
        delay(1000);
      }
    }

    if (wheelIdentified == false) {

      if ((wheelbyte == 160) and (fifthbyte == 0)) {
      wheelID = 2;                                           // Ferrari 599xx wheel
      memcpy(bit2btn,F599Btn,sizeof(F599Btn));               // button numbers 599xx wheel to working array
      wheelIdentified = true;
      }

    } else if (wheelbyte == 32) {

      if (((fourthbyte == 0) and (fifthbyte == 0)) or ((currBytes[3] == 255) and (currBytes[4] == 255))) {
        wheelID = 3;                               // Sparco R383 connected
        memcpy(bit2btn,R383Btn,sizeof(R383Btn));   // button numbers R383 wheel to working array
        wheelIdentified = true;
      }
      

    } else if ((wheelbyte == 32) and (fifthbyte == 15)) {      // F1 wheel sets last four bits of byte 5 as 1, 599xx wheel sets byte 5 as zero
    
      wheelID = 1;                          // F1 wheel connected
      memcpy(bit2btn,F1Btn,sizeof(F1Btn));  // button numbers F1 wheel to working array
      wheelIdentified = true;
    }



    if (debugging) { 
      Serial.print(wheelbyte);
      Serial.print("\t");
      Serial.println(wheelID);
    }
  
  // deal with the buttons first
    if (wheelIdentified) 
      for(int i=0; i<4; i++)      //process the four bytes
        for(int b=0; b<8; b++)     //one bit at a time
          if((currBytes[i] & pos[b])!=(prevBytes[i] & pos[b])) {  // if the buttonstate has changed
            btnState=currBytes[i] & pos[b];        
            if ((bit2btn[(i*8)+b] >= 31) and (bit2btn[(i*8)+b] <= 34)) {   // hatswitch (Dpad) pressed?
              if (btnState == 0)               // button released?
                //Joystick.setHatSwitch(0, JOYSTICK_HATSWITCH_RELEASE);  // release hatswitch
                bleGamepad.setHat1(HAT_CENTERED);
                else convertHat((bit2btn[(i*8)+b] - 31)); //Joystick.setHatSwitch(0, (bit2btn[(i*8)+b] - 31) * 90);  // direction in 0, 90, 180, 270 degrees up down left right 0,1,2,3
            } else setButton(bit2btn[(i*8)+b], btnState);      // send the update
      
        }

    if ((wheelIdentified) and (wheelID == 3)) {                         // only for R383 wheel
      joyBtnState = (currBytes[3] & pos[0]) && !(currBytes[2] & 0x3c);  // if hatswitch is pressed in the middle
      if (joyBtnState != prevJoyBtnState)
        setButton(13, joyBtnState);                            // press button 14
    }

    bleGamepad.sendReport();
  
    for(int i=0;i<5;i++)
      prevBytes[i] = currBytes[i];   // finally update the just read input to the the previous input for the next cycle

    prevJoyBtnState = joyBtnState;

  }
}

void setButton(int button, bool buttonState) {
  //converts setbutton to press and release functions
  if (buttonState == 0) {
    bleGamepad.release(button);

  } else if (buttonState == 1) {
    bleGamepad.press(button);
  }
}

void convertHat(int hat) {
  //converts 0,1,2,3 to 1,3,5,7 (bleGamepad limit)
  int hatstate;
  
  if (hat == 0) {   //LOL
    hatstate = 1;
  }
  if (hat == 1) {
    hatstate = 3;
  }
  if (hat == 2) {
    hatstate = 5;
  }
  if (hat == 3) {
    hatstate = 7;
  }

  bleGamepad.setHat1(hatstate);
}
