#include <stdio.h>
#include <math.h>
#include <string.h>
#include "wm_event.h"
#include "wm_wifi.h"
#include "wm_nvs.h"
#include "wm_utils.h"
#include "wm_mqtt_client.h"
#include "wm_drv_gpio.h"
#include "wm_cli.h"
#include "lwipopts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "cJSON/cJSON.h"
#include "fastbee.h"

#define LOG_TAG "fastbee"
#include "wm_log.h"

#define BROKER_URI            "mqtt://iot.fastbee.cn:1883"

#define USER_NAME             "FastBee"
#define PASSWORD              "P7HJN19H8KM860Z2"

#define PRODUCT_ID            "1817"
#define DEVICE_ID             "D6963KC6KY06"

#define CLIENT_ID             "S&" DEVICE_ID "&" PRODUCT_ID "&1"

#define TOPIC_OTA_UPDATE      "/" PRODUCT_ID "/" DEVICE_ID "/ota/get"
#define TOPIC_MQTT_SUBSCRIBE  "/" PRODUCT_ID "/" DEVICE_ID "/function/get"

#define TOPIC_DEV_INFO_GET    "/" PRODUCT_ID "/" DEVICE_ID "/info/get"
#define TOPIC_MONITOR_GET     "/" PRODUCT_ID "/" DEVICE_ID "/monitor/get"

#define TOPIC_MQTT_PUBLISH    "/" PRODUCT_ID "/" DEVICE_ID "/property/post"
#define TOPIC_MONITOR_PUBLISH "/" PRODUCT_ID "/" DEVICE_ID "/monitor/post"

#define TASK_STACK           512
#define TASK_PRIO            2

#define QUEUE_MSG_TYPE_KEY   1
#define QUEUE_MSG_TYPE_MQTT  2
#define QUEUE_MSG_TYPE_PUB   3

static QueueHandle_t work_queue = NULL;
static wm_mqtt_client_handle_t mqtt_client;

static int get_value_from_json(const char *json_string)
{
    cJSON *json = cJSON_Parse(json_string);
    if (json == NULL) {
        return -1;
    }

    if (!cJSON_IsArray(json)) {
        cJSON_Delete(json);
        return -1;
    }

    cJSON *json_item = cJSON_GetArrayItem(json, 0);
    if (json_item == NULL) {
        cJSON_Delete(json);
        return -1;
    }

    cJSON *json_value = cJSON_GetObjectItem(json_item, "value");
    if (json_value == NULL) {
        cJSON_Delete(json);
        return -1;
    }

    if (!cJSON_IsString(json_value)) {
        cJSON_Delete(json);
        return -1;
    }

    int value = atoi(json_value->valuestring);

    cJSON_Delete(json);

    return value;
}

void led_state_upload(int state)
{
    char message[128];

    sprintf(message, "[{\"id\":\"switch\",\"remark\":\"\",\"value\":\"%d\"}]", state);

    if (mqtt_client) {
        wm_log_info("publish message is %s", message);

        wm_mqtt_client_publish(mqtt_client, TOPIC_MQTT_PUBLISH, message, 0, 1, 0);
    }
}

static void event_handler_got_ip(wm_event_group_t group, int event, wm_lwip_event_data_t *data, void *priv)
{
    if (event == WM_EVENT_ETH_GOT_IP) {
        int msg = QUEUE_MSG_TYPE_MQTT;
        xQueueSend(work_queue, &msg, 0);
    }
}

static void event_handler_mqtt_connected(wm_mqtt_event_data_t *data, void *priv)
{
    int msg = QUEUE_MSG_TYPE_PUB;

    wm_log_info("MQTTS connected");

    xQueueSend(work_queue, &msg, 0);
}

static void event_handler_mqtt_disconnected(wm_mqtt_event_data_t *data, void *priv)
{
    int msg = QUEUE_MSG_TYPE_MQTT;

    wm_log_info("MQTTS disconnected");

    xQueueSend(work_queue, &msg, 0);
}

static void event_handler_mqtt_recved_data(wm_mqtt_event_data_t *data, void *priv)
{
    int ret = WM_ERR_SUCCESS;

    wm_log_info("MQTT recved");
    wm_log_info("Topic: %.*s", data->mqtt_client_data_info.topic_len, data->mqtt_client_data_info.topic);
    wm_log_info("Payload: %.*s", data->mqtt_client_data_info.payload_len, data->mqtt_client_data_info.payload);

    if (strstr(data->mqtt_client_data_info.topic, TOPIC_OTA_UPDATE) /*||
        strstr(data->mqtt_client_data_info.topic, TOPIC_OTA_RESPONSE)*/) {
        ret = ota_parse_update(data->mqtt_client_data_info.payload);
        if (ret != WM_ERR_SUCCESS) {
            wm_log_error("OTA update parse error: %d", ret);
        }
    } else if (strstr(data->mqtt_client_data_info.topic, TOPIC_MQTT_SUBSCRIBE)) {
        int value = get_value_from_json(data->mqtt_client_data_info.payload);

        if (strstr(data->mqtt_client_data_info.payload, "\"id\":\"switch\""))
        {
            if (value)
                led_on();
            else
                led_off();
        }
        else if (strstr(data->mqtt_client_data_info.payload, "\"id\":\"red\""))
        {
            if (value)
                led_red();
            else
                led_off();
            //led_state_upload(led_state());
        }
        else if (strstr(data->mqtt_client_data_info.payload, "\"id\":\"green\""))
        {
            if (value)
                led_green();
            else
                led_off();
            //led_state_upload(led_state());
        }
        else if (strstr(data->mqtt_client_data_info.payload, "\"id\":\"blue\""))
        {
            if (value)
                led_blue();
            else
                led_off();
            //led_state_upload(led_state());
        }
    }
}

