#pragma once
#ifndef __HOMEKIT_SPOKE_01_COMMON_H__
#define __HOMEKIT_SPOKE_01_COMMON_H__

#include "secrets.h"
#include <Arduino.h>
#include <SheetsLogger.h>

#define LED_PIN 7
#define uS_TO_M_FACTOR 60000000ULL

// Inkbird BLE
#define IB_DEVICE_ID "IBS-TH2"
#define IB_COMMAND_UPDATE_TEMP "update_temp"
#define IB_COMMAND_GET_TEMP "get_temp"
#define IB_SCAN_INTERVAL 1800000 // 30 minutes
#define IB_SCAN_TIME 90 // 90 seconds

// Logging
#define HK_DEBUG_NONE -1
#define HK_DEBUG_ERROR 0
#define HK_DEBUG_INFO 1

#define HK_DEBUG HK_DEBUG_INFO

#define FREE_HEAP() esp_get_free_heap_size() * 0.001f

#if HK_DEBUG >= HK_DEBUG_ERROR
    #define HK_ERROR_LINE(message, ...) sl_printf(SHEETS_URL, "Homekit Spoke 01", "ERR [%7d][%.2fkb] !!!Homekit Spoke 01!!!: " message "\n", millis(), FREE_HEAP(), ##__VA_ARGS__)
#else
    #define HK_ERROR_LINE(message, ...)
#endif

#if HK_DEBUG >= HK_DEBUG_INFO
    #define HK_INFO_LINE(message, ...) printf(">>> [%7d][%.2fkb] Homekit Spoke 01: " message "\n", millis(), FREE_HEAP(), ##__VA_ARGS__)
#else
    #define HK_INFO_LINE(message, ...)
#endif

#endif