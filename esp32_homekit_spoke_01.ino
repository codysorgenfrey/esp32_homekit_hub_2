#include "common.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Adafruit_NeoPixel.h>
#include "inkbirdTempSensor.h"

#define WHITE_COLOR pixels.gamma32(pixels.ColorHSV(0, 0, 64))
#define MAG_COLOR pixels.gamma32(pixels.ColorHSV(320, 255, 128))
#define RED_COLOR pixels.gamma32(pixels.ColorHSV(40, 255, 128))

WebSocketsClient webSocket;
InkbirdTempSensor *tempSensor = NULL;
Adafruit_NeoPixel pixels(1, LED_PIN, NEO_GRB);

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
	switch(type) {
		case WStype_DISCONNECTED:
			HK_INFO_LINE("Websocket: disconnected.");
			if (tempSensor != NULL) {
				delete tempSensor;
				tempSensor = NULL;
			}
			pixels.setPixelColor(0, RED_COLOR);
			pixels.show();
			break;
		case WStype_CONNECTED:
			HK_INFO_LINE("Websocket: connected to url: %s", payload);
			tempSensor = new InkbirdTempSensor(&webSocket);
			pixels.clear();
			pixels.show();
			break;
		case WStype_TEXT:
			HK_INFO_LINE("Websocket got text: %s", payload);
			tempSensor->HKRWebsocketEvent(0, payload);
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
	Serial.begin(115200);
	#if HK_DEBUG > HK_DEBUG_ERROR
		while (!Serial) { ; } // wait for Serial
	#endif

	HK_INFO_LINE("Starting...");
	pixels.begin();
	pixels.setPixelColor(0, MAG_COLOR);
	pixels.show();

	HK_INFO_LINE("Connecting to " WIFI_SSID ".");
	WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
	HK_INFO_LINE("Connected.");
	sl_printf(SHEETS_URL, "Homekit Spoke 01", "Rebooting...\n");

	HK_INFO_LINE("Setting up websocket connection to hub.");
	webSocket.begin(WEBSOCKET_IP, 81, "/");
	webSocket.setAuthorization(WEBSOCKET_USER, WEBSOCKET_PASS);
	webSocket.onEvent(webSocketEvent);
	webSocket.setReconnectInterval(5000);
	webSocket.enableHeartbeat(60000, 3000, 2);
}

void loop() {
	webSocket.loop();

	if (webSocket.isConnected()) {
		tempSensor->loop();
	}
}