static void event_handler_mqtt_event(wm_event_group_t group, int event, wm_mqtt_event_data_t *data, void *priv)
{
    switch (event) {
        case WM_EVENT_MQTT_CLIENT_CONNECTED:
            event_handler_mqtt_connected(data, priv);
            break;
        case WM_EVENT_MQTT_CLIENT_DISCONNECTED:
            event_handler_mqtt_disconnected(data, priv);
            break;
        case WM_EVENT_MQTT_CLIENT_DATA:
            event_handler_mqtt_recved_data(data, priv);
            break;
        default:
            break;
    }
}

static int mqtt_connect(void)
{
    int ret = WM_ERR_SUCCESS;

    wm_mqtt_client_config_t mqtt_cfg = {
        .uri       = BROKER_URI,
        .client_id = CLIENT_ID,
        .username  = USER_NAME,
        .password  = PASSWORD,
    };

    if (mqtt_client) {
        wm_log_info("mqtt reconnect...");
        wm_mqtt_client_reconnect(mqtt_client);
    } else {
        mqtt_client = wm_mqtt_client_init(&mqtt_cfg);
        if (mqtt_client) {
            wm_event_add_callback(WM_MQTT_EV, WM_EVENT_ANY_TYPE, (wm_event_callback)event_handler_mqtt_event, mqtt_client);

            wm_log_info("mqtt connect...");
            ret = wm_mqtt_client_connect(mqtt_client);
            if (ret != WM_ERR_SUCCESS) {
                wm_log_error("Failed to connect MQTT client.");
            }
        } else {
            wm_log_error("Failed to initialize MQTT client.");
            return WM_ERR_FAILED;
        }
    }

    return ret;
}

static void work_task(void *param)
{
    void *msg = NULL;

    while (1) {
        if (xQueueReceive(param, (void **)&msg, portMAX_DELAY) == pdTRUE) {
            switch ((int)msg) {
                case QUEUE_MSG_TYPE_KEY:
                {
                    //uint32_t time = xTaskGetTickCount();
                    //while (0 == wm_drv_gpio_data_get(0)) {
                    //    vTaskDelay(1);
                    //}
                    //if ((xTaskGetTickCount() - time) <= pdMS_TO_TICKS(500))
                    {
                        //short press <= 500ms
                        if (led_state()) {
                            wm_log_info("led turn off");
                            led_off();
                        } else {
                            wm_log_info("led turn on");
                            led_on();
                        }
                        led_state_upload(led_state());
                    }
                    break;
                }
                case QUEUE_MSG_TYPE_MQTT:
                {
                    mqtt_connect();
                    break;
                }
                case QUEUE_MSG_TYPE_PUB:
                {
                    set_firmware_type("INIT");

                    wm_mqtt_client_subscribe(mqtt_client, TOPIC_OTA_UPDATE, 1);
                    wm_mqtt_client_subscribe(mqtt_client, TOPIC_MQTT_SUBSCRIBE, 0);

                    led_state_upload(led_state());
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
    }
}

void send_key_to_fastbee(void)
{
    int msg = QUEUE_MSG_TYPE_KEY;

    if (work_queue)
        xQueueSend(work_queue, &msg, 0);
}

static void cmd_fastbee(int argc, char *argv[])
{
    int msg;
    static uint8_t fastbee_inited = 0;

    if (fastbee_inited)
        return;

    fastbee_inited = 1;

    work_queue = xQueueCreate(16, sizeof(void *));
    xTaskCreate(work_task, "fastbee", TASK_STACK, work_queue, TASK_PRIO, NULL);

    wm_event_add_callback(WM_LWIP_EV, WM_EVENT_ANY_TYPE, (wm_event_callback)event_handler_got_ip, NULL);

    msg = QUEUE_MSG_TYPE_MQTT;
    xQueueSend(work_queue, &msg, 0);

    return;
}
WM_CLI_CMD_DEFINE(fastbee, cmd_fastbee, fastbee cmd, fastbee -- connect to fastbee cloud); //cppcheck # [syntaxError]
