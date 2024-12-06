#include <stdio.h>
#include <string.h>
#include "wmsdk_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON/cJSON.h"
#include "wm_nvs.h"
#include "wm_key_config.h"
#include "wm_ota_http.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "wm_partition_table.h"
#include "wm_drv_flash.h"

#define LOG_TAG "ota"
#include "wm_log.h"

#define CONFIG_OTA_PROGRESS_REPORT_THRESHOLD 10

typedef struct {
    uint32_t uplink_msg_id;            /**< Message identifier for uplink communication. */
    uint32_t downlink_msg_id;          /**< Message identifier for downlink communication. */
    bool is_not_first_downlink;        /**< Flag to indicate if this is not the first downlink message. */
    char *sign_data;                   /**< Pointer to the signature data for verifying the firmware integrity. */
    size_t sign_size;                  /**< Size of the signature data in bytes. */
    wm_ota_http_cfg_t http_cfg;        /**< Configuration for HTTP operations related to the OTA process. */
    mbedtls_sha256_context sha256_ctx; /**< SHA-256 context for computing the hash of the OTA firmware file. */
} ctx_t;

static ctx_t g_ctx = { 0 }; /**< Global OTA context instance */

int set_firmware_type(char *fw_type)
{
    int ret = WM_ERR_SUCCESS;
    wm_nvs_handle_t handle;

    if (fw_type == NULL) {
        return WM_ERR_INVALID_PARAM;
    }

    ret = wm_nvs_open(WM_NVS_DEF_PARTITION, WM_GROUP_NETWORK, WM_NVS_OP_READ_WRITE, &handle);
    if (ret != WM_ERR_SUCCESS) {
        return ret;
    }

    ret = wm_nvs_set_str(handle, "fw_type", fw_type);
    if (ret != WM_ERR_SUCCESS) {
        wm_nvs_close(handle);
        return ret;
    }

    wm_nvs_close(handle);

    return ret;
}

static int ota_progress_report(wm_ota_state_t *ota_state)
{
    int ret = WM_ERR_SUCCESS;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "msg_sequence", g_ctx.uplink_msg_id);
    // TODO: Waiting for RTC Driver to obtain time
    cJSON_AddNumberToObject(root, "time", xTaskGetTickCount() * portTICK_PERIOD_MS);
    cJSON *data = cJSON_CreateObject();

    switch (ota_state->status) {
        case WM_OTA_STATUS_CONNECTION_FAILED:
        {
            cJSON_AddNumberToObject(data, "errorCode", WM_OTA_STATUS_CONNECTION_FAILED);
            cJSON_AddStringToObject(data, "errorMsg", "OTA connection failed");
            break;
        }
        case WM_OTA_STATUS_DOWNLOAD_FAILED:
        {
            cJSON_AddNumberToObject(data, "errorCode", WM_OTA_STATUS_DOWNLOAD_FAILED);
            cJSON_AddStringToObject(data, "errorMsg", "OTA download failed");
            break;
        }
        case WM_OTA_STATUS_UPGRADE_FAILED:
        {
            cJSON_AddNumberToObject(data, "errorCode", WM_OTA_STATUS_UPGRADE_FAILED);
            cJSON_AddStringToObject(data, "errorMsg", "OTA upgrade failed");
            break;
        }
        case WM_OTA_STATUS_ABORT:
        {
            cJSON_AddNumberToObject(data, "errorCode", WM_OTA_STATUS_ABORT);
            cJSON_AddStringToObject(data, "errorMsg", "OTA abort");
            break;
        }
        case WM_OTA_STATUS_DOWNLOAD_START:
        {
            cJSON_AddNumberToObject(data, "progress", ota_state->progress);
            break;
        }
        default:
        {
            break;
        }
    }

    cJSON_AddItemToObject(root, "data", data);
    char *out = cJSON_Print(root);
    wm_log_debug("%s", out);

    if (ret >= 0) {
        g_ctx.uplink_msg_id += 1;
    }

    free(out);
    cJSON_Delete(root);

    return ret;
}

static void ota_get_file_callback(uint8_t *data, uint32_t len)
{
    if (0 != mbedtls_sha256_update(&g_ctx.sha256_ctx, data, len)) {
        mbedtls_sha256_free(&g_ctx.sha256_ctx);
    }
}

