/*
 * SPDX-FileCopyrightText: 2021-2022 Nordic Semiconductor ASA
 * SPDX-FileContributor: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_audio_lc3_defs.h"
#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_pacs_api.h"
#include "esp_ble_audio_bap_lc3_preset_defs.h"

#include "ble_audio_example_init.h"
#include "ble_audio_example_utils.h"

#include "adv.h"
#include <math.h>
#include "encoder/esp_audio_enc.h"
#include "encoder/impl/esp_lc3_enc.h"

ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_2_1_DEFINE(preset_active,   /* JC: 48 kHz, 10 ms, 100 B: a real Auracast setting */
                                                     ESP_BLE_AUDIO_LOCATION_FRONT_LEFT |
                                                     ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

#define LOCAL_DEVICE_NAME      "JC Peer Cast"
#define LOCAL_DEVICE_NAME_LEN  (sizeof(LOCAL_DEVICE_NAME) - 1)

#define LOCAL_BROADCAST_CODE   ""       /* JC: unencrypted, like most public broadcasts */
#define LOCAL_BROADCAST_ID     0x123456

#define STREAM_COUNT            CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT
#define SUBGROUP_COUNT          CONFIG_BT_BAP_BROADCAST_SRC_SUBGROUP_COUNT

static struct broadcast_source_stream {
    esp_ble_audio_bap_stream_t stream;
    uint16_t seq_num;
    uint8_t *data;
    example_audio_tx_scheduler_t scheduler;
} streams[STREAM_COUNT];

static esp_ble_audio_bap_broadcast_source_t *broadcast_source;

static void broadcast_source_tx(struct broadcast_source_stream *source_stream);

static int stream_index(const esp_ble_audio_bap_stream_t *stream)
{
    for (size_t i = 0; i < ARRAY_SIZE(streams); i++) {
        if (&streams[i].stream == stream) {
            return (int)i;
        }
    }
    return -1;
}

static void stream_started_cb(esp_ble_audio_bap_stream_t *stream)
{
    struct broadcast_source_stream *source_stream = CONTAINER_OF(stream,
                                                                 struct broadcast_source_stream,
                                                                 stream);
    int idx = stream_index(stream);
    esp_err_t err;

    ESP_LOGI(TAG, "[SRC #%d] Stream started", idx);

    if (source_stream->stream.qos == NULL || source_stream->stream.qos->sdu == 0) {
        ESP_LOGE(TAG, "[SRC #%d] Invalid QoS", idx);
        return;
    }

    if (source_stream->data == NULL) {
        source_stream->data = calloc(1, source_stream->stream.qos->sdu);
        if (source_stream->data == NULL) {
            ESP_LOGE(TAG, "[SRC #%d] Failed to alloc TX buffer (sdu %u)",
                     idx, source_stream->stream.qos->sdu);
            return;
        }
    }

    source_stream->seq_num = 0;
    example_audio_tx_scheduler_reset(&source_stream->scheduler);

    /* Note: esp timer is not accurate enough */
    err = example_audio_tx_scheduler_start(&source_stream->scheduler, preset_active.qos.interval);
    if (err) {
        ESP_LOGE(TAG, "[SRC #%d] Scheduler start failed, err %d", idx, err);
        return;
    }

    broadcast_source_tx(source_stream);
}

static void stream_stopped_cb(esp_ble_audio_bap_stream_t *stream, uint8_t reason)
{
    struct broadcast_source_stream *source_stream = CONTAINER_OF(stream,
                                                                 struct broadcast_source_stream,
                                                                 stream);
    int idx = stream_index(stream);
    esp_err_t err;

    ESP_LOGI(TAG, "[SRC #%d] Stream stopped, reason 0x%02x", idx, reason);

    err = example_audio_tx_scheduler_stop(&source_stream->scheduler);
    if (err) {
        ESP_LOGE(TAG, "[SRC #%d] Scheduler stop failed, err %d", idx, err);
    }
}

static void stream_disconnected_cb(esp_ble_audio_bap_stream_t *stream, uint8_t reason)
{
    struct broadcast_source_stream *source_stream = CONTAINER_OF(stream,
                                                                 struct broadcast_source_stream,
                                                                 stream);
    int idx = stream_index(stream);
    esp_err_t err;

    ESP_LOGI(TAG, "[SRC #%d] ISO disconnected, reason 0x%02x", idx, reason);

    err = example_audio_tx_scheduler_stop(&source_stream->scheduler);
    if (err) {
        ESP_LOGE(TAG, "[SRC #%d] Scheduler stop failed, err %d", idx, err);
    }
}

