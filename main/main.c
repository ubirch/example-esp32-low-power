/*!
 * @file    main.c
 * @brief   main function of the esp low power program
 *
 * @author Waldemar Gruenwald
 * @date   2019-04-02
 *
 * @copyright &copy; 2019 ubirch GmbH (https://ubirch.com)
 *
 * ```
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ```
 */
#ifndef ESP_PLATFORM
#define ESP_PLATFORM
#endif

#include <sys/time.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <soc/rtc_wdt.h>
#include <esp_log.h>

#include <storage.h>
#include <ubirch_console.h>
#include <key_handling.h>
#include <networking.h>
#include <sntp_time.h>
#include <ubirch_ota.h>

#include "sensor.h"
#include "util.h"


#define MAIN_SLEEP_READY    BIT1
#define MAIN_SEND_READY     BIT2
#define MAIN_TIME_READY     BIT3

#define INTERVAL_MEASURE    (time_t)10        //!> measurement interval 10 / [1s]
#define INTERVAL_TIME       (time_t)21600   //!> time update interval 6h / [1s]
#define INTERVAL_OTA        (time_t)43200   //!> firmware update interval 12h / [1s]

#define SLEEP_INTERVAL 5000000    //!< time interval for sleeping [1us]

/*
 * Task handles for all the different tasks
 */
static TaskHandle_t sleep_task_handle = NULL;
static TaskHandle_t sensor_task_handle = NULL;
static TaskHandle_t wifi_task_handle = NULL;
static TaskHandle_t key_register_handle = NULL;
static TaskHandle_t console_handle = NULL;
static TaskHandle_t send_data_handle = NULL;
static TaskHandle_t update_time_handle = NULL;
static TaskHandle_t ota_task_handle = NULL;


/*
 * FreeRTOS event group to signal when main tasks can be synchronized
 */
EventGroupHandle_t main_event_group = NULL;


/*!
 * system status structure to store all important information
 */
struct system_status {
	time_t time;
	time_t next_measurement_time;
	time_t next_ota_time;
	time_t next_time_update;
	uint32_t cycles;
	bool keys_registered;
	float hall;
	float temperature;
};


/*!
 * Instance of system status
 */
struct system_status status;


/*
 * Instance of rtc time value
 */
static RTC_DATA_ATTR struct timeval sleep_enter_time;


/*!
 * Initialize the system
 * @return ESP_OK, if everything alright,
 * @return ESP_ERROR... if error occurs
 */
esp_err_t init_system() {
	//todo
	init_nvs();
	sensor_setup();
	set_hw_ID();
	check_key_status();

	// create the main event group for task synchronization
	if (!main_event_group) {
		main_event_group = xEventGroupCreate();
		if (!main_event_group) {
			ESP_LOGE(__func__, "Main Event Group Create Failed!");
			return ESP_FAIL;
		}
	}
	return ESP_OK;
}


/*!
 * Get the wakeup reason.
 *
 * @return ESP_OK TODO maybe build in error checking
 */
esp_err_t get_wakeup_reason() {
	// get the time difference from going to sleep, until waking up
	struct timeval now;
	gettimeofday(&now, NULL);
	int sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;
	ESP_LOGI(__func__, "Time spent in deep sleep: %dms", sleep_time_ms);

	// get the reason for the wakeup and resume the program accordingly
	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	switch (cause) {
		case ESP_SLEEP_WAKEUP_UNDEFINED:    //!< In case of deep sleep, reset was not caused by exit from deep sleep
			ESP_LOGI(__func__, "RESET Button");
			// this is where the program enters after reset button
			status.cycles = 0;
			status.next_measurement_time = 0;
			status.keys_registered = false;
			status.time = 0;
			status.next_time_update = 0;
			break;
		case ESP_SLEEP_WAKEUP_ALL:          //!< Not a wakeup cause, used to disable all wakeup sources with esp_sleep_disable_wakeup_source
			ESP_LOGI(__func__, "all");
			break;
		case ESP_SLEEP_WAKEUP_EXT0:         //!< Wakeup caused by external signal using RTC_IO
			ESP_LOGI(__func__, "ext0");
			break;
		case ESP_SLEEP_WAKEUP_EXT1:         //!< Wakeup caused by external signal using RTC_CNTL
			ESP_LOGI(__func__, "ext1");
			break;
		case ESP_SLEEP_WAKEUP_TIMER:        //!< Wakeup caused by timer
			ESP_LOGI(__func__, "timer");
			// this is where the program enters after esp_deep_sleep
			struct system_status *p_status = &status;
			size_t s_status = sizeof(status);
			ESP_ERROR_CHECK(kv_load("status-region", "status-key", (void **) &p_status, &s_status));
			break;
		case ESP_SLEEP_WAKEUP_TOUCHPAD:     //!< Wakeup caused by touchpad
			ESP_LOGI(__func__, "touchpad");
			break;
		case ESP_SLEEP_WAKEUP_ULP:          //!< Wakeup caused by ULP program
			ESP_LOGI(__func__, "ulp");
			break;
		case ESP_SLEEP_WAKEUP_GPIO:         //!< Wakeup caused by GPIO (light sleep only)
			ESP_LOGI(__func__, "gpio");
			break;
		case ESP_SLEEP_WAKEUP_UART:         //!< Wakeup caused by UART (light sleep only)
			ESP_LOGI(__func__, "uart");
			break;
		default:
			ESP_LOGI(__func__, "default");
			break;
	}
	// TODO
	return ESP_OK;
}


