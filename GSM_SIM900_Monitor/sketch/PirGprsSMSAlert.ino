// GPRS Motion Monitor
// v1.1  2016.02.21
// WA4EFH
//
// For Arduino UNO and SIM900 GPS Shield
//
// EXAMPLE OPERATION
// This program detects movement and collects the sensor event counts, totaling for each hour for 24 hours and then sends an 
// SMS message with the last few hours of data. It then resets the counts and starts again. 
// It also sends an initial SMS message when the Arduino is reset or powered up to verify it is functioning correctly.
//
// Known bug: SIM900 appears to not send long SMS messages. 
// Workaround:  Edit c:\Program Files (x86)\Arduino\hardware\arduino\avr\libraries\SoftwareSerial\SoftwareSerial.h should be increased:
// #define _SS_MAX_RX_BUFF 128 // RX buffer size //BEFORE WAS 64
// This will allow larger SMS messages to be sent, though short messages are suggested!


#include <SoftwareSerial.h>
#include <String.h>
#include <Time.h>  
 
SoftwareSerial myGPRS(7, 8);  // GPIO pins used by SeeedStudio GSM

#define DEBUG     // Comment out to turn off debugging (if desired) to Serial Monitor

// ---- USER MODIFY ---- 
String MYPHONENUM = "+19045551212";  // Replace with your phone# to send texts to. SMS needs country code
// Which hour to send message 
int timeToSend = 8;    // Sends SMS msg at 08:00 AM

// PIR & LED Pins
int pirPin = 4;     //the digital pin connected to the PIR sensor's output
int ledPin = 13;    // built-in LED (or external LED, optional)
int calibrationTime = 10;   // Delay X seconds for PIR
int pirState = LOW; // Start as no motion  
      
boolean msgSent = false;  // Flag to verify msg sent (future)
boolean savePower = true;    // Conserve power by turning off SIM900 between transmissions  
boolean UseSMS= true;  // Send via SMS Text  (future to allow HTTP alternative)
   
int recNumber = 0;    // Current array number being updated 0-23hr
String message;   // Data to be sent via SMS (or HTTP future) 
  
// Set GPRS TIME manually one-time using SerialRelay.ino, not this program.
// If AT+CCLK? does not return correct time:
// AT+CLTS=1
// AT+CCLK="16/02/14,15:45:00+05"  (GMT time + offset to local TZ)


typedef struct        // Count of motion detections by hour (actually 0 - 23)
{
  int month;
  int day;
  int hour;
  int count;
} rec_type;

rec_type motionarray[24];

// DATE/TIME  [+CCLK: "00/01/16,02:31:58+00"]
#define CCLK_MSG_LEN 28  // total characters in the CCLK message
#define TIME_STR_LEN 18  // the actual number of characters for date and time
char timePreamble[] = "CCLK: \"" ;
char timeStamp[TIME_STR_LEN]; // holds the time string

// DECLARE FUNCTIONS - Add here if you get "function not found" errors during compiling
void setup();
void loop();
boolean getTimeStamp();
boolean readTime();
void powerUpOrDown();
void blink();
void clearArray();
int total();
void checkRange();
void ShowSerialData();
void digitalClockDisplay();
void printDigits(int digits);
boolean sendTextMessage();


// INITIALIZATION STARTS HERE
void setup()
{
  myGPRS.begin(19200);      // the GPRS baud rate   
  Serial.begin(19200);      // the Serial Terminal baud rate 
  delay(500);
  Serial.println("Starting");
  powerUpOrDown(1);  // POWER UP 
  delay(5000);
  ShowSerialData();
  
  // LED 
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
   
  myGPRS.println("ATE0");  // Turn off Echo
  delay(100);
  ShowSerialData();

  // SET UP 24HR ARRAY
  clearArray();

  // INITIALIZE DATE/TIME 
  while (getTimeStamp() == false);

  // Turn on power for PIR sensor
  pinMode(pirPin, INPUT);
  digitalWrite(pirPin, LOW);
  //give the sensor some time to calibrate
  for(int i = 0; i < calibrationTime; i++){
      delay(1000);
  }

  // SEND OK MESSAGE
  message = "SIM900 Initialized";
  if (UseSMS){ sendTextMessage(); }
  
  // FINISHED SETUP
  Serial.println("setup() completed");
  myGPRS.flush();
  Serial.flush(); 
  if (savePower)
  {
    powerUpOrDown(0);  // Power Down GPRS module
  }
  blink(4);
}

