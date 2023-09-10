#include "HT_SSD1306Wire.h"
#include "LoRaWan_APP.h"
#include "TinyGPS.h"
#include "Wire.h"
#include <Arduino.h>

// Parser for GPS strings.
TinyGPS gps;
// UART connection to the GPS module.
HardwareSerial gpsSerial(1);

// Pin for receiving data from GPS.
const int GPS_TX_PIN = 45;

// How long to sleep after a packet has been transmitted before
// sending the next packet.
const int SLEEP_TIME_BETWEEN_EVENTS_MS = 15000;

// Use Over the Air Activation for joining the LoRaWAN network.
const bool LORA_OTAA = true;

// For OTAA.
uint8_t devEui[8] = {0};

// These are only used for ABP.
uint8_t nwkSKey[16] = {0};
uint8_t appSKey[16] = {0};
uint32_t devAddr    = 0;

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

// Driver for the OLED display.
//
// THIS MUST BE DEFINED LAST. (I don't know why.)
SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);


///////////////////////////////////////////////////////////////////////////////
// Display Functions
///////////////////////////////////////////////////////////////////////////////

// Display on the OLED the current lat/lon coordinates.
void display_gps(long lat, long lng) {
  char lat_str[25];
  char lng_str[25];
  char cnt_str[25];

  char lat_dir = 'N';
  char lng_dir = 'E';
  if (lat < 0) {
    lat_dir = 'S';
    lat    *= -1;
  }
  if (lng < 0) {
    lng_dir = 'W';
    lng    *= -1;
  }

  long lat_deg     = lat / 1000000;
  long lat_deg_rem = lat - (lat_deg * 1000000);
  long lat_min     = (lat_deg_rem * 60) / 1000000;
  long lat_min_rem = (lat_deg_rem * 60) - (lat_min * 1000000);
  snprintf(lat_str, 25, "%i°%i.%i'%c", lat_deg, lat_min, lat_min_rem, lat_dir);

  long lng_deg     = lng / 1000000;
  long lng_deg_rem = lng - (lng_deg * 1000000);
  long lng_min     = (lng_deg_rem * 60) / 1000000;
  long lng_min_rem = (lng_deg_rem * 60) - (lng_min * 1000000);
  snprintf(lng_str, 25, "%i°%i.%i'%c", lng_deg, lng_min, lng_min_rem, lng_dir);

  snprintf(cnt_str, 25, "Count: %i", count);

  display.init();
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(4, 5, "GPS Location:");
  display.setFont(ArialMT_Plain_10);
  display.drawString(4, 20, lat_str);
  display.drawString(4, 30, lng_str);
  display.drawString(4, 40, cnt_str);
  display.display();
  delay(2000);
}

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

///////////////////////////////////////////////////////////////////////////////
// TX Packet and GPS Functions
///////////////////////////////////////////////////////////////////////////////

// Read the GPS and create an outgoing packet structure.
void prepareTxFrame(void) {
  // We are going to send 11 bytes.
  appDataSize = 11;

  // To read GPS.
  long lat, lng;
  unsigned long age = 9999999;

  // Wait until we have fresh data.
  while (age > 1000) {
    // Read in from the GPS and use the library to parse the message.
    while (gpsSerial.available() > 0) {
      gps.encode(gpsSerial.read());
    }
    // Retrieve the GPS location in millionths of degrees.
    gps.get_position(&lat, &lng, &age);

    Serial.println("Lat: " + String(lat) + ", Lng: " + String(lng) + ", Age: " + String(age));

    // If we don't have a fix wait and retry.
    if (lat == 999999999) {
      delay(1000);
      continue;
    }

    // Create our packet structure.
    appData[0] = (1 << 7); // our GPS

    appData[1] = (lat >> 0) & 0xFF;
    appData[2] = (lat >> 8) & 0xFF;
    appData[3] = (lat >> 16) & 0xFF;
    appData[4] = (lat >> 24) & 0xFF;

    appData[5] = (lng >> 0) & 0xFF;
    appData[6] = (lng >> 8) & 0xFF;
    appData[7] = (lng >> 16) & 0xFF;
    appData[8] = (lng >> 24) & 0xFF;

    appData[9]  = (count >> 0) & 0xFF;
    appData[10] = (count >> 8) & 0xFF;

    for (int i = 0; i < 11; i++) {
      Serial.print(appData[i], HEX);
      Serial.print(" ");
    }
    Serial.println("");

    // Display the GPS data to the OLED.
    display_gps(lat, lng);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Callback Functions
///////////////////////////////////////////////////////////////////////////////

// Called when we have joined a LoRaWAN network.
static void joined(void) {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(4, 20, "Status: Joined");
  display.display();
  delay(2000);

  // Create a new packet to send.
  prepareTxFrame();

  // Since we are sending a packet, increment our counter.
  count += 1;

  LoRaWAN.send(true, confirmedNbTrials, appPort);
}

// Called after a packet has been sent.
static void sent(uint8_t tries, bool acked) {
  printf("Packet Sent. TX count: %i, Acked: %i\r\n", tries, acked);

  // Show status on OLED.
  display_tx_done(tries, acked);

  // We want to wait for a period of time and then prepare and send another
  // packet. To do so in a low power mode, we use the LoRaWAN `cycle()` function
  // which will reboot the ESP32 chip after the desired amount of time.
  //
  // Note, all state is NOT lost in this operation. State marked `RTC_DATA_ATTR`
  // will be preserved, and the LoRaWAN stack uses this extensively. So, when
  // the chip restarts we will still be joined to the LoRaWAN network.
  LoRaWAN.cycle(SLEEP_TIME_BETWEEN_EVENTS_MS);
}

static void acked(void) {
}

void received(McpsIndication_t *mcpsIndication) {
  printf("+REV DATA:%s", mcpsIndication->RxSlot?"RXWIN2":"RXWIN1");
  printf(", RXSIZE %d", mcpsIndication->BufferSize);
  printf(", PORT %d\r\n", mcpsIndication->Port);
//   printf("+REV DATA:");
//   for (uint8_t i = 0; i < mcpsIndication->BufferSize; i++) {
//     printf("%02X", mcpsIndication->Buffer[i]);
//   }
//   printf("\r\n");
}

///////////////////////////////////////////////////////////////////////////////
// SETUP
///////////////////////////////////////////////////////////////////////////////

void setup() {
  // Configure our normal `printf()` UART.
  Serial.begin(115200);

  // Configure the serial port for the GPS.
  gpsSerial.begin(9600, SERIAL_8N1, GPS_TX_PIN, -1);
  delay(10); // give the serial port time to initialize

  // Turn off LED.
  pinMode(LED, OUTPUT);
  digitalWrite(LED, 0);

  // Initialize MCU.
  Mcu.begin();
  display.init();

  // Configure the DEVEUI to be what is hardcoded in this chip.
  LoRaWAN.generateDeveuiByChipID();

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
    display.drawString(64, 16, "GPSLoRa");
    display.display();
    delay(2000);
  }

  // Initiate the LoRa process. This will check if the connection
  // is already established, or if we are connecting for the first
  // time (after a full power cycle for example).
  LoRaWAN.init(CLASS_A, LORAWAN_ACTIVE_REGION, loraWanAdr, joined, sent, acked, received);
  LoRaWAN.join(LORA_OTAA);
}

///////////////////////////////////////////////////////////////////////////////
// LOOP
///////////////////////////////////////////////////////////////////////////////

void loop() {
  // All operations are event based, so all we do here is
  // let the LoRaWAN stack handle events or go to sleep.
  LoRaWAN.sleep();
}