/*!
 * Task to go to sleep.
 */
static void sleep_task(void __unused *pvParameters) {
	EventBits_t event_bits;
	for (;;) {
		event_bits = xEventGroupWaitBits(main_event_group, MAIN_SLEEP_READY,
		                                 false, false, portMAX_DELAY);
		// if the corresponding flag is set, go to sleep, until then wait
		if (event_bits & MAIN_SLEEP_READY) {
			// store the status information
			ESP_ERROR_CHECK(kv_store("status-region", "status-key", (void *) &status, sizeof(status)));
			// save the time before sleeping and go to sleep
			ESP_LOGI(__func__, "going to sleep");
			gettimeofday(&sleep_enter_time, NULL);
			esp_deep_sleep(SLEEP_INTERVAL);
		}
	}
}

/*!
 * Task to Send the data to the backend.
 */
static void send_data_task(void __unused *pvParameter) {
	EventBits_t event_bits;
	for (;;) {
//		ESP_LOGI(__func__, "ping");
		event_bits = xEventGroupWaitBits(main_event_group, (MAIN_SEND_READY),
		                                 false, false, portMAX_DELAY);
		if (event_bits & (MAIN_SEND_READY)) {
//			ESP_LOGI(__func__, "doing");
			send_message(status.hall, status.temperature);
			xEventGroupSetBits(main_event_group, MAIN_SLEEP_READY);
			vTaskDelete(NULL);
		}
	}
}

/*!
 * Task to perform a measurement
 */
static void sensor_task(void __unused *pvParameters) {
	EventBits_t event_bits;

	for (;;) {
		event_bits = xEventGroupWaitBits(main_event_group, (MAIN_TIME_READY),
		                                 false, false, portMAX_DELAY);
		if (event_bits & (MAIN_TIME_READY)) {
			sensor_loop_float(&status.hall, &status.temperature);
			xEventGroupSetBits(main_event_group, MAIN_SEND_READY);
			status.next_measurement_time = status.time + INTERVAL_MEASURE;
			vTaskDelete(NULL);
		}
	}
}


/*!
 * Wifi task to connect to the wifi network
 */
static void wifi_task(void __unused *pvParameters) {
	init_wifi();
	for (;;) {
		ESP_LOGI(__func__, "connecting to wifi");
		struct Wifi_login wifi;

		esp_err_t err = kv_load("wifi_data", "wifi_ssid", (void **) &wifi.ssid, &wifi.ssid_length);
		if (err == ESP_OK) {
			ESP_LOGD(__func__, "SSID: %.*s", wifi.ssid_length, wifi.ssid);
			kv_load("wifi_data", "wifi_pwd", (void **) &wifi.pwd, &wifi.pwd_length);
			ESP_LOGD(__func__, "PASS: %.*s", wifi.pwd_length, wifi.pwd);
			if (wifi_join(wifi, 5000) == ESP_OK) {
				ESP_LOGI(__func__, "established");
			} else { // no connection possible
				ESP_LOGW(__func__, "no valid Wifi");
			}
			free(wifi.ssid);
			free(wifi.pwd);
		} else {  // no WiFi connection
			ESP_LOGW(__func__, "no Wifi login data");
		}
		vTaskDelete(NULL);
	}
}


/*!
 * Task to enter the console
 * @param pvParameters are currently not used, but part of the task declaration.
 */
static void enter_console_task(void __unused *pvParameter) {
	char c;
	init_console();

	for (;;) {
		c = (char) fgetc(stdin);
		printf("%02x\r", c);
		if ((c == 0x03) || (c == 0x15)) {  //0x03 = Ctrl + C      0x15 = Ctrl + U
			// If Ctrl + C was pressed, enter the console and suspend the other tasks until console exits.
			if (wifi_task_handle) vTaskSuspend(wifi_task_handle);
			if (sensor_task_handle) vTaskSuspend(sensor_task_handle);
			if (sleep_task_handle) vTaskSuspend(sleep_task_handle);
			if (key_register_handle) vTaskSuspend(key_register_handle);
			if (send_data_handle) vTaskSuspend(send_data_handle);
			if (rtc_wdt_is_on()) rtc_wdt_protect_off();

			run_console();

			if (rtc_wdt_is_on()) rtc_wdt_protect_on();
			if (send_data_handle) vTaskResume(send_data_handle);
			if (key_register_handle) vTaskResume(key_register_handle);
			if (sleep_task_handle) vTaskResume(sleep_task_handle);
			if (sensor_task_handle) vTaskResume(sensor_task_handle);
			if (wifi_task_handle) vTaskResume(wifi_task_handle);
		}
	}
}

