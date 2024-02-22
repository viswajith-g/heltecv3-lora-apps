#include "HT_SSD1306Wire.h"
#include "LoRaWan_APP.h"
#include "Wire.h"
#include <Arduino.h>
// #include <esp_timer.h>

// How long to sleep after a packet has been transmitted before
// sending the next packet.
const uint32_t SLEEP_TIME_BETWEEN_EVENTS_MS = 60000;

/*esp timer to toggle apps*/
// const uint64_t TIMER_PERIOD_US = 60000000;

// Use Over the Air Activation for joining the LoRaWAN network.
const bool LORA_OTAA = true;

//common keys
uint8_t devEui[] = {0xA8, 0x61, 0x0A, 0x31, 0x35, 0x43, 0x70, 0x19};
// bool overTheAirActivation = true;
uint8_t joinEui[8] = {0};  // you should set whatever your TTN generates. TTN calls this the joinEui, they are the same thing. 
uint8_t appKey[16] = {0};  // you should set whatever your TTN generates 

//These are only used for ABP, for OTAA, these values are generated on the Nwk Server, you should not have to change these values
uint8_t nwkSKey[16] = {0};
uint8_t appSKey[16] = {0};
uint32_t devAddr =  0; 

//App1 Keys
RTC_DATA_ATTR bool overTheAirActivation = true;
uint8_t joinEui1[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};  // you should set whatever your TTN generates. TTN calls this the joinEui, they are the same thing. 
uint8_t appKey1[] = {0x91, 0x4C, 0x40, 0x4E, 0x79, 0x78, 0x65, 0x3E, 0x86, 0xE8, 0xFA, 0xAC, 0xCA, 0x72, 0x85, 0x12};  // you should set whatever your TTN generates 
//These are only used for ABP, for OTAA, these values are generated on the Nwk Server, you should not have to change these values
uint8_t nwkSKey1[16] = {0};
uint8_t appSKey1[16] = {0};
uint32_t devAddr1 =  (uint32_t)0; 


//App2 Keys 
uint8_t joinEui2[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};  // you should set whatever your TTN generates. TTN calls this the joinEui, they are the same thing. 
uint8_t appKey2[] = {0x07, 0x55, 0x46, 0xBD, 0xA8, 0xC6, 0x01, 0xD3, 0xBD, 0xB7, 0x5E, 0x0E, 0x19, 0xFD, 0x1C, 0x0E};  // you should set whatever your TTN generates 
//These are only used for ABP, for OTAA, these values are generated on the Nwk Server, you should not have to change these values
uint8_t nwkSKey2[16] = {0};
uint8_t appSKey2[16] = {0};
uint32_t devAddr2 =  (uint32_t)0;  

// LoRaWAN channel mask
uint16_t userChannelsMask[6] = {0xFF00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};

// ADR enable
bool loraWanAdr = true;

// Application port
uint8_t appPort = 1;

/*!
 * Number of trials to transmit the frame, if the LoRaMAC layer did not
 * receive an acknowledgment. The MAC performs a datarate adaptation,
 * according to the LoRaWAN Specification V1.0.2, chapter 18.4, according
 * to the following table:
 *
 * Transmission nb | Data Rate
 * ----------------|-----------
 * 1 (first)       | DR
 * 2               | DR
 * 3               | max(DR-1,0)
 * 4               | max(DR-1,0)
 * 5               | max(DR-2,0)
 * 6               | max(DR-2,0)
 * 7               | max(DR-3,0)
 * 8               | max(DR-3,0)
 *
 * Note, that if NbTrials is set to 1 or 2, the MAC will not decrease
 * the datarate, in case the LoRaMAC layer did not receive an acknowledgment
 */
uint8_t confirmedNbTrials = 8;

// Keep a running counter of how many packets have been sent since reset.
RTC_DATA_ATTR uint16_t count = 0;
// Keep a count of how many packets have been acknowledged since reset.
RTC_DATA_ATTR uint16_t acked_count = 0;
// Number of unacked packets in a row.
RTC_DATA_ATTR uint16_t failed_tx_consecutive = 0;
// Flags to indicate which app should connect and send data
RTC_DATA_ATTR bool app1_flag = true;
RTC_DATA_ATTR bool app2_flag = false;
//Flag to indicate if we should switch apps
// RTC_DATA_ATTR bool switch_app_flag = false;
RTC_DATA_ATTR bool first_run = false;

