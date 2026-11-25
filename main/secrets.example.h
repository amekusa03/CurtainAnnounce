#pragma once

/**
 * @file secrets.example.h
 * @brief Template for sensitive configurations (Wi-Fi, API credentials, etc.)
 *
 * HOW TO USE:
 * 1. Copy this file to "secrets.h" in the same directory:
 *      cp main/secrets.example.h main/secrets.h
 * 2. Update the credentials below with your actual values.
 * 3. Ensure secrets.h is listed in .gitignore (it is by default).
 */

// Wi-Fi Credentials
#define CONFIG_WIFI_SSID            "YOUR_WIFI_SSID"
#define CONFIG_WIFI_PASSWORD        "YOUR_WIFI_PASSWORD"

// Google Home Device IP on Local Network
#define CONFIG_GOOGLE_HOME_IP       "192.168.11.7"

// Outdoor Data API Endpoint (e.g. SwitchBot sensor board)
#define CONFIG_WEATHER_API_URL      "http://esp32-switchbot.local/api/sensor"
#define CONFIG_WEATHER_API_TOKEN    ""
