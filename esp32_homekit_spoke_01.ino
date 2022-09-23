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
#include <time.h>

#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00) >> 8) + (((x)&0xFF) << 8))

struct Tilt {
    char* color;
    double gravity;
    double temp;
};

WebSocketsClient webSocket;
BLEScan* pBLEScan;
bool foundTemp = false;
float inkbirdTemp;
Tilt *foundTilts[8]; // Max one of each color
int curTiltIndex = -1;
struct tm timeInfo;
bool countdownToSleep = false;
unsigned long sleepCountdown;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
		if (advertisedDevice.getAddress().toString() == INKBIRD_BLE_ADDRESS) {
			// First 2 bytes are temp, 2-4 are humidity (if supported)
			const char *hexData = BLEUtils::buildHexData(nullptr, (uint8_t *)advertisedDevice.getManufacturerData().data(), 2);
			inkbirdTemp = ENDIAN_CHANGE_U16(strtoul(hexData, NULL, 16)) / 100.0f;
			foundTemp = true;
			HK_INFO_LINE("Found Inkbird Temperature: %.2fF", inkbirdTemp * 1.8f + 32.0f);
		}
		if (advertisedDevice.haveManufacturerData()) {
			std::string strManufacturerData = advertisedDevice.getManufacturerData();

            uint8_t cManufacturerData[100];
            strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length(), 0);

			if (strManufacturerData.length() == 25 && cManufacturerData[0] == 0x4C && cManufacturerData[1] == 0x00) {
				BLEBeacon oBeacon = BLEBeacon();
				oBeacon.setData(strManufacturerData);
				Tilt *curTilt = new Tilt;
				const String uuid = (String)oBeacon.getProximityUUID().toString().c_str();
				if (uuid == "de742df0-7013-12b5-444b-b1c510bb95a4") {
					curTilt->color = "Red";
				} else if (uuid == "de742df0-7013-12b5-444b-b1c520bb95a4") {
					curTilt->color = "Green";
				} else if (uuid == "de742df0-7013-12b5-444b-b1c530bb95a4") {
					curTilt->color = "Black";
				} else if (uuid == "de742df0-7013-12b5-444b-b1c540bb95a4") {
					curTilt->color = "Purple";
				} else if (uuid == "de742df0-7013-12b5-444b-b1c550bb95a4") {
					curTilt->color = "Orange";
				} else if (uuid == "de742df0-7013-12b5-444b-b1c560bb95a4") {
					curTilt->color = "Blue";
				} else if (uuid == "de742df0-7013-12b5-444b-b1c570bb95a4") {
					curTilt->color = "Yellow";
				} else if (uuid == "de742df0-7013-12b5-444b-b1c580bb95a4") {
					curTilt->color = "Pink";
				} else {
					return;
				}
				
				curTilt->gravity = ENDIAN_CHANGE_U16(oBeacon.getMinor()) * 0.001;
				curTilt->temp = ENDIAN_CHANGE_U16(oBeacon.getMajor());
				curTiltIndex++;
				foundTilts[curTiltIndex] = curTilt;
				HK_INFO_LINE("Found %s tilt: %.3f at %.2fF.", curTilt->color, curTilt->gravity, curTilt->temp);
			}
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

	HK_INFO_LINE("Getting current time from " NTP_SERVER);
	configTime(PST_OFFSET, DAYLIGHT_SAVINGS_OFFSET, NTP_SERVER);
	if (!getLocalTime(&timeInfo)) HK_ERROR_LINE("Failed to get current time.");
	HK_INFO_LINE("Current time: %i/%i/%i %i:%i:%i", timeInfo.tm_mon, timeInfo.tm_mday, timeInfo.tm_year, timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

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
	pBLEScan->setInterval(BLE_SCAN_TIME * 1000);
	pBLEScan->setWindow((BLE_SCAN_TIME * 1000) - 1);  // less or equal setInterval value
}

void loop() {
	webSocket.loop();

	if ((millis() % 1000) == 0) { // only check once a second

		if (countdownToSleep) {
			unsigned long diff = max(millis(), sleepCountdown) - min(millis(), sleepCountdown);
			if (diff >= SLEEP_DELAY) {
				HK_INFO_LINE("See you in %i minutes.", TIME_TO_WAKE);
				esp_sleep_enable_timer_wakeup(TIME_TO_WAKE * uS_TO_M_FACTOR);
				esp_deep_sleep_start();
			} else {
				HK_INFO_LINE("Sleeping in %i...", (SLEEP_DELAY - diff) / 1000);
			}
		} else {
			if (webSocket.isConnected()) { 
				HK_INFO_LINE("Starting BLE scan for %i seconds.", BLE_SCAN_TIME);
				BLEScanResults foundDevices = pBLEScan->start(BLE_SCAN_TIME, false);

				if (foundTemp) {
					HK_INFO_LINE("Reporting Inkbird temp.");
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

				if (curTiltIndex >= 0) {
					HK_INFO_LINE("Reporting found tilts.");
					HK_INFO_LINE("Connecting to " TILT_HOST);
					WiFiClient client;
					if (!client.connect(TILT_HOST, TILT_PORT)) HK_ERROR_LINE("Connection to " TILT_HOST " failed.");
					else {
						HK_INFO_LINE("Connected.");

						for (int x = 0; x <= curTiltIndex; x++)
						{
							String url = "/tilt";
							url += "?id=";
							url += TILT_KEY;
							
							String content = "Temp=";
							content += String(foundTilts[x]->temp, 1);
							content += "&SG=";
							content += String(foundTilts[x]->gravity, 3);
							content += "&Color=";
							content += foundTilts[x]->color;
							content += "&Timepoint=";
							content += String(timeInfo.tm_mon) + 
									"\%2F" + String(timeInfo.tm_mday) +
									"\%2F" + String(timeInfo.tm_year) +
									"\%20" + String(timeInfo.tm_hour) + 
									"\%3A" + String(timeInfo.tm_min) + 
									"\%3A" + String(timeInfo.tm_sec);

							HK_INFO_LINE("Sending %s", content.c_str());
							client.print(String("POST ") + url + " HTTP/1.1\r\n" +
										"Host: " + TILT_HOST + "\r\n" +
										"Content-Type: application/x-www-form-urlencoded\r\n" + 
										"Content-Length: " + content.length() + "\r\n\r\n" +
										content);
							unsigned long requestStart = millis();
							while (client.available() == 0) {
								if (millis() - requestStart > 5000) {
									HK_ERROR_LINE("Post Request Timeout.");
									client.stop();
								}
							}

							while(client.available()) {
								String line = client.readStringUntil('\r');
								line.replace("\r", "");
								line.replace("\n", "");
								HK_INFO_LINE("Client: %s", line.c_str());
							}
						}
					}
				} else HK_INFO_LINE("No tilts found.");

				HK_INFO_LINE("Cleaning up.");
				digitalWrite(LED_BUILTIN, LOW); // clear LED status
				webSocket.disconnect();
				#if HK_DEBUG > HK_DEBUG_NONE
					Serial.flush();
				#endif
				WiFi.disconnect(true);
				pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
				esp_bt_controller_disable();
				countdownToSleep = true;
				sleepCountdown = millis();
			} else HK_INFO_LINE("Waiting for websocket connection.");
		}
		
		delay(1);
	}
}
