/* MQTT Relay Controller for Enphase Solar systems
   
   Receives MQTT data on site power consumption and calculates an
   appropriate generation level for an Enphase system, then sets
   four relays that drive the relay inputs on the Envoy to use its'
   power limiting function to curtail feed-in to the grid. The
   export control also gets the current export price and only
   curtails when the price is less than a threshold value. This is
   for situations where feed in can be a negative value, ie the 
   householder is charged by the electricity company to export to
   the grid.

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
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_wifi.h" 
#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"

#include "commonvalues.h"
#include "utilities.h"
#include "config.h"
#include "main.h"

const char *TAG = "EnphaseLimiter";

esp_err_t err;
int retry_num = 0;
char s[240]; // general purpose string input
bool wiFiGotIP = false;
bool wiFiConnected = false;
bool mqttConnected = false;
int mqttMessagesQueued = 0;
bool gotTime = false;
int year = 0, month = 0, day = 0, hour = 0, minute = 0, seconds = 0;
esp_mqtt_client_handle_t client;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code); 
    }
}

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi CONNECTING...."); 
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi CONNECTED"); 
        wiFiConnected = true;
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGE(TAG, "WiFi lost connection"); 
        wiFiConnected = false;
        wiFiGotIP = false;
        if (retry_num < 5) {
            esp_wifi_connect();
            retry_num++;
            ESP_LOGI(TAG, "Retrying to Connect, attempt # %d", retry_num); 
        } else { ESP_LOGE(TAG, "failed to reconnect after %d attempts.", retry_num); }
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        wiFiGotIP = true;
    } else {
        ESP_LOGI(TAG, "Unhandled WiFi event %ld", event_id); 
    }
}

void wifi_connection()
{
    err = nvs_flash_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Error at nvs_flash_init: %d = %s.", err, esp_err_to_name(err)); }
    err = esp_netif_init();                                                                    // network interface initialization
    if (err != ESP_OK) { ESP_LOGE(TAG, "Error at esp_netif_init: %d = %s.", err, esp_err_to_name(err)); }
    err = esp_event_loop_create_default();                                                     // responsible for handling and dispatching events
    if (err != ESP_OK) { ESP_LOGE(TAG, "Error at esp_event_loop_create_default: %d = %s.", err, esp_err_to_name(err)); }
    esp_netif_create_default_wifi_sta();                                                 // sets up necessary data structs for wifi station interface
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();                     // sets up wifi wifi_init_config struct with default values
    err = esp_wifi_init(&wifi_initiation);                                               // wifi initialised with dafault wifi_initiation
    if (err != ESP_OK) { ESP_LOGE(TAG, "Error at esp_wifi_init: %d = %s.", err, esp_err_to_name(err)); }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);  // creating event handler register for wifi
    if (err != ESP_OK) { ESP_LOGE(TAG, "Error at esp_event_handler_register(WIFI_EVENT: %d = %s.", err, esp_err_to_name(err)); }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL); // creating event handler register for ip event
    if (err != ESP_OK) { ESP_LOGE(TAG, "Error at esp_event_handler_register(IP_EVENT: %d = %s.", err, esp_err_to_name(err)); }
    wifi_config_t wifi_configuration = {                                                 // struct wifi_config_t var wifi_configuration
        .sta = {
            // we are sending a const char of ssid and password which we will strcpy in following line so leaving it blank
            .ssid = "",
            .password = ""
        }
    };
    strcpy((char*)wifi_configuration.sta.ssid, config.ssid);    
    strcpy((char*)wifi_configuration.sta.password, config.pass);   
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);  // setting up configs when event ESP_IF_WIFI_STA
    esp_wifi_start();       // start connection with configurations provided in funtion
    esp_wifi_set_mode(WIFI_MODE_STA);   // station mode selected
    esp_wifi_connect(); // connect with saved ssid and pass
    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s  password:%s", config.ssid, config.pass);
} 

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    char topic[200];
    char payload[1024];
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;

        case MQTT_EVENT_CONNECTED:
            mqttConnected = true;
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            // Subscribe to the time feed
            msg_id = esp_mqtt_client_subscribe(client, "homeassistant/CurrentTime", 0);
            ESP_LOGI(TAG, "Subscribe sent for time feed, msg_id=%d", msg_id);
            
            /*
            // Send the sensor configurations
            sprintf(topic, "homeassistant/binary_sensor/EntryMotion/config");
            sprintf(payload, 
                "{ \"name\": \"EntryMotion\", \
                \"device_class\": \"motion\", \
                \"state_topic\": \"homeassistant/binary_sensor/EntryMotion/state\", \
                \"payload_on\": 1, \
                \"payload_off\": 0, \
                \"unique_id\": \"mysensor1\", \
                \"device\": {\"identifiers\": [\"HouseAlarm-ID\"], \"name\": \"HouseAlarm\"}, \
                \"retain\": true }");
            msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); // Temp sensor config, set the retain flag on the message
            mqttMessagesQueued++;
            ESP_LOGI(TAG, "Published config message for HomuseAlarm EntryMotion sensor, msg_id=%d", msg_id);

            sprintf(topic, "homeassistant/binary_sensor/RumpusMotion/config");
            sprintf(payload, 
                "{ \"name\": \"RumpusMotion\", \
                \"device_class\": \"motion\", \
                \"state_topic\": \"homeassistant/binary_sensor/RumpusMotion/state\", \
                \"payload_on\": 1, \
                \"payload_off\": 0, \
                \"unique_id\": \"mysensor2\", \
                \"device\": {\"identifiers\": [\"HouseAlarm-ID\"], \"name\": \"HouseAlarm\"}, \
                \"retain\": true }");
            msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); // Temp sensor config, set the retain flag on the message
            mqttMessagesQueued++;
            ESP_LOGI(TAG, "Published config message for HomuseAlarm EntryMotion sensor, msg_id=%d", msg_id);
            */

            // Send an online message
            sprintf(topic, "homeassistant/binary_sensor/HouseAlarm/availability");
            sprintf(payload, "online");
            msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); // Temp sensor config, set the retain flag on the message
            mqttMessagesQueued++;
            ESP_LOGI(TAG, "Published online message, msg_id=%d", msg_id);
            break;

        case MQTT_EVENT_DISCONNECTED:
            mqttConnected = false;
            ESP_LOGE(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            mqttMessagesQueued--;
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            mqttMessagesQueued--;
            break;

        case MQTT_EVENT_DATA:
            //ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            strncpy(s, event->topic, event->topic_len);
            s[event->topic_len] = '\0';
            ESP_LOGV(TAG, "Received an event - topic was %s", s);
            if (strcmp(s, "homeassistant/CurrentTime") == 0) {
                // Process the time
                ESP_LOGV(TAG, "Got the time from %s, as %.*s.", s, event->data_len, event->data);
                gotTime = true;
                strncpy(s, event->data, event->data_len);
                s[event->data_len] = 0;
                sscanf(s, "%d.%d.%d %d:%d:%d", &year, &month, &day, &hour, &minute, &seconds);        

                // Send an online every 10 seconds
                if (seconds % 10 == 0) {
                    // Send an online message
                    sprintf(topic, "homeassistant/binary_sensor/HouseAlarm/availability");
                    sprintf(payload, "online");
                    msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); 
                    mqttMessagesQueued++;
                    ESP_LOGI(TAG, "Published online message for HouseAlarmm, id=%d", msg_id);
                }
            } else {
                ESP_LOGI(TAG, "Received unexpected message, topic %s", s);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR. ");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
                ESP_LOGI(TAG, "WiFi connected = %d", wiFiConnected);
            }
            break;

        default:
            ESP_LOGE(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

static void mqtt_app_start(void)
{
    char lwTopic[100];
    sprintf(lwTopic, "homeassistant/binary_sensor/HouseAlarm/availability");
    const char* lwMessage = "offline";
    esp_mqtt_client_config_t mqtt_cfg = {
        .network = {
            .reconnect_timeout_ms = 250, // Reconnect MQTT broker after this many ms
        },
        .broker.address.uri = config.mqttBrokerUrl,
        .credentials = { 
            .username = config.mqttUsername, 
            .authentication = { 
                .password = config.mqttPassword
            }, 
        },
        .session = {
            .message_retransmit_timeout = 250,  // ms transmission retry
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
            .keepalive = 30, // 30 second keepalive timeout
            .last_will = {
                .topic = lwTopic,
                .msg = (const char*)lwMessage,
                .msg_len = strlen(lwMessage),
                .qos = 1,
                .retain = 1
            }
        },
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    err = esp_mqtt_client_start(client);
    if (err != ESP_OK) { ESP_LOGE(TAG, "MQTT client start error: %s", esp_err_to_name(err)); }
}

void app_main(void)
{
    bool configMode = false;

    // GPIO setup
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
    gpio_set_direction(SW_IN_1, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SW_IN_1, GPIO_PULLUP_ONLY);
    gpio_set_direction(SW_IN_2, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SW_IN_2, GPIO_PULLUP_ONLY);

    // Read the config mode button
    if (gpio_get_level(BUTTON_PIN) == 0) { configMode = true; }

    // Initialise the SPIFFS system
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};
    
    err = esp_vfs_spiffs_register(&spiffs_conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SPIFFS Mount Failed: %s\r\n", esp_err_to_name(err));
        ESP_LOGE(TAG, "Reformatting the SPIFFs partition, please restart.");
        return;
    }

    // Load the configuration from the file system
    bool configLoad = LoadConfiguration();
    if (configLoad == false || config.configOK == false) 
    {
        if (configLoad == false)
        {
            ESP_LOGI(TAG, "Loading the configuration failed. Please enter the configuration details.\r\n");
        }
        else if (config.configOK == false)
        {
            ESP_LOGE(TAG, "The stored configuration is marked as invalid. Please enter the configuration details.\r\n");
        }
        configMode = true;
    }
    else
    {
        ESP_LOGI(TAG, "Loaded config: configOK: %d, Name: %s, Device ID: %s", config.configOK, config.Name, config.DeviceID);
        ESP_LOGI(TAG, "               UID: %s, battVCalFactor: %fV", config.UID, config.battVCalFactor);
        ESP_LOGI(TAG, "               WiFi SSID: %s, WiFi Password: %s", config.ssid, config.pass);
        ESP_LOGI(TAG, "               MQTT URL: %s, Username: %s, Password: %s", config.mqttBrokerUrl, config.mqttUsername, config.mqttPassword);
    }
    
    // If we're in config mode, ask if the user wants to change the config
    if (configMode) {
        printf("\r\nDo you want to change the configuration (y/n)? ");
        char c = 'n';
        if (getLineInput(s, 1) > 0) { c = s[0]; }
        printf("\r\n");
        if (c == 'y' || c == 'Y') { UserConfigEntry(); }
    }

    // Start WiFi, wait for WiFi to connect and get IP
    wifi_connection();
    int loops = 0;
    while (loops < 10000 && !wiFiGotIP) {
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Wait 10 millseconds
    }

    // Start mqtt, then wait up to 40 * 0.25 = 10 seconds for it to start
    mqtt_app_start();
    int mqttWaits = 0;
    while (!mqttConnected && mqttWaits < 40) { vTaskDelay(250 / portTICK_PERIOD_MS); mqttWaits++; } 
    ESP_LOGI(TAG, "MQTT client started after %f seconds.", ((float)mqttWaits) * 0.25);

    // Loop forever, processing MQTT events.
    int input1_currentState = false;
    int input1_previousState = false;
    int input2_currentState = false;
    int input2_previousState = false;
    while(true) {        
        if (!mqttConnected) { 
            ESP_LOGE(TAG, "Detected the MQTT client is offline in the main loop. Attempting to stop, destroy then restart it.");
            err = esp_mqtt_client_stop(client);
            if (err != ESP_OK) { ESP_LOGE(TAG, "MQTT client stop error: %s", esp_err_to_name(err)); }
            err = esp_mqtt_client_destroy(client);
            if (err != ESP_OK) { ESP_LOGE(TAG, "MQTT client destroy error: %s", esp_err_to_name(err)); }
            mqtt_app_start();
        }        
        vTaskDelay(25 / portTICK_PERIOD_MS); // Sleep 

        // Check the inputs and send state messages
        char topic[250];
        char payload[80];
        input1_currentState = gpio_get_level(SW_IN_1);
        // Send a state message if the input changed
        if (input1_currentState != input1_previousState) {
            input1_previousState = input1_currentState;
            sprintf(topic, "homeassistant/binary_sensor/HouseAlarm/EntryMotion/state");
            if (input1_currentState) { sprintf(payload, "1"); } else { sprintf(payload, "0"); }
            int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); 
            mqttMessagesQueued++;
            ESP_LOGI(TAG, "Published state message for Entry motion sensor, msg_id=%d", msg_id);
        }
        input2_currentState = gpio_get_level(SW_IN_2);
        // Send a state message if the input changed
        if (input2_currentState != input2_previousState) {            
            input2_previousState = input2_currentState;
            sprintf(topic, "homeassistant/binary_sensor/HouseAlarm/RumpusMotion/state");
            if (input2_currentState) { sprintf(payload, "1"); } else { sprintf(payload, "0"); }
            int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); 
            mqttMessagesQueued++;
            ESP_LOGI(TAG, "Published state message for Rumpus motion sensor, msg_id=%d", msg_id);
        }
    }

}