// Driver for the OLED display.
//
// THIS MUST BE DEFINED LAST. (I don't know why.)
SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);


// void IRAM_ATTR timerISR(void* arg) {
// 	switch_app_flag = true;
// }

///////////////////////////////////////////////////////////////////////////////
// Display Functions
///////////////////////////////////////////////////////////////////////////////

// Display on the OLED that a packet was sent and if it was acked.
void display_tx_done(uint8_t tries, bool acked) {
  char cnt_str[25];
  char ack_str[25];

  snprintf(cnt_str, 25, "TX Count: %i", tries);
  snprintf(ack_str, 25, "Ack: %s", acked?"Yes":"No");

  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(4, 5, "Packet Sent");
  display.setFont(ArialMT_Plain_10);
  display.drawString(4, 20, cnt_str);
  display.drawString(4, 30, ack_str);
  display.display();
  delay(4000);
}

// Display LoRaWAN Join and GPS status.
void display_status(bool lora_joined) {
  char msg[32];
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(4, 10, "Multiapp LoRa Status");
  display.setFont(ArialMT_Plain_10);
  snprintf(msg, 32, "LoRa: %s", (lora_joined?"Joined":"Joining..."));
  display.drawString(4, 25, msg);
  snprintf(msg, 32, "Failed Cnt: %i", failed_tx_consecutive);
  display.drawString(4, 45, msg);
  display.display();
  delay(2000);
}

///////////////////////////////////////////////////////////////////////////////
// TX Packet and GPS Functions
///////////////////////////////////////////////////////////////////////////////

// Read the GPS and create an outgoing packet structure.
void prepareTxFrame(void) {
  // We are going to send 20 bytes.
  appDataSize = 4;

	if (app1_flag){
		appData[0] = 0x00;
		appData[1] = 0x00;
		appData[2] = 0x00;
		appData[3] = 0x01;
	}

	if (app2_flag){
		appData[0] = 0x00;
		appData[1] = 0x00;
		appData[2] = 0x00;
		appData[3] = 0x02;
	}
}

static void send_packet(void) {
  prepareTxFrame();

  // Since we are sending a packet, increment our counter.
  count += 1;

  LoRaWAN.send(true, confirmedNbTrials, appPort);
}

///////////////////////////////////////////////////////////////////////////////
// Callback Functions
///////////////////////////////////////////////////////////////////////////////

// Called when we have joined a LoRaWAN network.
static void joined(void) {
  display_status(true);
  if(first_run){
    send_packet();      // send packet immediately after joining only for the first time.  
    first_run = false;  // From the second time onwards, we rely on the tx cycle timer to send packets
    }
}

// Called after successfully receiving ack for a packet. We switch to the next app and join the network 
static void switch_app(void){
  app1_flag = !app1_flag;
	app2_flag = !app2_flag;

  if (app1_flag){
    memcpy(joinEui, joinEui1, (sizeof(joinEui)/sizeof(joinEui[0])));
		memcpy(appKey, appKey1, (sizeof(appKey)/sizeof(appKey[0])));
    printf("Joining App1\n");
  }
  if (app2_flag){
    memcpy(joinEui, joinEui2, (sizeof(joinEui)/sizeof(joinEui[0])));
		memcpy(appKey, appKey2, (sizeof(appKey)/sizeof(appKey[0])));
    printf("Joining App2\n");
  }

  // we don't do this anymore because we use the force argument in 
  // the .join() method to force a new connection
  
  // extern bool IsLoRaMacNetworkJoined; // enforce new join to TTN
  // IsLoRaMacNetworkJoined = false;

  LoRaWAN.join(LORA_OTAA, true);
}

