// Nanode to emoncms

#include <JeeLib.h>    // https://github.com/jcw/jeelib
#include <EtherCard.h> // https://github.com/jcw/ethercard/tree/development  dev version with DHCP fixes
#include <NanodeMAC.h> // https://github.com/thiseldo/NanodeMAC

// Fixed RF12 settings
#define MYNODE 35            // node ID 30 reserved for base station
#define freq RF12_433MHZ     // frequency
#define group 210            // network group 

// emoncms settings
#define SERVER "my.server"; // emoncms server
#define EMONCMS    "emoncms" // location of emoncms on server, blank if at root
#define APIKEY  "xxxxxxxx" // API write key 

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

// Set mac address using NanodeMAC
  static uint8_t mymac[6] = { 0,0,0,0,0,0 };
  NanodeMAC mac( mymac );

// Flow control varaiables
  int dataReady=0;                                                  // is set to 1 when there is data ready to be sent
  unsigned long lastRF;                                             // used to check for RF recieve failures
  int post_count;                                                   // used to count number of ethernet posts that dont recieve a reply

void setup () {
  Serial.begin(57600);
  Serial.println("Emonbase:NanodeRF Multiple EmonTX");
  Serial.print("Node: "); Serial.print(MYNODE); 
  Serial.print(" Freq: "); Serial.print("433Mhz"); 
  Serial.print(" Network group: "); Serial.println(group);
  
  rf12_initialize(MYNODE, freq,group);
  lastRF = millis()-40000;                                        // setting lastRF back 40s is useful as it forces the ethernet code to run straight away
                                                                  // which means we dont have to wait to see if its working

  pinMode(6, OUTPUT); digitalWrite(6,LOW);                       // Nanode indicator LED setup, HIGH means off! if LED lights up indicates that Etherent and RFM12 has been initialize

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
  digitalWrite(6,HIGH);    //turn inidicator LED off! yes off! input gets inverted by buffer

//########################################################################################################################
// On data receieved from rf12
//########################################################################################################################

  if (rf12_recvDone() && rf12_crc == 0 && (rf12_hdr & RF12_HDR_CTL) == 0) 
  {
    
    int emontx_nodeID = rf12_hdr & 0x1F;   //extract node ID from received packet
    digitalWrite(6,LOW);                                         // Flash LED on recieve ON
    emontx=*(Payload*) rf12_data;                                 // Get the payload
    
    // JSON creation: JSON sent are of the format: {key1:value1,key2:value2} and so on
    str.reset();                                                  // Reset json string     

    str.print("{node");    
    str.print(emontx_nodeID);                                     // RF recieved so no failure
    str.print("_rx:");
    str.print(emontx.rx1);                                        // Add CT 1 reading 
    
    
    str.print(",node");   
    str.print(emontx_nodeID);                                     // RF recieved so no failure
    str.print("_v:");
    str.print(emontx.supplyV);                                    // Add tx battery voltage reading

    str.print("}\0");

    dataReady = 1;                                                // Ok, data is ready
    lastRF = millis();                                            // reset lastRF timer
    digitalWrite(6,HIGH);                                         // Flash LED on recieve OFF
    Serial.println("Data received");

  }
  

//########################################################################################################################
// Send the data
//########################################################################################################################

  ether.packetLoop(ether.packetReceive());
  
  if (dataReady==1) {                      // If data is ready: send data
    Serial.print("Sending to emoncms: ");
    Serial.println(str.buf);

    Stash::prepare(PSTR("GET http://$F/$F/api/post?apikey=$F&json=$S HTTP/1.0" "\r\n"
                        "Host: $F" "\r\n"
                        "User-Agent: NanodeRF" "\r\n"
                        "\r\n"),
            website, PSTR(EMONCMS), PSTR(APIKEY), str.buf, website);

    ether.tcpSend();     // send the packet - this also releases all stash buffers once done
    Serial.println("Sent");
    dataReady = 0;
  }
}

