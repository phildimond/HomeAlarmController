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

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_spiffs.h"
#include <cJSON.h>

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"

#include "config.h"
#include "utilities.h"

Configuration config;

void SetDefaultConfig()
{
    // Create the default config file
    config.configOK = false;
    strcpy(config.Name, "NotSet!");
    strcpy(config.DeviceID, "Not Set!");
    strcpy(config.UID, "Not Set!");
    strcpy(config.ssid, "Not Set!");
    strcpy(config.pass, "Not Set!");
    strcpy(config.mqttBrokerUrl, "Not Set!");
    strcpy(config.mqttUsername, "Not Set!");
    strcpy(config.mqttPassword, "Not Set!");
    config.battVCalFactor = 1.0;
}

// Loads the configuration from a file
bool LoadConfiguration()
{
    // Open file for reading
    FILE *f = fopen(filename, "r");
    if (f == NULL)
    {
        printf("Failed to open file for reading.\r\n");
        return false;
    }

    // Initialise the json document
    char doc[2048];
    memset(doc, '\0', sizeof(doc));

    // Read the settings file
    int p = 0;
    while(1) {
      char c = fgetc(f);
      if( feof(f) || p >= sizeof(doc) - 1) { break; } 
      doc[p++] = c;
    }
    fclose(f);

    // Parse the json config document
    cJSON* settingsJSON = cJSON_Parse(doc);
    if (settingsJSON == NULL) {
        printf("Error parsing json config file.\r\n");
        return false;
    }

    // Extract the config information
    char errorString[80];
    memset (errorString, '\0', sizeof(errorString));
    errorString[0] = ' ';

    cJSON* item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "configOK");
    if (cJSON_IsBool(item)) {
        config.configOK = (bool)(item->valueint);
    } else { strcat(errorString, "configOK "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "Name");
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        strcpy(config.Name, item->valuestring);
    } else { strcat(errorString, "Name "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "DeviceID");
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        strcpy(config.DeviceID, item->valuestring);
    } else { strcat(errorString, "DeviceID "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "UID");
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        strcpy(config.UID, item->valuestring);
    } else { strcat(errorString, "UID "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "battVCalFactor");
    if (cJSON_IsNumber(item)) {
        config.battVCalFactor = (float)(item->valuedouble);
    } else { strcat(errorString, "battVCalFactor "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "ssid");
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        strcpy(config.ssid, item->valuestring);
    } else { strcat(errorString, "ssid "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "pass");
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        strcpy(config.pass, item->valuestring);
    } else { strcat(errorString, "pass "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "mqttBrokerUrl");
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        strcpy(config.mqttBrokerUrl, item->valuestring);
    } else { strcat(errorString, "mqttBrokerUrl "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "mqttUsername");
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        strcpy(config.mqttUsername, item->valuestring);
    } else { strcat(errorString, "mqttUsername "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "mqttPassword");
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        strcpy(config.mqttPassword, item->valuestring);
    } else { strcat(errorString, "mqttPassword "); } // record which value failed

    item = cJSON_GetObjectItemCaseSensitive(settingsJSON, "retries");
    if (cJSON_IsNumber(item)) {
        config.retries = item->valueint;
    } else { strcat(errorString, "retries "); } // record which value failed

    // Report any decoding errors
    if (strlen(errorString) != 1) {
        printf("Error decoding these configuration elements: %s\r\n", errorString);
        return false;
    }

    // Remove the cJSON documents to recover memory
    cJSON_Delete(settingsJSON);

    return true;
}

// Saves the configuration to a file
bool SaveConfiguration()
{
    // Allocate a temporary JsonDocument
    cJSON *root = cJSON_CreateObject();

    // Set the values in the document
    cJSON_AddItemToObject(root, "configOK", cJSON_CreateBool(true));
    cJSON_AddItemToObject(root, "Name", cJSON_CreateString(config.Name));
    cJSON_AddItemToObject(root, "DeviceID", cJSON_CreateString(config.DeviceID));
    cJSON_AddItemToObject(root, "UID", cJSON_CreateString(config.UID));
    cJSON_AddItemToObject(root, "battVCalFactor", cJSON_CreateNumber(config.battVCalFactor));
    cJSON_AddItemToObject(root, "ssid", cJSON_CreateString(config.ssid));
    cJSON_AddItemToObject(root, "pass", cJSON_CreateString(config.pass));
    cJSON_AddItemToObject(root, "mqttBrokerUrl", cJSON_CreateString(config.mqttBrokerUrl));
    cJSON_AddItemToObject(root, "mqttUsername", cJSON_CreateString(config.mqttUsername));
    cJSON_AddItemToObject(root, "mqttPassword", cJSON_CreateString(config.mqttPassword));
    cJSON_AddItemToObject(root, "retries", cJSON_CreateNumber(config.retries));

    // Write the values to the file
    char* rendered = cJSON_Print(root);
    //printf("Rendered JSON: %s\r\n", rendered);

    // Remove the cJSON documents to recover memory
    cJSON_Delete(root);

    // Open file for writing, overwite if exists
    FILE *f = fopen(filename, "w");
    if (f == NULL)
    {
        printf("Failed to create file.\r\n");
        return false;
    }
    fprintf(f, "%s", rendered);
    fclose(f);

    // Open file for reading
    f = fopen(filename, "r");
    if (f == NULL)
    {
        printf("Failed to open file to read it back.\r\n");
        return false;
    }

    return true;
}