static void stream_sent_cb(esp_ble_audio_bap_stream_t *stream, void *user_data)
{
    struct broadcast_source_stream *source_stream = CONTAINER_OF(stream,
                                                                 struct broadcast_source_stream,
                                                                 stream);
    char name[24];

    snprintf(name, sizeof(name), "SRC #%d", stream_index(stream));
    example_audio_tx_scheduler_on_sent(&source_stream->scheduler, user_data, TAG, name);
}

static esp_ble_audio_bap_stream_ops_t stream_ops = {
    .started      = stream_started_cb,
    .stopped      = stream_stopped_cb,
    .sent         = stream_sent_cb,
    .disconnected = stream_disconnected_cb,
};

static bool stream_is_streaming(const esp_ble_audio_bap_stream_t *stream)
{
    esp_ble_audio_bap_ep_info_t ep_info = {0};

    if (stream->ep == NULL) {
        return false;
    }

    if (esp_ble_audio_bap_ep_get_info(stream->ep, &ep_info)) {
        return false;
    }

    return (ep_info.state == ESP_BLE_AUDIO_BAP_EP_STATE_STREAMING);
}

static void broadcast_source_tx(struct broadcast_source_stream *source_stream)
{
    esp_err_t err;

    if (!stream_is_streaming(&source_stream->stream)) {
        return;
    }

    int idx = stream_index(&source_stream->stream);

    if (source_stream->stream.qos == NULL || source_stream->stream.qos->sdu == 0) {
        ESP_LOGE(TAG, "[SRC #%d] Invalid QoS", idx);
        return;
    }

    if (source_stream->data == NULL) {
        ESP_LOGE(TAG, "[SRC #%d] TX buffer unavailable (sdu %u)",
                 idx, source_stream->stream.qos->sdu);
        return;
    }

    /* JC: real LC3 of a sine (440 Hz left, 660 Hz right) instead of filler bytes. One second (100 frames) is encoded
     * once and looped: 440 and 660 Hz fit whole cycles into a second, and encoding two streams live in this callback
     * could not keep up (each stream sent ~60 % of its intervals). */
    {
        static uint8_t *pre[STREAM_COUNT];
        static uint32_t at[STREAM_COUNT];
        uint16_t sdu = source_stream->stream.qos->sdu;
        if (!pre[idx]) {
            pre[idx] = malloc((size_t)sdu * 100);
            esp_audio_enc_handle_t enc = NULL;
            esp_lc3_enc_config_t lc = ESP_LC3_ENC_CONFIG_DEFAULT();
            lc.sample_rate = 48000; lc.channel = 1; lc.frame_dms = 100; lc.nbyte = sdu;
            esp_audio_enc_config_t ec = { .type = ESP_AUDIO_TYPE_LC3, .cfg = &lc, .cfg_sz = sizeof lc };
            esp_lc3_enc_register();
            if (!pre[idx] || esp_audio_enc_open(&ec, &enc) != ESP_AUDIO_ERR_OK) { ESP_LOGE(TAG, "JC lc3 open failed"); return; }
            static int16_t pcm[480];
            double f = idx ? 660.0 : 440.0;
            for (int fr = 0; fr < 100; fr++) {
                for (int i = 0; i < 480; i++) pcm[i] = (int16_t)(8000 * sin(2 * M_PI * f * (fr * 480 + i) / 48000.0));
                esp_audio_enc_in_frame_t in = { .buffer = (uint8_t *)pcm, .len = sizeof pcm };
                esp_audio_enc_out_frame_t o = { .buffer = pre[idx] + (size_t)fr * sdu, .len = sdu };
                esp_audio_enc_process(enc, &in, &o);
            }
            esp_audio_enc_close(enc);
            ESP_LOGI(TAG, "JC src #%d: 1 s of %.0f Hz pre-encoded", idx, f);
        }
        memcpy(source_stream->data, pre[idx] + (size_t)(at[idx]++ % 100) * sdu, sdu);
    }

    err = esp_ble_audio_bap_stream_send(&source_stream->stream,
                                        source_stream->data,
                                        source_stream->stream.qos->sdu,
                                        source_stream->seq_num);
    static uint32_t sent[2], failed[2];   /* JC: are both streams really sent every interval? */
    if (err) {
        failed[idx]++;
    } else {
        sent[idx]++;
    }
    if ((sent[idx] + failed[idx]) % 1000 == 0) ESP_LOGI(TAG, "JC src #%d sent=%lu failed=%lu", idx, (unsigned long)sent[idx], (unsigned long)failed[idx]);
    if (err) return;

    source_stream->seq_num++;
}