// Called after a packet has been sent.
static void sent(uint8_t tries, bool acked) {
  printf("Packet Sent. TX count: %i, Acked: %i\r\n", tries, acked);

  // Show status on OLED.
  display_tx_done(tries, acked);

  if (!acked) {
    // Increment our failed counter.
    failed_tx_consecutive += 1;

    // If we have relatively few failures, just try to send again.
    if (failed_tx_consecutive < 5) {
      send_packet();
    } else {
      // If we have many failed packets, try to re-join the network.
      failed_tx_consecutive = 0;
      LoRaWAN.join(LORA_OTAA, true);
    }
  } else {
    // Reset failed counter if needed.
    failed_tx_consecutive = 0;

    // Increment number of packets acked.
    acked_count += 1;

    // if this is not the first time we are running the code, 
    // we want to join the second app after receiving ack
    if (!first_run){
      switch_app();
    }

    // We want to wait for a period of time and then prepare and send another
    // packet. To do so in a low power mode, we use the LoRaWAN `cycle()` function
    // which will reboot the ESP32 chip after the desired amount of time.
    //
    // Note, all state is NOT lost in this operation. State marked `RTC_DATA_ATTR`
    // will be preserved, and the LoRaWAN stack uses this extensively. So, when
    // the chip restarts we will still be joined to the LoRaWAN network.
    LoRaWAN.cycle(SLEEP_TIME_BETWEEN_EVENTS_MS);
  }
}

static void acked(void) {
}

void received(McpsIndication_t *mcpsIndication) {
  printf("+REV DATA:%s", mcpsIndication->RxSlot?"RXWIN2":"RXWIN1");
  printf(", RXSIZE %d", mcpsIndication->BufferSize);
  printf(", PORT %d\r\n", mcpsIndication->Port);
}

///////////////////////////////////////////////////////////////////////////////
// SETUP
///////////////////////////////////////////////////////////////////////////////

void setup() {
  // Configure our normal `printf()` UART.
  Serial.begin(115200);

  // Turn off LED.
  pinMode(LED, OUTPUT);
  digitalWrite(LED, 0);

  // Initialize MCU.
  Mcu.begin();
  display.init();

  // set the joinEUI and appKey based on which app we want to connect to
  if (app1_flag){
    memcpy(joinEui, joinEui1, (sizeof(joinEui)/sizeof(joinEui[0])));
		memcpy(appKey, appKey1, (sizeof(appKey)/sizeof(appKey[0])));
  }
  if (app2_flag){
    memcpy(joinEui, joinEui2, (sizeof(joinEui)/sizeof(joinEui[0])));
		memcpy(appKey, appKey2, (sizeof(appKey)/sizeof(appKey[0])));
  }

  first_run = true;

  // Show a title screen.
  if (count == 0) {
    char eui_str[100];
    int eui_str_len = snprintf(eui_str, 100, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
      devEui[0], devEui[1], devEui[2], devEui[3], devEui[4], devEui[5], devEui[6], devEui[7]);
    eui_str[eui_str_len] = '\0';

    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 5, "Starting...");
    display.drawString(64, 40, "Device EUI:");
    display.drawString(64, 50, eui_str);
    display.setFont(ArialMT_Plain_24);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 16, "Multiapp LoRa");
    display.display();
    delay(2000);
  }

  // esp_timer_handle_t timer;
	// esp_timer_create_args_t timer_args = {
	// 	.callback = &timerISR,
	// 	.arg = NULL,
	// 	.dispatch_method = ESP_TIMER_TASK,
	// 	.name = "App Switcher"  //<whatever you want to name your timer>
	// };

	// esp_timer_create(&timer_args, &timer); 
  // 	esp_timer_start_periodic(timer, TIMER_PERIOD_US);

  // Initiate the LoRa process. This will check if the connection
  // is already established, or if we are connecting for the first
  // time (after a full power cycle for example).
  LoRaWAN.init(CLASS_A, LORAWAN_ACTIVE_REGION, loraWanAdr, joined, sent, acked, received);
  if (first_run){
    printf("Joining App1\n");
  }
  LoRaWAN.join(LORA_OTAA, false);
}

///////////////////////////////////////////////////////////////////////////////
// LOOP
///////////////////////////////////////////////////////////////////////////////

void loop() {
  // All operations are event based, so all we do here is
  // let the LoRaWAN stack handle events or go to sleep.
  LoRaWAN.sleep();
}
