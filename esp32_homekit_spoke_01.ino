#include "common.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Adafruit_NeoPixel.h>

#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00) >> 8) + (((x)&0xFF) << 8))
#define TEAL_COLOR map(180, 0, 360, 0, 65535)
#define MAG_COLOR map(320, 0, 360, 0, 65535)

WebSocketsClient webSocket;
BLEScan* pBLEScan;
bool foundTemp = false;
float inkbirdTemp;
bool countdownToSleep = false;
unsigned long sleepCountdown;
Adafruit_NeoPixel pixels(1, LED_PIN, NEO_GRB);
u16_t curHue = TEAL_COLOR;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
		if (advertisedDevice.getAddress().toString() == INKBIRD_BLE_ADDRESS) {
			// First 2 bytes are temp, 2-4 are humidity (if supported)
			const char *hexData = BLEUtils::buildHexData(nullptr, (uint8_t *)advertisedDevice.getManufacturerData().data(), 2);
			inkbirdTemp = ENDIAN_CHANGE_U16(strtoul(hexData, NULL, 16)) / 100.0f;
			foundTemp = true;
			HK_INFO_LINE("Found Inkbird Temperature: %.2fF", inkbirdTemp * 1.8f + 32.0f);
			pBLEScan->stop();
		}
    }
};

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
	switch(type) {
		case WStype_DISCONNECTED:
			HK_INFO_LINE("Websocket: disconnected.");
			break;
		case WStype_CONNECTED:
			HK_INFO_LINE("Websocket: connected to url: %s", payload);
			webSocket.sendTXT("Connected");
			break;
		case WStype_TEXT:
			HK_INFO_LINE("Websocket got text: %s", payload);
			break;
		case WStype_BIN:
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
	}
}

void setup() {
	#if HK_DEBUG > HK_DEBUG_ERROR
		Serial.begin(115200);
		while (!Serial) { ; } // wait for Serial
	#endif

	HK_INFO_LINE("Starting...");
	pixels.begin();

	HK_INFO_LINE("Connecting to " WIFI_SSID ".");
	WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
	HK_INFO_LINE("Connected.");

	HK_INFO_LINE("Setting up websocket connection to hub.");
	webSocket.begin(WEBSOCKET_IP, 81, "/");
	webSocket.setAuthorization(WEBSOCKET_USER, WEBSOCKET_PASS);
	webSocket.onEvent(webSocketEvent);
	webSocket.setReconnectInterval(5000);
	webSocket.enableHeartbeat(60000, 3000, 2);

	HK_INFO_LINE("Setting up BLE.");
	BLEDevice::init("");
	pBLEScan = BLEDevice::getScan(); //create new scan
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
	pBLEScan->setInterval(BLE_SCAN_TIME * 1000);
	pBLEScan->setWindow((BLE_SCAN_TIME * 1000) - 1);  // less or equal setInterval value
}

void loop() {
	webSocket.loop();

	if (!countdownToSleep && millis() % 100 == 0) { // sine color
		u8_t brightness = ((sin(millis() * 0.01) + 1.0) / 2.0) * 122.0;
		pixels.setPixelColor(0, pixels.ColorHSV(curHue, 255, brightness));
		pixels.show();
	}

	if ((millis() % 1000) == 0) { // only check once a second
		if (countdownToSleep) {
			unsigned long diff = max(millis(), sleepCountdown) - min(millis(), sleepCountdown);
			if (diff >= SLEEP_DELAY) {
				HK_INFO_LINE("Cleaning up. See you in %i minutes.", TIME_TO_WAKE);
				pixels.clear();
				pixels.show();
				pBLEScan->clearResults();
				webSocket.disconnect();
				#if HK_DEBUG > HK_DEBUG_NONE
					Serial.flush();
				#endif
				WiFi.disconnect(true);
				esp_bt_controller_disable();
				delay(1000);
				esp_sleep_enable_timer_wakeup(TIME_TO_WAKE * uS_TO_M_FACTOR);
				esp_deep_sleep_start();
			} else {
				int secs = (SLEEP_DELAY - diff) / 1000;
				pixels.setPixelColor(0, pixels.ColorHSV(0, 0, 9 * secs)); // fade from white half brightness
				pixels.show();
				HK_INFO_LINE("Sleeping in %i...", secs);
			}
		} else {
			if (webSocket.isConnected()) {
				HK_INFO_LINE("Starting BLE scan for %i seconds.", BLE_SCAN_TIME);
				BLEScanResults foundDevices = pBLEScan->start(BLE_SCAN_TIME);

				if (foundTemp) {
					HK_INFO_LINE("Reporting Inkbird temp.");
					curHue = MAG_COLOR;
					pixels.setPixelColor(0, pixels.ColorHSV(curHue,255,255));
					pixels.show();
					
					if (webSocket.isConnected()) {
						String message = String("{'device':'IBS-TH2', 'command':'update_temp', 'payload':'");
							message += String(inkbirdTemp);
							message += "'}";
						if (!webSocket.sendTXT(message)) HK_ERROR_LINE("Error sending message: %s", message);
						else HK_INFO_LINE("Sent %s.", message.c_str());
					} else {
						HK_ERROR_LINE("Websocket not connected.");
					}
				} else HK_INFO_LINE("Did not find any Inkbird devices.");

				countdownToSleep = true;
				sleepCountdown = millis();
			} else HK_INFO_LINE("Waiting for websocket connection.");
		}
		
		delay(10);
	}
}