/*!
 * Task to Update the system time every 6 hours.
 */
static void update_time_task(void __unused *pvParameter) {
	EventBits_t event_bits;

	for (;;) {
		event_bits = xEventGroupWaitBits(network_event_group, (NETWORK_ETH_READY | NETWORK_STA_READY),
		                                 false, false, portMAX_DELAY);
		if (event_bits & (NETWORK_ETH_READY | NETWORK_STA_READY)) {
			// check that we have current time before trying to generate/register keys
			struct tm timeinfo = {0};
			time(&status.time);
			localtime_r(&status.time, &timeinfo);
			if (timeinfo.tm_year >= (2019 - 1900)) {
				status.next_time_update = status.time + INTERVAL_TIME;
				xEventGroupSetBits(main_event_group, MAIN_TIME_READY);
				vTaskDelete(NULL);  // delay this task for the next 6 hours
			} else if (status.time >= status.next_time_update) {
				sntp_update();
			}
		}
	}
}


/*!
 * Task to check for a firmware update every 12 hours.
 */
static void ota_task(void __unused *pvParameter) {
	EventBits_t event_bits;

	for (;;) {
		event_bits = xEventGroupWaitBits(network_event_group, (NETWORK_ETH_READY | NETWORK_STA_READY),
		                                 false, false, portMAX_DELAY);
		if (event_bits & (NETWORK_ETH_READY | NETWORK_STA_READY)) {
			// check for firmware update
			ubirch_firmware_update();
			status.next_ota_time = status.time + INTERVAL_OTA;
			xEventGroupSetBits(main_event_group, MAIN_SLEEP_READY);
			vTaskDelete(NULL);
		}
	}
}


/*!
 * Task to register the keys at the backend.
 */
static void key_register_task(void __unused *pvParameter) {
	EventBits_t event_bits;

	for (;;) {
		event_bits = xEventGroupWaitBits(main_event_group, (MAIN_TIME_READY),
		                                 false, false, portMAX_DELAY);
		if (event_bits & (MAIN_TIME_READY)) {
			if (status.keys_registered == false) {
				status.keys_registered = true;
				register_keys();
			}
		}
		vTaskDelete(NULL);
	}
}


/*!
 * scheduled sensor functionality
 */
static void sensor_schedule() {
	xTaskCreate(&wifi_task, "wifi", 4096, NULL, 7, &wifi_task_handle);
	xTaskCreate(&enter_console_task, "console", 4096, NULL, 8, &console_handle);
	xTaskCreate(&sensor_task, "sensor", 4096, NULL, 5, &sensor_task_handle);
	xTaskCreate(&sleep_task, "sleep", 4096, NULL, 4, &sleep_task_handle);
	xTaskCreate(&key_register_task, "key_reg", 8192, NULL, 6, &key_register_handle);
	xTaskCreate(&send_data_task, "send_data", 8192, NULL, 6, &send_data_handle);
	xTaskCreate(&update_time_task, "sntp", 4096, NULL, 9, &update_time_handle);
	ESP_LOGI(__func__, "all tasks created");
	vTaskDelay(100);
}

/*!
 * Scheduled sleep functionality, if nothing is to do
 */
static void sleep_schedule() {
	xTaskCreate(&sleep_task, "sleep", 4096, NULL, 9, &sleep_task_handle);
	xEventGroupSetBits(main_event_group, MAIN_SLEEP_READY);
}

/*!
 * Scheduled Firmware update functionality
 */
static void ota_schedule() {
	xTaskCreate(&wifi_task, "wifi", 4096, NULL, 7, &wifi_task_handle);
	vTaskDelay(100);
	xTaskCreate(&ota_task, "sensor", 8192, NULL, 5, &ota_task_handle);
	xTaskCreate(&sleep_task, "sleep", 4096, NULL, 4, &sleep_task_handle);
}


/*!
 * Check the schedule and perform tasks
 */
static void check_schedule() {
	struct timeval now;
	gettimeofday(&now, NULL);
	time_t current_time = (now.tv_sec);

	// check the schedule after priority
	//check for OTA
	if (current_time >= (status.next_ota_time)) {
		ota_schedule();
		ESP_LOGI(__func__, "OTA:current time = (%ld s), next time = (%ld s) ", current_time, status.next_ota_time);
	}
		// time to measure?
	else if (current_time >= (status.next_measurement_time)) {
		sensor_schedule();
		ESP_LOGI(__func__, "MEAS:current time = (%ld s), next time = (%ld s) ", current_time,
		         status.next_measurement_time);
	}
		// nothing to do
	else {
		ESP_LOGI(__func__, "nothing to do");
		sleep_schedule();
	}
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

/*!
 * Main function
 */
void app_main() {
	//todo
	ESP_LOGI(__func__, "start");
	// initialize the system
	ESP_ERROR_CHECK(init_system());
	get_wakeup_reason();
	status.cycles++;
	ESP_LOGI(__func__, "wakeup cycles = %d", status.cycles);

	// check the tasks to perform
	check_schedule();
}

#pragma GCC diagnostic pop
