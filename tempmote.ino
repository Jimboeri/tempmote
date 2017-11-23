// Sketch to send temp readings to a gateway
// Copyright Jim West (2017)

#include <RFM69.h>    //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>//get it here: https://www.github.com/lowpowerlab/rfm69
#include <EEPROM.h>   // used to store parameters, in specific the time interval between reading transmissions
#include <LowPower.h> // used to power down/sleep the unit to save power
#include <radio_struct.h> // library to hold the radio packet structure

#include <OneWire.h>
#include <DallasTemperature.h>

//*********************************************************************************************
//************ MOTEINO specific settings
//*********************************************************************************************
#define NODEID        9    //must be unique for each node on same network (range up to 254, 255 is used for broadcast)
#define NETWORKID     100  //the same on all nodes that talk to each other (range up to 255)
#define GATEWAYID     1
#define MOTEINO       1
#define SW_MODEL      11
#define SW_VERSION    1.2

#define INITIAL_SETUP // uncomment this for an initial setup of a moteino

//Match frequency to the hardware version of the radio on your Moteino (uncomment one):
#define FREQUENCY   RF69_433MHZ
//#define FREQUENCY   RF69_868MHZ
//#define FREQUENCY   RF69_915MHZ
#define ENCRYPTKEY    "TheWildWestHouse" //exactly the same 16 characters/bytes on all nodes!
//#define IS_RFM69HW    //uncomment only for RFM69HW! Leave out if you have RFM69W!
//#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL

#define LED_DEVICE      3
#define DS18B20_DEVICE  21
#define ONE_WIRE_BUS    4

#define LED           9 // Moteinos have LEDs on D9
#define FLASH_SS      8 // and FLASH SS on D8

//**********************************************************************************************
// Radio transmission specific settings
// ******************************************************************************************
radioPayload3 theData, sendData;

#ifdef ENABLE_ATC
RFM69_ATC radio;
#else
RFM69 radio;
#endif

byte radio_network, radio_node;
byte radio_gateway;
char buff[20];
byte sendSize = 0;
boolean requestACK = false;
char radio_encrypt[16];

//*********************************************************************************************
// Serial channel settings
//*********************************************************************************************
#define SERIAL_BAUD   115200

//**********************************************************************************************
//*** Definitions for Sensors
//**********************************************************************************************

// device 101 is temperature stored as DHT_DEVICE
unsigned long report_period = 15000;   //send data every X milliseconds
unsigned long report_period_time;      //seconds since last period
unsigned long t1 = 0L;

// EEPROM Parameter offsets
#define PARAM_REPORT_PERIOD 1

#define RADIO_NETWORK 101
#define RADIO_NODE 102
#define RADIO_GATEWAY 103
#define RADIO_ENCRYPT 105

//***********************************************************

//************************************************************
// LED is device 3
// ***********************************************************
int ledStatus = 0;    // initially off

//************************************************************
// DS18 is device 21
// ***********************************************************
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
/********************************************************************/

//**********************************
// General declarations
//**********************************
unsigned long timepassed = 0L;
String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
unsigned long requestID;
long lastPeriod = 0;
unsigned long sleepTimer = 0L;
int downTimer = 0;
int counter = 0;

//*************************************************************
//***  SETUP Section
//*************************************************************
void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.println("Sketch name = tempmote");
  Serial.print("Software model no : ");
  Serial.println(SW_MODEL);
  Serial.print("Software version no : ");
  Serial.println(SW_VERSION);

  //************************************************************
  // set up the radio
  //************************************************************

#ifdef INITIAL_SETUP
  EEPROM.put(RADIO_NETWORK, NETWORKID);        // temp to set up network
  EEPROM.put(RADIO_NODE, NODEID);              // temp to set up node
  EEPROM.put(RADIO_GATEWAY, GATEWAYID);        // temp to set up gateway
  EEPROM.put(RADIO_ENCRYPT, ENCRYPTKEY);       // temp to set up encryption key
  EEPROM.put(PARAM_REPORT_PERIOD, report_period); // temp to set up interval
#endif
  EEPROM.get(RADIO_NETWORK, radio_network);      // get network
  EEPROM.get(RADIO_NODE, radio_node);            // get node
  EEPROM.get(RADIO_GATEWAY, radio_gateway);      // get gateway
  EEPROM.get(RADIO_ENCRYPT, radio_encrypt);      // get encryption key

  Serial.print("Radio network = ");
  Serial.println(radio_network);
  Serial.print("Radio node = ");
  Serial.println(radio_node);
  Serial.print("Radio gateway = ");
  Serial.println(radio_gateway);
  Serial.print("Node number is ");
  Serial.println(NODEID);

  //Serial.print("Radio password = ");
  char y;
  for (int x = 0; x < 16; x++) {
    y = EEPROM.read(RADIO_ENCRYPT + x);
    //Serial.print(y);
    radio_encrypt[x] = y;
  }

  radio.initialize(FREQUENCY, NODEID, NETWORKID);
  Serial.print("Radio frequency = ");
  Serial.println(FREQUENCY);
#ifdef IS_RFM69HW
  radio.setHighPower(); //uncomment only for RFM69HW!
#endif
  radio.encrypt(ENCRYPTKEY);

  //Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
  //For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
  //For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
  //Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
#ifdef ENABLE_ATC
  Serial.println("RFM69_ATC Enabled (Auto Transmission Control)\n");
  radio.enableAutoPower(-40);
