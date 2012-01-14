//------------------------------------------------------------------------------------------------------------------------
// Nanode to emoncms
// By Nathan Chantrell. http://zorg.org/
// Receives date from multiple emonTX and temperature sensor modules and uploads to an emoncms server
// Based on emonbase multiple emontx example for ethershield by Trystan Lea and Glyn Hudson at OpenEnergyMonitor.org
// Licenced under GNU GPL V3
//------------------------------------------------------------------------------------------------------------------------

#include <JeeLib.h>    // https://github.com/jcw/jeelib
#include <EtherCard.h> // https://github.com/jcw/ethercard/tree/development  dev version with DHCP fixes
#include <NanodeMAC.h> // https://github.com/thiseldo/NanodeMAC

// Fixed RF12 settings
#define MYNODE 30            // node ID 30 reserved for base station
#define freq RF12_433MHZ     // frequency
#define group 210            // network group 

// emoncms settings
#define SERVER  "tardis.chantrell.net";            // emoncms server
#define EMONCMS "emoncms"                          // location of emoncms on server, blank if at root
#define APIKEY  "b872449aa3ba74458383a798b740a378" // API write key 

// #define DEBUG 

//########################################################################################################################
//Data Structure to be received 
//########################################################################################################################
typedef struct
{
  int rx1;		    // received value
  int supplyV;              // emontx voltage
} Payload;
Payload emontx; 

//########################################################################################################################
//Data Structure to be sent
//########################################################################################################################
typedef struct
{
  int hour, mins, sec;  // time
} PayloadOut;
PayloadOut nanode; 

//########################################################################################################################
// The PacketBuffer class is used to generate the json string that is send via ethernet - JeeLabs
//########################################################################################################################
class PacketBuffer : public Print {
public:
    PacketBuffer () : fill (0) {}
    const char* buffer() { return buf; }
    byte length() { return fill; }
    void reset() { fill = 0; }
    virtual size_t write(uint8_t ch)
        { if (fill < sizeof buf) buf[fill++] = ch; }  
    byte fill;
    char buf[150];
    private:
};
PacketBuffer str;

//########################################################################################################################

  char website[] PROGMEM = SERVER;
  byte Ethernet::buffer[700];
  uint32_t timer;
  Stash stash;
  char line_buf[50];                       // Used to store line of http reply header

// Set mac address using NanodeMAC
  static uint8_t mymac[6] = { 0,0,0,0,0,0 };
  NanodeMAC mac( mymac );

// Flow control varaiables
  int dataReady=0;                         // is set to 1 when there is data ready to be sent
  unsigned long lastRF;                    // used to check for RF recieve failures
  int post_count;                          // used to count number of ethernet posts that dont recieve a reply