static void tx_scheduler_cb(void *arg)
{
    struct broadcast_source_stream *source_stream = arg;

    if (source_stream == NULL) {
        return;
    }

    broadcast_source_tx(source_stream);
}

static void source_started_cb(esp_ble_audio_bap_broadcast_source_t *source)
{
    ESP_LOGI(TAG, "Broadcast source started");
}

static void source_stopped_cb(esp_ble_audio_bap_broadcast_source_t *source, uint8_t reason)
{
    ESP_LOGI(TAG, "Broadcast source stopped, reason 0x%02x", reason);

    for (size_t i = 0; i < ARRAY_SIZE(streams); i++) {
        if (streams[i].data != NULL) {
            free(streams[i].data);
            streams[i].data = NULL;
        }
    }
}

static esp_err_t broadcast_source_setup(void)
{
    esp_ble_audio_bap_broadcast_source_subgroup_param_t subgroup_param[SUBGROUP_COUNT];
    esp_ble_audio_bap_broadcast_source_stream_param_t stream_params[STREAM_COUNT];
    esp_ble_audio_bap_broadcast_source_param_t create_param = {0};
    size_t streams_per_subgroup;
    uint8_t left[] = {
        ESP_BLE_AUDIO_CODEC_DATA(ESP_BLE_AUDIO_CODEC_CFG_CHAN_ALLOC,
                                 EXAMPLE_BYTES_LIST_LE32(ESP_BLE_AUDIO_LOCATION_FRONT_LEFT))
    };
    uint8_t right[] = {
        ESP_BLE_AUDIO_CODEC_DATA(ESP_BLE_AUDIO_CODEC_CFG_CHAN_ALLOC,
                                 EXAMPLE_BYTES_LIST_LE32(ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT))
    };
    static esp_ble_audio_bap_broadcast_source_cb_t broadcast_source_cb = {
        .started = source_started_cb,
        .stopped = source_stopped_cb,
    };
    esp_err_t err;

    err = esp_ble_audio_bap_broadcast_source_register_cb(&broadcast_source_cb);
    if (err) {
        ESP_LOGE(TAG, "Failed to register broadcast source callbacks, err %d", err);
        return err;
    }

    streams_per_subgroup = ARRAY_SIZE(stream_params) / ARRAY_SIZE(subgroup_param);

    for (size_t i = 0; i < ARRAY_SIZE(subgroup_param); i++) {
        subgroup_param[i].params_count = streams_per_subgroup;
        subgroup_param[i].params = stream_params + i * streams_per_subgroup;
        subgroup_param[i].codec_cfg = &preset_active.codec_cfg;
    }

    for (size_t i = 0; i < ARRAY_SIZE(stream_params); i++) {
        stream_params[i].stream = &streams[i].stream;
        stream_params[i].data = (i == 0 ? left : right);
        stream_params[i].data_len = (i == 0 ? sizeof(left) : sizeof(right));

        esp_ble_audio_bap_stream_cb_register(stream_params[i].stream, &stream_ops);
    }

    create_param.params_count = ARRAY_SIZE(subgroup_param);
    create_param.params = subgroup_param;
    create_param.qos = &preset_active.qos;
    create_param.encryption = (strlen(LOCAL_BROADCAST_CODE) > 0);
    create_param.packing = ESP_BLE_ISO_PACKING_SEQUENTIAL;

    if (create_param.encryption) {
        memcpy(create_param.broadcast_code, LOCAL_BROADCAST_CODE, strlen(LOCAL_BROADCAST_CODE));
    }

    ESP_LOGI(TAG, "Creating broadcast source: %u subgroup(s), %u stream(s)/subgroup",
             ARRAY_SIZE(subgroup_param), streams_per_subgroup);

    err = esp_ble_audio_bap_broadcast_source_create(&create_param, &broadcast_source);
    if (err) {
        ESP_LOGE(TAG, "Failed to create broadcast source, err %d", err);
        return err;
    }

    return 0;
}

