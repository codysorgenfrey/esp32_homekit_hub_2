#pragma once
#ifndef INKBIRDTEMPSENSOR_H
#define INKBIRDTEMPSENSOR_H

#include "common.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HomekitRemoteDevice.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00) >> 8) + (((x)&0xFF) << 8))

class InkbirdTempGetter: public BLEAdvertisedDeviceCallbacks {
public:
  bool foundTemp = false;
  float inkbirdTemp;

  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getAddress().toString() == INKBIRD_BLE_ADDRESS) {
      // First 2 bytes are temp, 2-4 are humidity (if supported)
      const char *hexData = BLEUtils::buildHexData(nullptr, (uint8_t *)advertisedDevice.getManufacturerData().data(), 2);
      inkbirdTemp = ENDIAN_CHANGE_U16(strtoul(hexData, NULL, 16)) / 100.0f;
      foundTemp = true;
      HK_INFO_LINE("Found Inkbird Temperature: %.2fF", inkbirdTemp * 1.8f + 32.0f);
      advertisedDevice.getScan()->stop();
    }
  }
};

class InkbirdTempSensor : public HomekitRemoteDevice {
  BLEScan* pBLEScan;
  InkbirdTempGetter *tempGetter;
  WebSocketsClient *webSocket;
  unsigned long lastScan = -60000; // wait 1 minute before first scan
  bool scanning = false;

  void startScan() {
    HK_INFO_LINE("Starting BLE scan for %i seconds.", IB_SCAN_TIME);
    pBLEScan = BLEDevice::getScan(); //create new scan
    tempGetter = new InkbirdTempGetter();
    pBLEScan->setAdvertisedDeviceCallbacks(tempGetter);
    pBLEScan->setActiveScan(true); 
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    pBLEScan->start(IB_SCAN_TIME);
    scanning = true;
  }

  void endScan() {
    HK_INFO_LINE("Stopping BLE scan.");
    pBLEScan->clearResults();
    delete tempGetter;
    scanning = false;
    lastScan = millis();
  }

  void updateHomekitTemp() {
    HK_INFO_LINE("Reporting Inkbird temp: %.02f.", tempGetter->inkbirdTemp);
    StaticJsonDocument<92> doc;
    doc[HKR_DEVICE] = IB_DEVICE_ID;
    doc[HKR_COMMAND] = IB_COMMAND_UPDATE_TEMP;
    doc[HKR_PAYLOAD] = tempGetter->inkbirdTemp;
    sendHKRMessage(doc, true , [](bool success) {
      if (success) HK_INFO_LINE("Successfully sent temp to hub through HKR.");
      else HK_ERROR_LINE("Error sending temp to hub through HKR.");
    });
  }

public:
  InkbirdTempSensor(WebSocketsClient *ws) : HomekitRemoteDevice(ws, IB_DEVICE_ID) {
    HK_INFO_LINE("Created InkbirdTempSensor");
    BLEDevice::init("");
  }

  void loop() {
    unsigned long now = millis();

    if (!scanning) {
      unsigned long diff = max(now, lastScan) - min(now, lastScan);
      if (diff >= IB_SCAN_INTERVAL) startScan();
      else if (now % 60000 == 0) {
        HK_INFO_LINE("Waiting %i minutes to scan again.", (IB_SCAN_INTERVAL - diff) / 60000);
        delay(10);
      }
    } else {
      if (tempGetter->foundTemp) {
        updateHomekitTemp();
        endScan();
      } else if (now % 1000 == 0) {
        HK_INFO_LINE("Scanning...");
        delay(10);
      }
    }

    listenForHKRResponse();
  }

  void handleHKRCommand(const JsonDocument &doc) {
    bool success = false;
    const char *command = doc["command"].as<const char *>();

    if (strcmp(command, IB_COMMAND_GET_TEMP) == 0) {
      HK_INFO_LINE("Received command to get temp.");
      if (!scanning) startScan();
      success = true;
    }

    sendHKRResponse(success);
    if (!success) HK_ERROR_LINE("Error handling temerature sensor command %s.", command);
  }

  void handleHKRError(HKR_ERROR err) {
    switch (err) {
      case HKR_ERROR_CONNECTION_REFUSED:
        HK_ERROR_LINE("%s: HKR refused connection.", IB_DEVICE_ID);
        break;
      case HKR_ERROR_DEVICE_NOT_REGISTERED:
        HK_ERROR_LINE("%s: HKR device not registered.", IB_DEVICE_ID);
        break;
      case HKR_ERROR_TIMEOUT:
        HK_ERROR_LINE("%s: HKR timeout waiting for response.", IB_DEVICE_ID);
        break;
      case HKR_ERROR_UNEXPECTED_RESPONSE:
        HK_ERROR_LINE("%s: HKR response recieved to no command.", IB_DEVICE_ID);
        break;
      case HKR_ERROR_WEBSOCKET_ERROR:
        HK_ERROR_LINE("%s: HKR websocket error.", IB_DEVICE_ID);
        break;
      default:
        HK_ERROR_LINE("%s: HKR unknown error: %i.", IB_DEVICE_ID, err);
        break;
    }
  }
};

#endif