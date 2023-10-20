#include "ESP32_Mcu.h"
#include <Arduino.h>
#include "Wire.h"
#include "HT_SSD1306Wire.h"

SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

void display_temperature(float temp) {
	char out[100];
	snprintf(out, 100, "%f°C", temp);

	display.setFont(ArialMT_Plain_16);
	display.setTextAlignment(TEXT_ALIGN_LEFT);
	display.clear();
	display.drawString(5, 10, "Temperature:");
	display.drawString(5, 30, out);
	display.display();
}

void setup() {
	Serial.begin(115200);
	delay(10); // give the serial port time to initialize

	pinMode(LED, OUTPUT);
	digitalWrite(LED, 0);

	Mcu.begin();

	display.init();
	display.setFont(ArialMT_Plain_16);
	display.setTextAlignment(TEXT_ALIGN_LEFT);
	display.clear();
	display.drawString(5, 10, "TempApp");
	display.display();
}

void loop() {
	float temp = temperatureRead();
	Serial.println(temperatureRead()); 
	display_temperature(temp);
	delay(2000);
}