//------------------------
// MAIN LOOP
void loop()
{
  // CHECK FOR MOVEMENT
  // wait for movement - PIR out will turn HIGH
  int val = digitalRead(pirPin);    // read input value
  if (val == HIGH) {            // check if the input is HIGH
    digitalWrite(ledPin, HIGH);  // turn LED ON
    if (pirState == LOW) {
      // we have just turned on
#ifdef DEBUG
        Serial.print("Motion detected: ");
        digitalClockDisplay();
#endif
      // INCREMENT ARRAY[HOUR]
      recNumber = hour() - timeToSend;    // array[0] should start at timeToSend
      if (recNumber < 0) { recNumber = 23 + recNumber; }  // wrap array range between 0 to 23
      checkRange();  // Be sure recNumber is within 0-23
      int hr = recNumber;
      motionarray[hr].month = month();
      motionarray[hr].day = day();
      motionarray[hr].hour = hour();
      motionarray[hr].count++;   // increment hit counter
      // We only want to log on the output change, not state
      pirState = HIGH;
#ifdef DEBUG
        Serial.print("rec=");
        Serial.print(hr);
        Serial.print(" count=");
        Serial.println(motionarray[hr].count);
#endif
    }
  } else {
    digitalWrite(ledPin, LOW); // turn LED OFF
    if (pirState == HIGH){
      // we have just turned of
#ifdef DEBUG
      Serial.println("Motion ended!");
#endif
      // We only want to print on the output change, not state
      pirState = LOW;
    }
  }

  // IS IT TIME TO SEND MESSAGE?
  if ((hour() == timeToSend) && (msgSent == false))
  {
#ifdef DEBUG
        Serial.print("Time sending: ");
        digitalClockDisplay();
#endif
  	  if (savePower)
	    {
	    powerUpOrDown(1);  // Power Up GPRS module
      }

      // BUILD MESSAGE 
      String spacer = "<BR>";
      if (UseSMS) { spacer = "\n"; }
      
      message = "Activity seen ";
      message += total();
      message += " times";
      message += spacer;

      if (UseSMS) // SMS limited to 64 char w/o mods!
      {
        for (int i = 20; i < 24; i++) // last 4 hrs is < 64 char. Change to i = 12 for 128 char mod.
        {
          if (motionarray[i].count > 0)
          {
            message += motionarray[i].month;
            message += "/";
            message += motionarray[i].day;
            message += " ";
            message += motionarray[i].hour;
            message += ":00- ";
            message += motionarray[i].count;
            message += spacer;
          }
        }
      } // FINISHED BUILDING MESSAGE

#ifdef DEBUG
      Serial.println(message); 
#endif
      // SEND MESSAGE
        if (UseSMS)
        {
          msgSent = sendTextMessage();
        }
      
      msgSent = true;  // FORCE TO ALWAYS ASSUME MESSAGE SENT OK (remove this in future)

      if (msgSent)
      {
        // CLEAR ARRAY
        clearArray();
      }
	    // DELAY TO KEEP FROM FLOODING GPRS
	    delay(10000);

      // FINISHED USING RADIO, TURN OFF 
      if (savePower)
      {
        powerUpOrDown(0);
      }
  } // End of Time to Send Message
  
  // Wait an hour before reseting the Msg Sent flag
  if (hour() == (timeToSend + 1))  // Known bug, if timeToSend = 23, this will fail! (But, I didn't want a text alert at 11pm anyway! :-)
  {
    msgSent = false;
  }
  
  // SHORT PAUSE BETWEEN MOTION CHECKS
  delay(500);
}
//------------------------


// METHODS BELOW
boolean getTimeStamp(){
#ifdef DEBUG
  Serial.println("getTimeStamp()"); 
#endif
 myGPRS.flush();
 delay(100);
 myGPRS.println("AT+CCLK?");      //SIM900 AT command to get time stamp
 delay(1000);
 if (readTime() == true){ 
  int yr = (((timeStamp[0])-48)*10)+((timeStamp[1])-48);
  int month = (((timeStamp[3])-48)*10)+((timeStamp[4])-48);
  int day  = (((timeStamp[6])-48)*10)+((timeStamp[7])-48);
  int hr = (((timeStamp[9])-48)*10)+((timeStamp[10])-48);
  int min = (((timeStamp[12])-48)*10)+((timeStamp[13])-48);
  int sec = (((timeStamp[15])-48)*10)+((timeStamp[16])-48);
 
#ifdef DEBUG
	  Serial.print("DATE/TIME: ");
	  Serial.print("Year:");
	  Serial.println(yr, DEC);
	  Serial.print("Month:");
	  Serial.println(month, DEC);
	  Serial.print("Hour:");
	  Serial.println(hr, DEC);
	  Serial.print("Min:");
	  Serial.println(min, DEC);
	  Serial.print("Sec:");
	  Serial.println(sec, DEC);
#endif
  // SET ARDUINO SOFTWARE CLOCK
  setTime(hr,min,sec,day,month,yr);  
 }
 else {
   Serial.println("Cannot get current time!");
   myGPRS.flush();
   delay(1000);
   return false;
 }
 myGPRS.flush(); 
 delay(1000);
 return true;
}

