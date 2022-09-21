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

WebSocketsClient webSocket;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
	switch(type) {
		case WStype_DISCONNECTED:
			HK_INFO_LINE("Websocket disconnected.\n");
			break;
		case WStype_CONNECTED:
			HK_INFO_LINE("Websocket connected to url: %s\n", payload);

			// send message to server when Connected
			webSocket.sendTXT("Connected");
			break;
		case WStype_TEXT:
			HK_INFO_LINE("Websocket got text: %s\n", payload);

			// send message to server
			// webSocket.sendTXT("message here");
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

void cleanupAndSleep() {
    digitalWrite(LED_BUILTIN, LOW); // clear LED status
    Serial.flush();
    WiFi.disconnect(true);
    esp_bt_controller_disable();
    esp_sleep_enable_timer_wakeup(TIME_TO_WAKE * uS_TO_M_FACTOR);
    delay(1000); // delay to be sure timer was set
    esp_deep_sleep_start();
}

void setup() {
	WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    // server address, port and URL
	webSocket.begin(WEBSOCKET_IP, 81, "/");

	// event handler
	webSocket.onEvent(webSocketEvent);

	// use HTTP Basic Authorization this is optional remove if not needed
	webSocket.setAuthorization(WEBSOCKET_USER, WEBSOCKET_PASS);

	// try ever 5000 again if connection has failed
	webSocket.setReconnectInterval(5000);
}

void loop() {}
