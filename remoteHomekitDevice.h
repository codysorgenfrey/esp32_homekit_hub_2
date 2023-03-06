#pragma once
#ifndef REMOTEHOMEKITDEVICE_H
#define REMOTEHOMEKITDEVICE_H

#include "common.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

class RemoteHomekitDevice {
  WebSocketsClient *webSocket;
  unsigned long lastMessage = 0;
  bool needResponse = false;

public:
  RemoteHomekitDevice(WebSocketsClient *ws) {
    webSocket = ws;
  }

  virtual void handleCommand(const JsonDocument &doc) = 0;

  void sendHomekitMessage(const JsonDocument &doc) {
    String message;
    serializeJson(doc, message);
    if (!webSocket->sendTXT(message)) HK_ERROR_LINE("Error sending message: %s", message);
    lastMessage = millis();
    // needResponse = true;
  }

  void homekitMessageRecieved(const JsonDocument &doc) {
    const char *command = doc["command"].as<const char *>();
    if (strcmp(command, RESPONSE_COMMAND) == 0) {
      if (needResponse) {
        needResponse = false;
        lastMessage = 0;
      } else {
        HK_ERROR_LINE("Unexpected hub response.");
      }
    } else {
      handleCommand(doc);
    }
  }

  void listenForHomekitResponse() {
    if (!needResponse) return;

    const unsigned long now = millis();
    const unsigned long diff = max(now, lastMessage) - min(now, lastMessage);
    if (diff >= HUB_RESPONSE_TIMEOUT) {
      HK_ERROR_LINE("Homekit hub not responding.");
    }
  }
};

#endif