/* MQTT Sensor Sender for Home Assistant: configuration
   
   Reads the config from and writes it to SPIFFS, and allows for entering
   the configuration data.

   Copyright 2023 Phillip C Dimond

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define filename "/spiffs/config.txt"
#define VinPerBitDefault (3.30/2.0)/4095.0   // ADC FS split in two / resolution
#define USER_INPUT_TIMEOUT_MS 60000

typedef struct {
  bool configOK;
  char Name[40];
  char DeviceID[40];
  char UID[80];
  char ssid[40];
  char pass[40];
  char mqttBrokerUrl[160];
  char mqttUsername[40];
  char mqttPassword[160];
  float battVCalFactor;
  int retries;
} Configuration;

extern Configuration config;

void SetDefaultConfig(void);
bool LoadConfiguration();
bool SaveConfiguration();
void UserConfigEntry();

#endif // #ifndef __CONFIG_H__
