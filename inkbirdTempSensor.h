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
    doc["device"] = IB_DEVICE_ID;
    doc["command"] = IB_COMMAND_UPDATE_TEMP;
    doc["payload"] = tempGetter->inkbirdTemp;
    sendHKRMessage(doc);
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

    
    StaticJsonDocument<92> resDoc;
    resDoc["device"] = IB_DEVICE_ID;
    resDoc["command"] = HKR_RESPONSE_COMMAND;
    resDoc["payload"] = success;
    sendHKRMessage(resDoc, false);

    if (!success) HK_ERROR_LINE("Error handling temerature sensor command %s.", command);
  }
};

#endif