static void ota_state_callback(wm_ota_state_t *ota_state)
{
    int ret                     = WM_ERR_SUCCESS;
    uint8_t ota_file_sha256[32] = { 0 };
    wm_device_t *flash_dev      = NULL;

    //TODO, it should be replace by WM_GET_DEVICE_BY_NAME after DT complete
    flash_dev = wm_dt_get_device_by_name("iflash");

    wm_log_debug("OTA progress:%d-%d%%.", ota_state->status, ota_state->progress);
    if (ota_state->status == WM_OTA_STATUS_CONNECTION_FAILED || ota_state->status == WM_OTA_STATUS_DOWNLOAD_FAILED ||
        ota_state->status == WM_OTA_STATUS_UPGRADE_FAILED || ota_state->status == WM_OTA_STATUS_ABORT) {
        wm_log_info("OTA failed.");
    }

    if (ota_state->status == WM_OTA_STATUS_UPGRADE_START) {
        wm_log_info("OTA finished.");
        wm_log_info("This example succeeds in running.");
    }

    if (ota_state->status == WM_OTA_STATUS_CONNECTION_FAILED || ota_state->status == WM_OTA_STATUS_DOWNLOAD_FAILED ||
        ota_state->status == WM_OTA_STATUS_UPGRADE_FAILED || ota_state->status == WM_OTA_STATUS_ABORT ||
        (ota_state->status == WM_OTA_STATUS_DOWNLOAD_START &&
         ota_state->progress % CONFIG_OTA_PROGRESS_REPORT_THRESHOLD == 0)) {
        ota_progress_report(ota_state);
    }

    if (ota_state->status == WM_OTA_STATUS_CONNECTED) {
        mbedtls_sha256_init(&g_ctx.sha256_ctx);
        ret = mbedtls_sha256_starts(&g_ctx.sha256_ctx, false);
        if (0 != WM_ERR_SUCCESS) {
            mbedtls_sha256_free(&g_ctx.sha256_ctx);
        }
    }

    if (ota_state->status == WM_OTA_STATUS_DOWNLOADED) {
        mbedtls_sha256_finish(&g_ctx.sha256_ctx, ota_file_sha256);
        mbedtls_sha256_free(&g_ctx.sha256_ctx);
        if (ret) {
            wm_log_error("SHA-256 ECDSA signature verification failed, ret = %d", ret);
            wm_partition_item_t app_ota_partition = { 0 };
            ret                                   = wm_partition_table_find("app_ota", &app_ota_partition);
            if (ret != WM_ERR_SUCCESS) {
                wm_log_error("Failed to find 'app_ota' partition.");
            }
            ret = wm_drv_flash_write(flash_dev, app_ota_partition.offset, (void *)0xFFFFFFFF, 4);
            if (ret != WM_ERR_SUCCESS) {
                wm_log_error("Failed to write to the flash device.");
            }
        } else {
            wm_log_info("SHA-256 ECDSA signature verification successful.");
            ret = set_firmware_type("UPDATE");
            if (ret != WM_ERR_SUCCESS) {
                wm_log_error("Error setting firmware type to UPDATE.");
            }
            wm_ota_ops_reboot();
        }
        wm_os_internal_free(g_ctx.sign_data);
    }
}

int ota_parse_update(char *payload)
{
    int ret               = WM_ERR_SUCCESS;
    char sw_version[16]   = { 0 };
    size_t sign_data_size = 0;

    cJSON *json = cJSON_Parse(payload);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            wm_log_error("cJSON error: %s", error_ptr);
        }
        return WM_ERR_FAILED;
    }
    if (g_ctx.downlink_msg_id != cJSON_GetObjectItem(json, "msg_sequence")->valueint || g_ctx.is_not_first_downlink == false) {
        g_ctx.downlink_msg_id = cJSON_GetObjectItem(json, "msg_sequence")->valueint;
    } else {
        wm_log_error("OTA message sequence duplication");
        goto exit;
    }

    cJSON *data_item = cJSON_GetObjectItem(json, "downloadUrl");
    if (data_item != NULL) {
        wm_log_info("Version: %f", cJSON_GetObjectItem(json, "version")->valuedouble);
        wm_log_info("HTTPS URL: %s", cJSON_GetObjectItem(json, "downloadUrl")->valuestring);

        ret = wm_ota_ops_get_version(sw_version);
        if (ret != WM_ERR_SUCCESS) {
            goto exit;
        }
        if (!strcmp(cJSON_GetObjectItem(json, "version")->valuestring, sw_version)) {
            wm_log_warn("OTA version error");
            // return WM_ERR_OTA_SAME_VERSION;
        }

        g_ctx.http_cfg.fw_url          = cJSON_GetObjectItem(json, "downloadUrl")->valuestring;
        g_ctx.http_cfg.ota_get_file_cb = ota_get_file_callback;
        g_ctx.http_cfg.ota_state_cb    = ota_state_callback;
        g_ctx.http_cfg.reboot          = 0;

        // TODO: Upgrade_delay is converted to RTC time, an RTC Alarm is created, and in the Alarm callback, wm_ota_htp_update is executed
        ret = wm_ota_http_update(&g_ctx.http_cfg);
        if (ret != WM_ERR_SUCCESS) {
            wm_log_warn("OTA http update error: %d", ret);
            goto exit;
        }

        sign_data_size  = strlen(cJSON_GetObjectItem(data_item, "sig-sha256-ecdsa")->valuestring) * 3 / 4;
        g_ctx.sign_data = (char *)wm_os_internal_malloc(sign_data_size);
        if (g_ctx.sign_data) {
            ret =
                mbedtls_base64_decode((unsigned char *)g_ctx.sign_data, sign_data_size, &g_ctx.sign_size,
                                      (const unsigned char *)(cJSON_GetObjectItem(data_item, "sig-sha256-ecdsa")->valuestring),
                                      strlen(cJSON_GetObjectItem(data_item, "sig-sha256-ecdsa")->valuestring));
            if (ret != WM_ERR_SUCCESS) {
                wm_log_error("Base64 decode error: %d", ret);
                goto exit;
            }
        }
    }

exit:
    cJSON_Delete(json);

    return ret;
}
