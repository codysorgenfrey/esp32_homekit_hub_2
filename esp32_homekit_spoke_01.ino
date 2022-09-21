#include "common.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <time.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>

#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00) >> 8) + (((x)&0xFF) << 8))

WebSocketsClient webSocket;
int scanTime = 30; //In seconds
BLEScan* pBLEScan;
bool foundTemp = false;
float inkbirdTemp;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.getAddress().toString() == INKBIRD_BLE_ADDRESS) {
		// First 2 bytes are temp, 2-4 are humidity (if supported)
        const char *hexData = BLEUtils::buildHexData(nullptr, (uint8_t *)advertisedDevice.getManufacturerData().data(), 2);
        inkbirdTemp = ENDIAN_CHANGE_U16(strtoul(hexData, NULL, 16)) / 100.0f;
		foundTemp = true;
        HK_INFO_LINE("Found Inkbird Temperature: %.2fF", inkbirdTemp * 1.8f + 32.0f);
      }
    }
};

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
	switch(type) {
		case WStype_DISCONNECTED:
			HK_INFO_LINE("Websocket disconnected.");
			break;
		case WStype_CONNECTED:
			HK_INFO_LINE("Websocket connected to url: %s", payload);
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
	#if HK_DEBUG > HK_DEBUG_NONE
		Serial.begin(115200);
		while (!Serial) { ; } // wait for Serial
	#endif

	HK_INFO_LINE("Starting...");
	pinMode(LED_BUILTIN, OUTPUT); // setup LED
	digitalWrite(LED_BUILTIN, HIGH); // Indicate that we're working.

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

	HK_INFO_LINE("Setting up BLE.");
	BLEDevice::init("");
	pBLEScan = BLEDevice::getScan(); //create new scan
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
	pBLEScan->setInterval(scanTime * 1000);
	pBLEScan->setWindow((scanTime * 1000) - 1);  // less or equal setInterval value
}

void loop() {
	webSocket.loop();

	if ((millis() % 1000) == 0) { // only check once a second
		if (webSocket.isConnected()) { 
			HK_INFO_LINE("Starting BLE scan for %i seconds.", scanTime);
			BLEScanResults foundDevices = pBLEScan->start(scanTime, false);

			if (foundTemp) {
				HK_INFO_LINE("Reporting findings.");
				if (webSocket.isConnected()) {
					String message = String("{'device':'IBS-TH2', 'command':'update_temp', 'payload':'");
						message += String(inkbirdTemp);
						message += "'}";
					if (!webSocket.sendTXT(message)) HK_ERROR_LINE("Error sending message: %s", message);
					else HK_INFO_LINE("Sent %s.", message.c_str());
				} else {
					HK_ERROR_LINE("Websocket not connected.");
				}
			} else HK_INFO_LINE("Did not find any BLE devices.");

			HK_INFO_LINE("Going to sleep for %i minutes.", TIME_TO_WAKE);
			digitalWrite(LED_BUILTIN, LOW); // clear LED status
			#if HK_DEBUG > HK_DEBUG_NONE
				Serial.flush();
			#endif
			WiFi.disconnect(true);
			pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
			esp_bt_controller_disable();
			esp_sleep_enable_timer_wakeup(TIME_TO_WAKE * uS_TO_M_FACTOR);
			delay(1000); // delay to be sure timer was set
			esp_deep_sleep_start();
		} else HK_INFO_LINE("Waiting for websocket connection.");
		
		delay(1);
	}
}