#endif

  char buff[50];
  sprintf(buff, "Transmitting at %d Mhz...", FREQUENCY == RF69_433MHZ ? 433 : FREQUENCY == RF69_868MHZ ? 868 : 915);
  Serial.println(buff);

  sendData.nodeID = NODEID;    // This node id should be the same for all devices

  //***************
  //**Setup for Report period
  //****************
  // dev4 is temperature_F/humidity
  report_period_time = millis();  //seconds since last period

  EEPROM.get(PARAM_REPORT_PERIOD, t1);      // start delay parameter
  if (t1 < 4000000000)                  // a realistic delay interval
    report_period = t1;
  else {
    EEPROM.put(PARAM_REPORT_PERIOD, report_period); // should only happen on initial setup
    Serial.print("Initial setup of ");
  }
  Serial.print("Delay between reports is ");
  Serial.print(report_period);
  Serial.println(" millseconds");
  Serial.println(' ');

  //pinMode(LED, OUTPUT);

  // Initialise DS18B20
  sensors.begin();
  Serial.print("Devices found - ");
  Serial.println(sensors.getDeviceCount());
  Serial.println("Temp sensor initialised");
}
//************************************************
// End of Setup
//************************************************


void loop() {

  //check for any received radio packets
  if (radio.receiveDone())
  {
    Serial.println("Radio packet received");
    process_radio();
  }

  if (sleepTimer < millis())
  {

    downTimer = (report_period / 1000) - 5;
    Serial.print("Going to sleep for ");
    Serial.print(downTimer);
    Serial.println(" seconds");
    delay(100);
    radio.sleep();

    counter = 0;
    while (counter < downTimer)
    {
      LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);
      counter += 1;
    }

    sleepTimer = millis() + 5000;
    Serial.println("Wake up");
    send_temp();
  }

}


// **********************************************************************************************
void process_radio()
{
  Serial.print('['); Serial.print(radio.SENDERID, DEC); Serial.print("] ");
  // for (byte i = 0; i < radio.DATALEN; i++)
  //   Serial.print((char)radio.DATA[i]);
  Serial.print("   [RX_RSSI:"); Serial.print(radio.RSSI); Serial.print("]");

  if (radio.ACKRequested())
  {
    radio.sendACK();
    Serial.print(" - ACK sent");
  }

  Serial.println();
  Serial.println("Data received");
  theData = *(radioPayload3*)radio.DATA;
  //printThedata(theData);
  requestID = theData.req_ID;

  sendData.nodeID = 1;    // always send to the gateway node

  if (theData.nodeID = NODEID)  // only if the message is for this node
  {
    switch (theData.action) {
      case 'P':   // parameter update
        if (theData.deviceID = MOTEINO)  // MOTEINO code
        {
          Serial.print('Parameter update request ');
          Serial.print(theData.float1);
          Serial.println(' secs');
          txData(MOTEINO, 'C', theData.float1, 0, 0, 0, 0);
          Serial.print("Temp regular update changed to ");
          Serial.print(theData.float1);
          Serial.println(" seconds");
          EEPROM.put(PARAM_REPORT_PERIOD, report_period);
        }
        break;
      case 'Q':   // parameter query
        if (theData.deviceID = MOTEINO)    // DHT22
        {
          txData(MOTEINO, 'C', report_period / 1000, 0, 0, 0, 0);

          Serial.print("Query - report period is ");
          Serial.print(sendData.float1);
          Serial.println(" seconds");
        }
        break;
      case 'R':    // information request
        //send_temp();
        break;

      case 'A':
        if (theData.deviceID = LED_DEVICE)
        {
          Serial.print("LED updated to ");
          if (ledStatus) {
            ledStatus = 0;
            digitalWrite(LED, LOW);
            Serial.println("OFF");
          }
          else
          {
            ledStatus = 1;
            digitalWrite(LED, HIGH);
            Serial.println("ON");
          }
          txData(LED_DEVICE, 'C', ledStatus, 0, 0, 0, 0);
        }

        break;
      case 'S':   // Status request
        txData(MOTEINO, 'C', readVcc(), radio.RSSI, 0, 0, 0);

        break;
    }   // end swicth case for action
  }   // end if when checking for NODEID
}

// ***********************************************************************************************
void send_temp()
{
  sensors.requestTemperatures(); // Tell the DS18B20 to get make a measurement
  Serial.println(sensors.getTempCByIndex(0), 4); // Get that temperature and print it.
  Serial.println();

  float t = sensors.getTempCByIndex(0);

  txData(DS18B20_DEVICE, 'I', t, 0, 0, 0, 0);

}

// ******************************************************************************************
long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  ADMUX = _BV(MUX5) | _BV(MUX0);
#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  ADMUX = _BV(MUX3) | _BV(MUX2);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA, ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
  uint8_t high = ADCH; // unlocks both

  long result = (high << 8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts


}
// *************************************************************************************************
void txData(int deviceID, char action, float data1, float data2, float data3, float data4, int result)
{
  sendData.nodeID     = NODEID;
  sendData.sw_model   = SW_MODEL;
  sendData.sw_version = SW_VERSION;
  sendData.instance   = 1;
  sendData.deviceID   = deviceID;
  sendData.req_ID     = millis();
  sendData.action     = action;
  sendData.result     = result;
  sendData.float1     = data1;
  sendData.float2     = data2;
  sendData.float3     = data3;
  sendData.float4     = data4;

  radio.sendWithRetry(GATEWAYID, (const void*)(&sendData), sizeof(sendData));
  digitalWrite(LED, HIGH);
  Serial.println("message sent");
  delay(500);
  digitalWrite(LED, LOW);
}

