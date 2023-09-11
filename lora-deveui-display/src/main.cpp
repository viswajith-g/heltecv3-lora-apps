#include "ESP32_Mcu.h"
#include <Arduino.h>
#include "Wire.h"
#include "HT_SSD1306Wire.h"

SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// Copy of `LoRaWanClass::generateDeveuiByChipID()`.
void LoRaWanClass_generateDeveuiByChipID(uint8_t* devEui) {
	uint32_t uniqueId[2];
	uint64_t id = getID();
	uniqueId[0]=(uint32_t)(id>>32);
	uniqueId[1]=(uint32_t)id;
	for (int i=0;i<8;i++) {
		if(i<4) {
			devEui[i] = (uniqueId[1]>>(8*(3-i)))&0xFF;
		} else{
			devEui[i] = (uniqueId[0]>>(8*(7-i)))&0xFF;
		}
	}
}

void setup() {
	Serial.begin(115200);
	delay(10); // give the serial port time to initialize

	pinMode(LED, OUTPUT);
	digitalWrite(LED, 0);

	Mcu.begin();

	char out[100];
	uint8_t eui[8];
	LoRaWanClass_generateDeveuiByChipID(eui);

	snprintf(out, 100, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		eui[0], eui[1], eui[2], eui[3], eui[4], eui[5], eui[6], eui[7]);

	Serial.println(out);

	display.init();
	display.setFont(ArialMT_Plain_16);
	display.setTextAlignment(TEXT_ALIGN_LEFT);
	display.clear();
	display.drawString(5, 10, "DEVICE EUI:");
	display.setFont(ArialMT_Plain_10);
	display.drawString(5, 30, out);
	display.display();
}

void loop() {
	delay(5000);
	esp_light_sleep_start();
}