static uint8_t *ext_adv_data_get(uint8_t *data_len)
{
    uint32_t broadcast_id;
    uint8_t *data;

    broadcast_id = LOCAL_BROADCAST_ID;

    /* - Broadcast Audio Announcement Service UUID (2 octets) and
     *   Broadcast ID (3 octets)
     * - Complete Device Name
     */
    *data_len = 7 + 2 + LOCAL_DEVICE_NAME_LEN;

    data = calloc(1, *data_len);
    if (data == NULL) {
        return NULL;
    }

    data[0] = 0x06; /* 1 + 2 + 3 */
    data[1] = EXAMPLE_AD_TYPE_SERVICE_DATA16;
    data[2] = (ESP_BLE_AUDIO_UUID_BROADCAST_AUDIO_VAL & 0xFF);
    data[3] = ((ESP_BLE_AUDIO_UUID_BROADCAST_AUDIO_VAL >> 8) & 0xFF);
    data[4] = (broadcast_id & 0xFF);
    data[5] = ((broadcast_id >> 8) & 0xFF);
    data[6] = ((broadcast_id >> 16) & 0xFF);

    data[7] = LOCAL_DEVICE_NAME_LEN + 1;
    data[8] = EXAMPLE_AD_TYPE_NAME_COMPLETE;
    memcpy(data + 9, LOCAL_DEVICE_NAME, LOCAL_DEVICE_NAME_LEN);

    return data;
}

static uint8_t *per_adv_data_get(uint8_t *data_len)
{
    NET_BUF_SIMPLE_DEFINE(base_buf, 128);
    uint8_t *data;
    esp_err_t err;

    /* Broadcast Audio Announcement Service UUID (2 octets) and
     * Broadcast Audio Source Endpoint (BASE)
     */

    err = esp_ble_audio_bap_broadcast_source_get_base(broadcast_source, &base_buf);
    if (err) {
        ESP_LOGE(TAG, "Failed to get encoded BASE, err %d", err);
        return NULL;
    }

    *data_len = 2 + base_buf.len;

    data = calloc(1, *data_len);
    if (data == NULL) {
        return NULL;
    }

    /* base_buf.len has included the UUID length (2 octets) */
    data[0] = 1 + base_buf.len;
    data[1] = EXAMPLE_AD_TYPE_SERVICE_DATA16;
    memcpy(data + 2, base_buf.data, base_buf.len);

    return data;
}

static int broadcast_start(void)
{
    esp_ble_audio_bap_broadcast_adv_info_t info = {
        .adv_handle = ADV_HANDLE,
    };
    /* BASE (per_adv_data_get) needs broadcast_source to exist — built earlier
     * by broadcast_source_setup(), so safe to call here. */
    uint8_t *ext_data = NULL;
    uint8_t *per_data = NULL;
    uint8_t ext_len = 0;
    uint8_t per_len = 0;
    int err = 0;

    ext_data = ext_adv_data_get(&ext_len);
    if (ext_data == NULL) {
        err = -ENOMEM;
        goto end;
    }

    per_data = per_adv_data_get(&per_len);
    if (per_data == NULL) {
        err = -ENOMEM;
        goto end;
    }

    err = ext_adv_start(ext_data, ext_len, per_data, per_len);
    if (err) {
        goto end;
    }

    err = esp_ble_audio_bap_broadcast_adv_add(&info);
    if (err) {
        ESP_LOGE(TAG, "Failed to add adv for broadcast source, err %d", err);
        goto end;
    }

    err = esp_ble_audio_bap_broadcast_source_start(broadcast_source, ADV_HANDLE);
    if (err) {
        ESP_LOGE(TAG, "Failed to start broadcast source, err %d", err);
    }

end:
    if (ext_data != NULL) {
        free(ext_data);
    }
    if (per_data != NULL) {
        free(per_data);
    }
    return err;
}

void app_main(void)
{
    esp_err_t err;

    /* Initialize NVS — it is used to store PHY calibration data */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "Failed to initialize BLE, err %d", err);
        return;
    }

    err = app_host_init();
    if (err) {
        ESP_LOGE(TAG, "Failed to init host, err %d", err);
        return;
    }

    err = esp_ble_audio_common_init(NULL);
    if (err) {
        ESP_LOGE(TAG, "Failed to initialize audio, err %d", err);
        return;
    }

    err = broadcast_source_setup();
    if (err) {
        return;
    }

    err = esp_ble_audio_common_start(NULL);
    if (err) {
        ESP_LOGE(TAG, "Failed to start audio, err %d", err);
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(streams); i++) {
        err = example_audio_tx_scheduler_init(&streams[i].scheduler,
                                              tx_scheduler_cb,
                                              &streams[i]);
        if (err) {
            ESP_LOGE(TAG, "[SRC #%zu] Scheduler init failed, err %d", i, err);
            return;
        }
    }

    err = broadcast_start();
    if (err) {
        ESP_LOGE(TAG, "Failed to start broadcast, err %d", err);
        return;
    }
}