void UserConfigEntry()
{
    char s[250];
    Configuration temp;

    strcpy(temp.Name, config.Name);
    strcpy(temp.DeviceID, config.DeviceID);
    strcpy(temp.UID, config.UID);
    strcpy(temp.ssid, config.ssid);
    strcpy(temp.pass, config.pass);
    strcpy(temp.mqttBrokerUrl, config.mqttBrokerUrl);
    strcpy(temp.mqttUsername, config.mqttUsername);
    strcpy(temp.mqttPassword, config.mqttPassword);
    printf("\r\nConfiguration: Enter the device name in HA (%s) : ", temp.Name);
    if (getLineInput(s, sizeof(s)))
    {
        if (strlen(s) > 0 && strlen(s) < sizeof(temp.Name))
        {
            strlcpy(temp.Name, s, sizeof(temp.Name) - 1);
        }
        else
        {
            printf("%s", temp.Name);
        }
    }
    printf("\r\nConfiguration: Enter the Device ID for HA (%s) : ", temp.DeviceID);
    if (getLineInput(s, sizeof(s)))
    {
        if (strlen(s) > 0 && strlen(s) < sizeof(temp.DeviceID))
        {
            strlcpy(temp.DeviceID, s, sizeof(temp.DeviceID) - 1);
        }
        else
        {
            printf("%s", temp.DeviceID);
        }
    }
    printf("\r\nConfiguration: Enter the device UID (%s) : ", temp.UID);
    if (getLineInput(s, sizeof(s)))
    {
        if (strlen(s) > 0 && strlen(s) < sizeof(temp.UID))
        {
            strlcpy(temp.UID, s, sizeof(temp.UID) - 1);
        }
        else
        {
            printf("%s", temp.UID);
        }
    }
    printf("\r\nConfiguration: Enter the SSID to connect to (%s) : ", temp.ssid);
    if (getLineInput(s, sizeof(s)))
    {
        if (strlen(s) > 0 && strlen(s) < sizeof(temp.ssid))
        {
            strlcpy(temp.ssid, s, sizeof(temp.ssid) - 1);
        }
        else
        {
            printf("%s", temp.ssid);
        }
    }
    printf("\r\nConfiguration: Enter the SSID's password (%s) : ", temp.pass);
    if (getLineInput(s, sizeof(s)))
    {
        if (strlen(s) > 0 && strlen(s) < sizeof(temp.pass))
        {
            strlcpy(temp.pass, s, sizeof(temp.pass) - 1);
        }
        else
        {
            printf("%s", temp.pass);
        }
    }
    printf("\r\nConfiguration: Enter the MQTT broker's URL (%s) : ", temp.mqttBrokerUrl);
    if (getLineInput(s, sizeof(s)))
    {
        if (strlen(s) > 0 && strlen(s) < sizeof(temp.mqttBrokerUrl))
        {
            strlcpy(temp.mqttBrokerUrl, s, sizeof(temp.mqttBrokerUrl) - 1);
        }
        else
        {
            printf("%s", temp.mqttBrokerUrl);
        }
    }
    printf("\r\nConfiguration: Enter the username for the MQTT broker (%s) : ", temp.mqttUsername);
    if (getLineInput(s, sizeof(s)))
    {
        if (strlen(s) > 0 && strlen(s) < sizeof(temp.mqttUsername))
        {
            strlcpy(temp.mqttUsername, s, sizeof(temp.mqttUsername) - 1);
        }
        else
        {
            printf("%s", temp.mqttUsername);
        }
    }
    printf("\r\nConfiguration: Enter the password for the MQTT broker (%s) : ", temp.mqttPassword);
    if (getLineInput(s, sizeof(s)))
    {
        if (strlen(s) > 0 && strlen(s) < sizeof(temp.mqttPassword))
        {
            strlcpy(temp.mqttPassword, s, sizeof(temp.mqttPassword) - 1);
        }
        else
        {
            printf("%s", temp.mqttPassword);
        }
    }

    printf("\r\n");
    printf("Set configuration to Name=%s, Device ID=%s, UID=%s\r\n", temp.Name, temp.DeviceID, temp.UID);
    printf("                     WiFi SSID=%s, WiFi Password=%s\r\n", temp.ssid, temp.pass);
    printf("                     MQTT URL=%s, Username=%s, Password=%s\r\n", temp.mqttBrokerUrl, temp.mqttUsername, temp.mqttPassword);

    printf("Do you wish to set these values (y/N)? ");
    if (getLineInput(s, 80))
    {
        if (s[0] == 'Y' || s[0] == 'y')
        {
            strcpy(config.Name, temp.Name);
            strcpy(config.DeviceID, temp.DeviceID);
            strcpy(config.UID, temp.UID);
            strcpy(config.ssid, temp.ssid);
            strcpy(config.pass, temp.pass);
            strcpy(config.mqttBrokerUrl, temp.mqttBrokerUrl);
            strcpy(config.mqttUsername, temp.mqttUsername);
            strcpy(config.mqttPassword, temp.mqttPassword);
            config.retries = 0;
            if (SaveConfiguration()) { printf("\r\nSaved the new configuration.\r\n"); }
            else { printf("\r\nERROR trying to save the new configuration.\r\n"); }
        }
        else
        {
            printf("\r\nNew configuration NOT saved.\r\n");
        }
    }
}