boolean readTime(){  // this was from another example, not bullet proof!
#ifdef DEBUG
  Serial.println("readTime(): "); 
#endif
 // Check for available bytes
 if(myGPRS.available() < CCLK_MSG_LEN )
    return false;  // Not enough characters for a full message.  Try again later
 char ch = myGPRS.read(); // skip first two chars
 ch = myGPRS.read();
 if(myGPRS.read() != '+')
    return false;  // Not in sync yet
 // In sync.  Check the preamble
 for(int i=0; i < strlen(timePreamble); i++) {
     if(myGPRS.read() != timePreamble[i] )
         return false; // exit if the received data does not match the preamble 
 }
 // Preamble checked.  We are now at the start of the time data
 for(int i=0; i < TIME_STR_LEN; i++)
       timeStamp[i] = myGPRS.read();
#ifdef DEBUG
  Serial.println(timeStamp); 
#endif
 myGPRS.flush();
 return true; // we have filled the timeString with valid data 
}



// SIM900 Power is same pushbutton for on & off.
// 0 = turn off 1 = turn on
void powerUpOrDown(int pwr)  
{
#ifdef DEBUG
  Serial.println("powerUpOrDown()"); 
#endif
  //CHECK STATE OF GSM
  int state = 0;
  myGPRS.flush(); // make sure nothing is in buffer
  delay(1000);
  myGPRS.println("AT");
  delay(500);
  if (myGPRS.available() > 0) { state = 1; }  
  ShowSerialData();
  myGPRS.flush(); // get rid of anything left
  // Do nothing if already in expected mode
  if (((pwr == 1) && (state == 1)) || ((pwr == 0) && (state == 0)))
  {
    return;
  }
#ifdef DEBUG
  Serial.println("Flipping...");
#endif
  // Ok to flip the switch  
  pinMode(9, OUTPUT); 
  digitalWrite(9,LOW);
  delay(1000);
  digitalWrite(9,HIGH);
  delay(2000);
  digitalWrite(9,LOW);
  delay(10000);  // give time for radio to sync up
}

// Blink the LED
void blink(int num) 
{
  for (int i = 0; i < num; i++)
  {
    digitalWrite(ledPin, LOW);
    delay(200); 
    digitalWrite(ledPin, HIGH);
    delay(200);
  }
}

 
void clearArray()
{
  for (int i = 0; i < 24; i++)
  {
    motionarray[i] = (rec_type) {0,0,0,0};
  }
}

int total()
{
  int cnt = 0;
  for (int i = 0; i < 24; i++)
  {
    cnt += motionarray[i].count;
  }
  return(cnt);
}

void checkRange()
{
        if ((recNumber < 0) || (recNumber > 23))
      {
         Serial.println("ERROR. timeToSend not between 0 - 22");
         Serial.print("timeToSend:");
         Serial.println(timeToSend);
         recNumber = 0; // Just set to 0 for safety
      }
}

void ShowSerialData()
{
#ifdef DEBUG
    while(myGPRS.available()!=0) 
      Serial.write(myGPRS.read());
#endif
  myGPRS.flush();
  delay(100);
}

void digitalClockDisplay(){
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year()); 
  Serial.println(); 
}

void printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

//SEND SMS MESSAGE
//this function is to send a sms message
boolean sendTextMessage()
{
  boolean msgsent = false;  // future use to verify success/fail
#ifdef DEBUG
  Serial.println("sendTextMessage()"); 
#endif
  myGPRS.print("AT+CMGF=1\r");    //Because we want to send the SMS in text mode
  delay(100);
  myGPRS.print("AT+CMGS=\"");
  myGPRS.print(MYPHONENUM);
  myGPRS.println("\""); 
  delay(100);
  myGPRS.println(message);//the content of the message
  delay(100);
  myGPRS.println((char)26);//the ASCII code of the ctrl+z is 26
  delay(100);
  myGPRS.println();
  // TODO Add ERROR CHECKING for SMS (if needed?)
  // if (reply.substring(0) == "OK") { msgsent = true;)
  msgsent = true;
#ifdef DEBUG
  Serial.println("message sent"); 
#endif
  myGPRS.flush();
  delay(2000);
  return(msgsent);
}