void setup () {
  
  #ifdef DEBUG
    Serial.begin(57600);
    Serial.println("NanodeRF Multiple TX to emoncms");
    Serial.print("Node: "); Serial.print(MYNODE); 
    Serial.print(" Freq: "); Serial.print("433Mhz"); 
    Serial.print(" Network group: "); Serial.println(group);
  #endif
  
  rf12_initialize(MYNODE, freq,group);
  lastRF = millis()-40000;                  // setting lastRF back 40s is useful as it forces the ethernet code to run straight away
                                            // which means we dont have to wait to see if its working

  pinMode(6, OUTPUT); digitalWrite(6,LOW);  // Nanode indicator LED setup, HIGH means off! if LED lights up indicates that Etherent and RFM12 has been initialize

  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0) 
    Serial.println( "Failed to access Ethernet controller");
  if (!ether.dhcpSetup())
    Serial.println("DHCP failed");
    
  ether.printIp("IP:  ", ether.myip);
  ether.printIp("GW:  ", ether.gwip);  
  ether.printIp("DNS: ", ether.dnsip);  

  if (!ether.dnsLookup(website))
    Serial.println("DNS failed");
    
  ether.printIp("SRV: ", ether.hisip);
}

  void loop () {
  digitalWrite(6,HIGH);    // turn LED off
  
//########################################################################################################################
// On data receieved from rf12
//########################################################################################################################

  if (rf12_recvDone() && rf12_crc == 0 && (rf12_hdr & RF12_HDR_CTL) == 0) 
  {
    
   int emontx_nodeID = rf12_hdr & 0x1F;   // extract node ID from received packet
   digitalWrite(6,LOW);                   // Turn LED on
   emontx=*(Payload*) rf12_data;          // Get the payload
   
   // JSON creation: format: {key1:value1,key2:value2} and so on
    
   str.reset();                           // Reset json string     
   str.print("{rf_fail:0,");              // RF recieved so no failure
   
   str.print("node");    
   str.print(emontx_nodeID);              // Add node ID
   str.print("_rx:");
   str.print(emontx.rx1);                 // Add reading 
   
   
   str.print(",node");   
   str.print(emontx_nodeID);              // Add node ID
   str.print("_v:");
   str.print(emontx.supplyV);             // Add tx battery voltage reading

   str.print("}\0");

   dataReady = 1;                         // Ok, data is ready
   lastRF = millis();                     // reset lastRF timer
   digitalWrite(6,HIGH);                  // Turn LED OFF

   #ifdef DEBUG
    Serial.println("Data received");
   #endif
  }

  // If no data is recieved from rf12 module the server is updated every 30s with RFfail = 1 indicator for debugging
  if ((millis()-lastRF)>30000)
  {
    lastRF = millis();                      // reset lastRF timer
    str.reset();                            // reset json string
    str.print("{rf_fail:1}\0");             // No RF received in 30 seconds so send failure 
    dataReady = 1;                          // Ok, data is ready
  }
  
//########################################################################################################################
// Send the data
//########################################################################################################################

  ether.packetLoop(ether.packetReceive());
  
  if (dataReady==1) {                      // If data is ready: send data

   #ifdef DEBUG
    Serial.print("Sending to emoncms: ");
    Serial.println(str.buf);
   #endif

   ether.browseUrl(PSTR("/"EMONCMS"/api/post?apikey="APIKEY"&json="), str.buf, website, my_callback);

   dataReady = 0;
  }

}

//########################################################################################################################
// Ethernet callback recieve reply and decode (OpenEnergyMonitor)
//########################################################################################################################

static void my_callback (byte status, word off, word len) {
  
  get_header_line(2,off);      // Get the date and time from the header
  
  #ifdef DEBUG  
  Serial.print("Date and Time: ");    // Print out the date and time
  Serial.println(line_buf);    // Print out the date and time
  #endif
  
  // Decode date time string to get integers for hour, min, sec, day
  // We just search for the characters and hope they are in the right place
  char val[1];
  val[0] = line_buf[23]; val[1] = line_buf[24];
  int hour = atoi(val);
  val[0] = line_buf[26]; val[1] = line_buf[27];
  int mins = atoi(val);
  val[0] = line_buf[29]; val[1] = line_buf[30];
  int sec = atoi(val);
  val[0] = line_buf[11]; val[1] = line_buf[12];
  int day = atoi(val);

  nanode.hour = hour;
  nanode.mins = mins;
  nanode.sec = sec;
  
  delay(100);
  
  // Send time data
  int i = 0; while (!rf12_canSend() && i<10) {rf12_recvDone(); i++;}    // if can send - exit if it gets stuck, as it seems too
  rf12_sendStart(0, &nanode, sizeof nanode);                        // send payload
  rf12_sendWait(0);

  #ifdef DEBUG  
  Serial.println("time sent");
  #endif
}

// decode reply
int get_header_line(int line,word off)
{
  memset(line_buf,NULL,sizeof(line_buf));
  if (off != 0)
  {
    uint16_t pos = off;
    int line_num = 0;
    int line_pos = 0;
    
    while (Ethernet::buffer[pos])
    {
      if (Ethernet::buffer[pos]=='\n')
      {
        line_num++; line_buf[line_pos] = '\0';
        line_pos = 0;
        if (line_num == line) return 1;
      }
      else
      {
        if (line_pos<49) {line_buf[line_pos] = Ethernet::buffer[pos]; line_pos++;}
      }  
      pos++; 
    } 
  }
  return 0;
}


