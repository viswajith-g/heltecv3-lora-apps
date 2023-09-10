#include "ESP32_Mcu.h"
#include <Arduino.h>
#include "TinyGPS.h"
#include "Wire.h"
#include <EEPROM.h>
#include "HT_SSD1306Wire.h"

TinyGPS gps;
HardwareSerial gpsSerial(1);
SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

void setup() {
  Serial.begin(115200);
//   gpsSerial.begin(9600, SERIAL_8N1, 20, -1);
  gpsSerial.begin(9600, SERIAL_8N1, 45, -1);
  delay(10); // give the serial port time to initialize

  pinMode(LED, OUTPUT);
  digitalWrite(LED, 0);

  pinMode(37, INPUT);
//   digitalWrite(37, 0);

  Mcu.begin();
}

void loop() {
	long lat, lng;
	unsigned long age;

	while (gpsSerial.available() > 0) {
		gps.encode(gpsSerial.read());
    }
    gps.get_position(&lat, &lng, &age);

	char lat_str[25];
	char lng_str[25];

	char lat_dir = 'N';
	char lng_dir = 'E';
	if (lat < 0) {
		lat_dir = 'S';
		lat *= -1;
	}
	if (lng < 0) {
		lng_dir = 'W';
		lng *= -1;
	}

	long lat_deg = lat / 1000000;
	long lat_deg_rem = lat - (lat_deg * 1000000);
	long lat_min = (lat_deg_rem * 60) / 1000000;
	long lat_min_rem = (lat_deg_rem * 60) - (lat_min * 1000000);

	snprintf(lat_str, 25, "%i°%i.%i'%c", lat_deg, lat_min, lat_min_rem, lat_dir);

	long lng_deg = lng / 1000000;
	long lng_deg_rem = lng - (lng_deg * 1000000);
	long lng_min = (lng_deg_rem * 60) / 1000000;
	long lng_min_rem = (lng_deg_rem * 60) - (lng_min * 1000000);

	snprintf(lng_str, 25, "%i°%i.%i'%c", lng_deg, lng_min, lng_min_rem, lng_dir);

	Serial.println(lat_str);
	Serial.println(lng_str);
	Serial.println(String(age));

	display.init();
	display.setFont(ArialMT_Plain_16);
	display.setTextAlignment(TEXT_ALIGN_LEFT);
	display.clear();
	display.drawString(4, 5, "GPS Location:");
	display.drawString(4, 25, lat_str);
	display.drawString(4, 45, lng_str);
	display.display();

	delay(5000);
}