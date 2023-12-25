/* MQTT Enphase Envoy power controller for Home Assistant
   
   Gets power data from Home Assistant via MQTT and uses it to set
   the Enphase Wnvoy relay inputs to limit power production so that
   there is no export to the grid when appropriate.

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

#ifndef __MAIN_H__
#define __MAIN_H__

#define BUTTON_PIN GPIO_NUM_13

#define SLEEPTIME 30
#define S_TO_uS(s) (s * 1000000)
#define uS_TO_S(s) (s / 1000000)

static void log_error_if_nonzero(const char *message, int error_code);
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void wifi_connection(void);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_app_start(void);
void app_main(void);

#endif // __MAIN_H__
