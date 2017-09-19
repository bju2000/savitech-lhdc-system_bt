/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "a2dp_vendor_lhdc_encoder"
#define ATRACE_TAG ATRACE_TAG_AUDIO

#include "a2dp_vendor_lhdc_encoder.h"

#ifndef OS_GENERIC
#include <cutils/trace.h>
#endif
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <lhdcBT.h>

#include "a2dp_vendor.h"
#include "a2dp_vendor_lhdc.h"
//#include "a2dp_vendor_lhdc_abr.h"
#include "bt_common.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"

//
// Encoder for LHDC Source Codec
//

//
// The LHDC encoder shared library, and the functions to use
//
static const char* LHDC_ENCODER_LIB_NAME = "liblhdcBT_enc.so";
static void* lhdc_encoder_lib_handle = NULL;

static const char* LHDC_GET_HANDLE_NAME = "lhdcBT_get_handle";
typedef HANDLE_LHDC_BT (*tLHDC_GET_HANDLE)(void);

static const char* LHDC_FREE_HANDLE_NAME = "lhdcBT_free_handle";
typedef void (*tLHDC_FREE_HANDLE)(HANDLE_LHDC_BT hLhdcParam);

//static const char* LHDC_CLOSE_HANDLE_NAME = "lhdcBT_close_handle";
//typedef void (*tLHDC_CLOSE_HANDLE)(HANDLE_LHDC_BT hLhdcParam);

//static const char* LHDC_GET_VERSION_NAME = "lhdcBT_get_version";
//typedef int (*tLHDC_GET_VERSION)(void);

static const char* LHDC_GET_BITRATE_NAME = "lhdcBT_get_bitrate";
typedef int (*tLHDC_GET_BITRATE)(HANDLE_LHDC_BT hLhdcParam);
static const char* LHDC_SET_BITRATE_NAME = "lhdcBT_set_bitrate";
typedef int (*tLHDC_SET_BITRATE)(HANDLE_LHDC_BT hLhdcParam, int index);

static const char* LHDC_GET_SAMPLING_FREQ_NAME = "lhdcBT_get_sampling_freq";
typedef int (*tLHDC_GET_SAMPLING_FREQ)(HANDLE_LHDC_BT hLhdcParam);

static const char* LHDC_INIT_HANDLE_ENCODE_NAME = "lhdcBT_init_handle_encode";
typedef int (*tLHDC_INIT_HANDLE_ENCODE)(HANDLE_LHDC_BT hLhdcParam,int sampling_freq, int bitPerSample, int bitrate_inx);

//int lhdcBT_adjust_bitrate(HANDLE_LHDC_BT handle, int queueLength)
static const char* LHDC_AUTO_ADJUST_BITRATE_NAME = "lhdcBT_adjust_bitrate";
typedef int (*tLHDC_AUTO_ADJUST_BITRATE)(HANDLE_LHDC_BT hLhdcParam, size_t queueLength);


static const char* LHDC_ENCODE_NAME = "lhdcBT_encode";
typedef int (*tLHDC_ENCODE)(HANDLE_LHDC_BT hLhdcParam, void* p_pcm, unsigned char* p_stream);

//static const char* LHDC_SET_EQMID_NAME = "lhdcBT_set_eqmid";
//typedef int (*tLHDC_SET_EQMID)(HANDLE_LHDC_BT hLhdcParam, int eqmid);

//static const char* LHDC_ALTER_EQMID_PRIORITY_NAME =
//    "lhdcBT_alter_eqmid_priority";
//typedef int (*tLHDC_ALTER_EQMID_PRIORITY)(HANDLE_LHDC_BT hLhdcParam,
//                                          int priority);

//static const char* LHDC_GET_EQMID_NAME = "lhdcBT_get_eqmid";
//typedef int (*tLHDC_GET_EQMID)(HANDLE_LHDC_BT hLhdcParam);

static const char* LHDC_GET_ERROR_CODE_NAME = "lhdcBT_get_error_code";
typedef int (*tLHDC_GET_ERROR_CODE)(HANDLE_LHDC_BT hLhdcParam);

static tLHDC_GET_HANDLE lhdc_get_handle_func;
static tLHDC_FREE_HANDLE lhdc_free_handle_func;
//static tLHDC_CLOSE_HANDLE lhdc_close_handle_func;
//static tLHDC_GET_VERSION lhdc_get_version_func;
static tLHDC_GET_BITRATE lhdc_get_bitrate_func;
static tLHDC_SET_BITRATE lhdc_set_bitrate_func;
static tLHDC_GET_SAMPLING_FREQ lhdc_get_sampling_freq_func;
static tLHDC_INIT_HANDLE_ENCODE lhdc_init_handle_encode_func;
static tLHDC_ENCODE lhdc_encode_func;
static tLHDC_AUTO_ADJUST_BITRATE lhdc_auto_adjust_bitrate_func;
static tLHDC_GET_ERROR_CODE lhdc_get_error_code_func;

// A2DP LHDC encoder interval in milliseconds
#define A2DP_LHDC_ENCODER_INTERVAL_MS 20
#define A2DP_LHDC_MEDIA_BYTES_PER_FRAME 512

// offset
#if (BTA_AV_CO_CP_SCMS_T == TRUE)
#define A2DP_LHDC_OFFSET (AVDT_MEDIA_OFFSET + A2DP_LHDC_MPL_HDR_LEN + 1)
#else
#define A2DP_LHDC_OFFSET (AVDT_MEDIA_OFFSET + A2DP_LHDC_MPL_HDR_LEN)
#endif

//#define A2DP_LHDC_OFFSET (AVDT_MEDIA_OFFSET + 0)

typedef struct {
  uint32_t sample_rate;
  uint8_t channel_mode;
  uint8_t bits_per_sample;
  int quality_mode_index;
  int latency_mode_index;
  int pcm_wlength;
  LHDCBT_SMPL_FMT_T pcm_fmt;
} tA2DP_LHDC_ENCODER_PARAMS;

typedef struct {
  uint32_t counter;
  uint32_t bytes_per_tick; /* pcm bytes read each media task tick */
  uint64_t last_frame_us;
} tA2DP_LHDC_FEEDING_STATE;

typedef struct {
  uint64_t session_start_us;

  size_t media_read_total_expected_packets;
  size_t media_read_total_expected_reads_count;
  size_t media_read_total_expected_read_bytes;

  size_t media_read_total_dropped_packets;
  size_t media_read_total_actual_reads_count;
  size_t media_read_total_actual_read_bytes;
} a2dp_lhdc_encoder_stats_t;

typedef struct {
  a2dp_source_read_callback_t read_callback;
  a2dp_source_enqueue_callback_t enqueue_callback;
  uint16_t TxAaMtuSize;
  size_t TxQueueLength;

  bool use_SCMS_T;
  bool is_peer_edr;          // True if the peer device supports EDR
  bool peer_supports_3mbps;  // True if the peer device supports 3Mbps EDR
  uint16_t peer_mtu;         // MTU of the A2DP peer
  uint32_t timestamp;        // Timestamp for the A2DP frames

  HANDLE_LHDC_BT lhdc_handle;
  bool has_lhdc_handle;  // True if lhdc_handle is valid
/*
  HANDLE_LHDC_ABR lhdc_abr_handle;
  bool has_lhdc_abr_handle;
  int last_lhdc_abr_eqmid;
  size_t lhdc_abr_adjustments;
*/
  tA2DP_FEEDING_PARAMS feeding_params;
  tA2DP_LHDC_ENCODER_PARAMS lhdc_encoder_params;
  tA2DP_LHDC_FEEDING_STATE lhdc_feeding_state;

  a2dp_lhdc_encoder_stats_t stats;
  uint32_t buf_seq;
} tA2DP_LHDC_ENCODER_CB;

//static bool lhdc_abr_loaded = false;

static tA2DP_LHDC_ENCODER_CB a2dp_lhdc_encoder_cb;

static void a2dp_vendor_lhdc_encoder_update(uint16_t peer_mtu,
                                            A2dpCodecConfig* a2dp_codec_config,
                                            bool* p_restart_input,
                                            bool* p_restart_output,
                                            bool* p_config_updated);
static void a2dp_lhdc_get_num_frame_iteration(uint8_t* num_of_iterations,
                                              uint8_t* num_of_frames,
                                              uint64_t timestamp_us);
static void a2dp_lhdc_encode_frames(uint8_t nb_frame);
static bool a2dp_lhdc_read_feeding(uint8_t* read_buffer);
static std::string quality_mode_index_to_name(int quality_mode_index);
static std::string latency_mode_index_to_name(int latency_mode_index);

static void* load_func(const char* func_name) {
  void* func_ptr = dlsym(lhdc_encoder_lib_handle, func_name);
  if (func_ptr == NULL) {
    LOG_ERROR(LOG_TAG,
              "%s: cannot find function '%s' in the encoder library: %s",
              __func__, func_name, dlerror());
    A2DP_VendorUnloadEncoderLhdc();
    return NULL;
  }
  return func_ptr;
}

bool A2DP_VendorLoadEncoderLhdc(void) {
  if (lhdc_encoder_lib_handle != NULL) return true;  // Already loaded

  // Initialize the control block
  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

  // Open the encoder library
  lhdc_encoder_lib_handle = dlopen(LHDC_ENCODER_LIB_NAME, RTLD_NOW);
  if (lhdc_encoder_lib_handle == NULL) {
    LOG_ERROR(LOG_TAG, "%s: cannot open LHDC encoder library %s: %s", __func__,
              LHDC_ENCODER_LIB_NAME, dlerror());
    return false;
  }

  // Load all functions
  lhdc_get_handle_func = (tLHDC_GET_HANDLE)load_func(LHDC_GET_HANDLE_NAME);
  if (lhdc_get_handle_func == NULL) return false;
  lhdc_free_handle_func = (tLHDC_FREE_HANDLE)load_func(LHDC_FREE_HANDLE_NAME);
  if (lhdc_free_handle_func == NULL) return false;
  //lhdc_close_handle_func = (tLHDC_CLOSE_HANDLE)load_func(LHDC_CLOSE_HANDLE_NAME);
  //if (lhdc_close_handle_func == NULL) return false;
  //lhdc_get_version_func = (tLHDC_GET_VERSION)load_func(LHDC_GET_VERSION_NAME);
  //if (lhdc_get_version_func == NULL) return false;
  lhdc_get_bitrate_func = (tLHDC_GET_BITRATE)load_func(LHDC_GET_BITRATE_NAME);
  if (lhdc_get_bitrate_func == NULL) return false;
  lhdc_set_bitrate_func = (tLHDC_SET_BITRATE)load_func(LHDC_SET_BITRATE_NAME);
  if (lhdc_set_bitrate_func == NULL) return false;
  lhdc_get_sampling_freq_func =
      (tLHDC_GET_SAMPLING_FREQ)load_func(LHDC_GET_SAMPLING_FREQ_NAME);
  if (lhdc_get_sampling_freq_func == NULL) return false;
  lhdc_init_handle_encode_func =
      (tLHDC_INIT_HANDLE_ENCODE)load_func(LHDC_INIT_HANDLE_ENCODE_NAME);
  if (lhdc_init_handle_encode_func == NULL) return false;
  lhdc_encode_func = (tLHDC_ENCODE)load_func(LHDC_ENCODE_NAME);
  if (lhdc_encode_func == NULL) return false;
  lhdc_auto_adjust_bitrate_func = (tLHDC_AUTO_ADJUST_BITRATE)load_func(LHDC_AUTO_ADJUST_BITRATE_NAME);
  if (lhdc_auto_adjust_bitrate_func == NULL) return false;
  //lhdc_alter_eqmid_priority_func = (tLHDC_ALTER_EQMID_PRIORITY)load_func(LHDC_ALTER_EQMID_PRIORITY_NAME);
  //if (lhdc_alter_eqmid_priority_func == NULL) return false;
  //lhdc_get_eqmid_func = (tLHDC_GET_EQMID)load_func(LHDC_GET_EQMID_NAME);
  //if (lhdc_get_eqmid_func == NULL) return false;
  lhdc_get_error_code_func = (tLHDC_GET_ERROR_CODE)load_func(LHDC_GET_ERROR_CODE_NAME);
  if (lhdc_get_error_code_func == NULL) return false;

/*
  if (!A2DP_VendorLoadLhdcAbr()) {
    LOG_WARN(LOG_TAG, "%s: cannot load the LHDC ABR library", __func__);
    lhdc_abr_loaded = false;
  } else {
    lhdc_abr_loaded = true;
  }
  */
  return true;
}

void A2DP_VendorUnloadEncoderLhdc(void) {
  // Cleanup any LHDC-related state

    LOG_DEBUG(LOG_TAG, "%s: a2dp_lhdc_encoder_cb.has_lhdc_handle = %d, lhdc_free_handle_func = %p",
              __func__, a2dp_lhdc_encoder_cb.has_lhdc_handle, lhdc_free_handle_func);
  if (a2dp_lhdc_encoder_cb.has_lhdc_handle && lhdc_free_handle_func != NULL)
    lhdc_free_handle_func(a2dp_lhdc_encoder_cb.lhdc_handle);
  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

  lhdc_get_handle_func = NULL;
  lhdc_free_handle_func = NULL;
  //lhdc_close_handle_func = NULL;
  //lhdc_get_version_func = NULL;
  lhdc_get_bitrate_func = NULL;
  lhdc_set_bitrate_func = NULL;
  lhdc_get_sampling_freq_func = NULL;
  lhdc_init_handle_encode_func = NULL;
  lhdc_encode_func = NULL;
  lhdc_auto_adjust_bitrate_func = NULL;
  //lhdc_alter_eqmid_priority_func = NULL;
  //lhdc_get_eqmid_func = NULL;
  lhdc_get_error_code_func = NULL;

  if (lhdc_encoder_lib_handle != NULL) {
    dlclose(lhdc_encoder_lib_handle);
    lhdc_encoder_lib_handle = NULL;
  }
}

void a2dp_vendor_lhdc_encoder_init(
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
    A2dpCodecConfig* a2dp_codec_config,
    a2dp_source_read_callback_t read_callback,
    a2dp_source_enqueue_callback_t enqueue_callback) {
  if (a2dp_lhdc_encoder_cb.has_lhdc_handle)
    lhdc_free_handle_func(a2dp_lhdc_encoder_cb.lhdc_handle);

  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

  a2dp_lhdc_encoder_cb.stats.session_start_us = time_get_os_boottime_us();

  a2dp_lhdc_encoder_cb.read_callback = read_callback;
  a2dp_lhdc_encoder_cb.enqueue_callback = enqueue_callback;
  a2dp_lhdc_encoder_cb.is_peer_edr = p_peer_params->is_peer_edr;
  a2dp_lhdc_encoder_cb.peer_supports_3mbps = p_peer_params->peer_supports_3mbps;
  a2dp_lhdc_encoder_cb.peer_mtu = p_peer_params->peer_mtu;
  a2dp_lhdc_encoder_cb.timestamp = 0;


  a2dp_lhdc_encoder_cb.use_SCMS_T = false;  // TODO: should be a parameter
#if (BTA_AV_CO_CP_SCMS_T == TRUE)
  a2dp_lhdc_encoder_cb.use_SCMS_T = true;
#endif

  // NOTE: Ignore the restart_input / restart_output flags - this initization
  // happens when the connection is (re)started.
  bool restart_input = false;
  bool restart_output = false;
  bool config_updated = false;
  a2dp_vendor_lhdc_encoder_update(a2dp_lhdc_encoder_cb.peer_mtu,
                                  a2dp_codec_config, &restart_input,
                                  &restart_output, &config_updated);
}

bool A2dpCodecConfigLhdc::updateEncoderUserConfig(
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params, bool* p_restart_input,
    bool* p_restart_output, bool* p_config_updated) {
  a2dp_lhdc_encoder_cb.is_peer_edr = p_peer_params->is_peer_edr;
  a2dp_lhdc_encoder_cb.peer_supports_3mbps = p_peer_params->peer_supports_3mbps;
  a2dp_lhdc_encoder_cb.peer_mtu = p_peer_params->peer_mtu;
  a2dp_lhdc_encoder_cb.timestamp = 0;

  if (a2dp_lhdc_encoder_cb.peer_mtu == 0) {
    LOG_ERROR(LOG_TAG,
              "%s: Cannot update the codec encoder for %s: "
              "invalid peer MTU",
              __func__, name().c_str());
    return false;
  }

  a2dp_vendor_lhdc_encoder_update(a2dp_lhdc_encoder_cb.peer_mtu, this,
                                  p_restart_input, p_restart_output,
                                  p_config_updated);
  return true;
}

// Update the A2DP LHDC encoder.
// |peer_mtu| is the peer MTU.
// |a2dp_codec_config| is the A2DP codec to use for the update.
static void a2dp_vendor_lhdc_encoder_update(uint16_t peer_mtu,
                                            A2dpCodecConfig* a2dp_codec_config,
                                            bool* p_restart_input,
                                            bool* p_restart_output,
                                            bool* p_config_updated) {
  tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params =
      &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
  uint8_t codec_info[AVDT_CODEC_SIZE];

  *p_restart_input = false;
  *p_restart_output = false;
  *p_config_updated = false;

  if (!a2dp_lhdc_encoder_cb.has_lhdc_handle) {
    a2dp_lhdc_encoder_cb.lhdc_handle = lhdc_get_handle_func();
    if (a2dp_lhdc_encoder_cb.lhdc_handle == NULL) {
      LOG_ERROR(LOG_TAG, "%s: Cannot get LHDC encoder handle", __func__);
      return;  // TODO: Return an error?
    }
    a2dp_lhdc_encoder_cb.has_lhdc_handle = true;
  }

  if (!a2dp_codec_config->copyOutOtaCodecConfig(codec_info)) {
    LOG_ERROR(LOG_TAG,
              "%s: Cannot update the codec encoder for %s: "
              "invalid codec config",
              __func__, a2dp_codec_config->name().c_str());
    return;
  }
  const uint8_t* p_codec_info = codec_info;
  btav_a2dp_codec_config_t codec_config = a2dp_codec_config->getCodecConfig();

  // The feeding parameters
  tA2DP_FEEDING_PARAMS* p_feeding_params = &a2dp_lhdc_encoder_cb.feeding_params;
  p_feeding_params->sample_rate =
      A2DP_VendorGetTrackSampleRateLhdc(p_codec_info);
  p_feeding_params->bits_per_sample =
      a2dp_codec_config->getAudioBitsPerSample();
  p_feeding_params->channel_count =
      A2DP_VendorGetTrackChannelCountLhdc(p_codec_info);
  LOG_DEBUG(LOG_TAG, "%s: sample_rate=%u bits_per_sample=%u channel_count=%u",
            __func__, p_feeding_params->sample_rate,
            p_feeding_params->bits_per_sample, p_feeding_params->channel_count);

  // The codec parameters
  p_encoder_params->sample_rate =
      a2dp_lhdc_encoder_cb.feeding_params.sample_rate;
  p_encoder_params->channel_mode =
      A2DP_VendorGetChannelModeCodeLhdc(p_codec_info);

  uint16_t mtu_size =
      BT_DEFAULT_BUFFER_SIZE - A2DP_LHDC_OFFSET - sizeof(BT_HDR);
  if (mtu_size < peer_mtu) {
    a2dp_lhdc_encoder_cb.TxAaMtuSize = mtu_size;
  } else {
    a2dp_lhdc_encoder_cb.TxAaMtuSize = peer_mtu;
  }

  // Set the quality mode index
  //int old_quality_mode_index = p_encoder_params->quality_mode_index;
  LOG_DEBUG(LOG_TAG, "%s:codec_config.codec_specific_1 = %d, codec_config.codec_specific_2 = %d", __func__, (int32_t)codec_config.codec_specific_1, (int32_t)codec_config.codec_specific_2);
  if ((codec_config.codec_specific_1 & A2DP_LHDC_VENDOR_CMD_MASK) == A2DP_LDHC_QUALITY_MAGIC_NUM) {
      //int newValue = codec_config.codec_specific_1 & 0xff;
      int newValue = codec_config.codec_specific_1 & 0xff;
      if (newValue != p_encoder_params->quality_mode_index) {

        p_encoder_params->quality_mode_index = newValue;
        LOG_DEBUG(LOG_TAG, "%s: setting quality mode to %s(%d)", __func__,
                  quality_mode_index_to_name(p_encoder_params->quality_mode_index)
                      .c_str(), p_encoder_params->quality_mode_index);

         lhdc_set_bitrate_func(a2dp_lhdc_encoder_cb.lhdc_handle, p_encoder_params->quality_mode_index);
      }
  }else {
      p_encoder_params->quality_mode_index = A2DP_LHDC_QUALITY_LOW;
      lhdc_set_bitrate_func(a2dp_lhdc_encoder_cb.lhdc_handle, p_encoder_params->quality_mode_index);
  }

  //p_encoder_params->latency_mode_index = 1;
  if ((codec_config.codec_specific_2 & A2DP_LHDC_VENDOR_CMD_MASK) == A2DP_LHDC_LATENCY_MAGIC_NUM) {
      int newValue = codec_config.codec_specific_2 & 0xff;
      if (newValue != p_encoder_params->latency_mode_index) {
          /* code */
          p_encoder_params->latency_mode_index = newValue;
          LOG_DEBUG(LOG_TAG, "%s: setting latency value to %s(%d)", __func__,
                    latency_mode_index_to_name(p_encoder_params->latency_mode_index).c_str(),
                    p_encoder_params->latency_mode_index);
      }
  }else {
      p_encoder_params->latency_mode_index = A2DP_LHDC_LATENCY_MID;
  }

  p_encoder_params->pcm_wlength =
      a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample >> 3;
  // Set the Audio format from pcm_wlength
  p_encoder_params->pcm_fmt = LHDCBT_SMPL_FMT_S16;
  if (p_encoder_params->pcm_wlength == 2)
    p_encoder_params->pcm_fmt = LHDCBT_SMPL_FMT_S16;
  else if (p_encoder_params->pcm_wlength == 3)
    p_encoder_params->pcm_fmt = LHDCBT_SMPL_FMT_S24;
 // else if (p_encoder_params->pcm_wlength == 4)
//    p_encoder_params->pcm_fmt = LHDCBT_SMPL_FMT_S32;

  LOG_DEBUG(LOG_TAG, "%s: MTU=%d, peer_mtu=%d", __func__,
            a2dp_lhdc_encoder_cb.TxAaMtuSize, peer_mtu);
  LOG_DEBUG(LOG_TAG,
            "%s: sample_rate: %d channel_mode: %d "
            "quality_mode_index: %d pcm_wlength: %d pcm_fmt: %d",
            __func__, p_encoder_params->sample_rate,
            p_encoder_params->channel_mode,
            p_encoder_params->quality_mode_index, p_encoder_params->pcm_wlength,
            p_encoder_params->pcm_fmt);

  // Initialize the encoder.
  // NOTE: MTU in the initialization must include the AVDT media header size.
  int result = lhdc_init_handle_encode_func(
      a2dp_lhdc_encoder_cb.lhdc_handle,
      p_encoder_params->sample_rate,
      p_encoder_params->pcm_fmt,
      p_encoder_params->quality_mode_index
  );

  if (result != 0) {
    LOG_ERROR(LOG_TAG, "%s: error initializing the LHDC encoder: %d", __func__,
              result);
  }
}

void a2dp_vendor_lhdc_encoder_cleanup(void) {
  if (a2dp_lhdc_encoder_cb.has_lhdc_handle)
    lhdc_free_handle_func(a2dp_lhdc_encoder_cb.lhdc_handle);
  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));
}

void a2dp_vendor_lhdc_feeding_reset(void) {
  /* By default, just clear the entire state */
  memset(&a2dp_lhdc_encoder_cb.lhdc_feeding_state, 0,
         sizeof(a2dp_lhdc_encoder_cb.lhdc_feeding_state));

  a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick =
      (a2dp_lhdc_encoder_cb.feeding_params.sample_rate *
       a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8 *
       a2dp_lhdc_encoder_cb.feeding_params.channel_count *
       A2DP_LHDC_ENCODER_INTERVAL_MS) /
      1000;
  a2dp_lhdc_encoder_cb.buf_seq = 0;
  LOG_DEBUG(LOG_TAG, "%s: PCM bytes per tick %u", __func__,
            a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick);
}

void a2dp_vendor_lhdc_feeding_flush(void) {
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter = 0;
  LOG_DEBUG(LOG_TAG, "%s", __func__);
}

period_ms_t a2dp_vendor_lhdc_get_encoder_interval_ms(void) {
  LOG_DEBUG(LOG_TAG, "%s: A2DP_LHDC_ENCODER_INTERVAL_MS %u",
              __func__, A2DP_LHDC_ENCODER_INTERVAL_MS);
  return A2DP_LHDC_ENCODER_INTERVAL_MS;
}

void a2dp_vendor_lhdc_send_frames(uint64_t timestamp_us) {
  uint8_t nb_frame = 0;
  uint8_t nb_iterations = 0;

  a2dp_lhdc_get_num_frame_iteration(&nb_iterations, &nb_frame, timestamp_us);
  LOG_DEBUG(LOG_TAG, "%s: Sending %d frames per iteration, %d iterations",
              __func__, nb_frame, nb_iterations);
  if (nb_frame == 0) return;

  for (uint8_t counter = 0; counter < nb_iterations; counter++) {
      /*
    if (a2dp_lhdc_encoder_cb.has_lhdc_abr_handle) {
      int flag_enable = 1;
      int prev_eqmid = a2dp_lhdc_encoder_cb.last_lhdc_abr_eqmid;
      a2dp_lhdc_encoder_cb.last_lhdc_abr_eqmid =
          a2dp_lhdc_abr_proc(a2dp_lhdc_encoder_cb.lhdc_handle,
                             a2dp_lhdc_encoder_cb.lhdc_abr_handle,
                             a2dp_lhdc_encoder_cb.TxQueueLength, flag_enable);
      if (prev_eqmid != a2dp_lhdc_encoder_cb.last_lhdc_abr_eqmid)
        a2dp_lhdc_encoder_cb.lhdc_abr_adjustments++;
#ifndef OS_GENERIC
      ATRACE_INT("LHDC ABR level", a2dp_lhdc_encoder_cb.last_lhdc_abr_eqmid);
#endif
    }
    */
    // Transcode frame and enqueue
    a2dp_lhdc_encode_frames(nb_frame);
  }
}

// Obtains the number of frames to send and number of iterations
// to be used. |num_of_iterations| and |num_of_frames| parameters
// are used as output param for returning the respective values.
static void a2dp_lhdc_get_num_frame_iteration(uint8_t* num_of_iterations,
                                              uint8_t* num_of_frames,
                                              uint64_t timestamp_us) {
  uint32_t result = 0;
  uint8_t nof = 0;
  uint8_t noi = 1;

  uint32_t pcm_bytes_per_frame =
      A2DP_LHDC_MEDIA_BYTES_PER_FRAME *
      a2dp_lhdc_encoder_cb.feeding_params.channel_count *
      a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;
  LOG_DEBUG(LOG_TAG, "%s: pcm_bytes_per_frame %u", __func__,
              pcm_bytes_per_frame);

  uint32_t us_this_tick = A2DP_LHDC_ENCODER_INTERVAL_MS * 1000;
  uint64_t now_us = timestamp_us;
  if (a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us != 0)
    us_this_tick =
        (now_us - a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us);
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us = now_us;

  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter +=
      a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick * us_this_tick /
      (A2DP_LHDC_ENCODER_INTERVAL_MS * 1000);

  result =
      a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter / pcm_bytes_per_frame;
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter -=
      result * pcm_bytes_per_frame;
  nof = result;

  LOG_DEBUG(LOG_TAG, "%s: effective num of frames %u, iterations %u",
              __func__, nof, noi);

  *num_of_frames = nof;
  *num_of_iterations = noi;
}

static BT_HDR *bt_buf_new( void) {
    BT_HDR *p_buf = ( BT_HDR*)osi_malloc( BT_DEFAULT_BUFFER_SIZE);
    if ( p_buf == NULL) {
        // LeoKu(C): should not happen
        LOG_ERROR( LOG_TAG, "%s: bt_buf_new failed!", __func__);
        return  NULL;
    }

    p_buf->offset = A2DP_LHDC_OFFSET;
    p_buf->len = 0;
    p_buf->layer_specific = 0;
    return  p_buf;
}


static void a2dp_lhdc_encode_frames(uint8_t nb_frame) {
    BT_HDR * p_buf = NULL;
    BT_HDR * p_btBufs[16];
    uint8_t nb_frame_org = nb_frame;
    tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params =
        &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
    uint32_t pcm_bytes_per_frame = LHDCBT_ENC_BLOCK_SIZE *
                                 a2dp_lhdc_encoder_cb.feeding_params.channel_count *
                                 a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;


#if (BTA_AV_CO_CP_SCMS_T == TRUE)
    uint32_t max_mtu_len = ( uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN - 1);
#else
    uint32_t max_mtu_len = ( uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN);
#endif

    uint8_t read_buffer[pcm_bytes_per_frame];
    uint8_t write_buffer[pcm_bytes_per_frame];
    uint8_t latency = p_encoder_params->latency_mode_index;
    int out_offset = 0;
    int out_len = 0;
    //uint32_t fragments = 0;

    uint32_t bt_buf_num = 0;
    while( nb_frame) {
        if ( !a2dp_lhdc_read_feeding(read_buffer)) {
        LOG_WARN(LOG_TAG, "%s: underflow %d", __func__, nb_frame);
        a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter +=
                  nb_frame * LHDCBT_ENC_BLOCK_SIZE *
                  a2dp_lhdc_encoder_cb.feeding_params.channel_count *
                  a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;
            break;
        }

        out_offset = 0;
        out_len = lhdc_encode_func(a2dp_lhdc_encoder_cb.lhdc_handle, read_buffer, write_buffer);

        nb_frame--;


        while (out_len > 0) {
            if (p_buf == NULL) {
                if (NULL == (p_buf = bt_buf_new())) {
                    LOG_ERROR (LOG_TAG, "%s: ERROR", __func__);
                    return;
                }
            }

            uint8_t *p = ( uint8_t *)( p_buf + 1) + p_buf->offset + p_buf->len;
            int space = max_mtu_len - p_buf->len;
            int bytes = ( out_len < space)? out_len : space;
            memcpy( p, &write_buffer[out_offset], bytes);
            out_offset += bytes;
            out_len -= bytes;
            p_buf->len += bytes;

            if ( p_buf->len >= max_mtu_len ) {
                p_btBufs[bt_buf_num] = p_buf;
                bt_buf_num++;
                // allocate new one
                p_buf = NULL;
            }
        }
    }

    if ( p_buf) {
        p_btBufs[bt_buf_num] = p_buf;
        bt_buf_num++;
    }

    if ( bt_buf_num == 1) {
        p_buf = p_btBufs[0];

        p_buf->layer_specific = a2dp_lhdc_encoder_cb.buf_seq++;
        p_buf->layer_specific <<= 8;
        p_buf->layer_specific |= ( latency | ( nb_frame_org << A2DP_LHDC_HDR_NUM_SHIFT));

        *( ( uint32_t*)( p_buf + 1)) = a2dp_lhdc_encoder_cb.timestamp;

        a2dp_lhdc_encoder_cb.enqueue_callback( p_buf, 1);

    } else {

        uint8_t i;

        for( i = 0; i < bt_buf_num; i++) {
            p_buf = p_btBufs[i];

            p_buf->layer_specific = a2dp_lhdc_encoder_cb.buf_seq++;
            p_buf->layer_specific <<= 8;
            p_buf->layer_specific |= ( A2DP_LHDC_HDR_F_MSK | latency);

            if ( i == 0) {
                p_buf->layer_specific |= ( A2DP_LHDC_HDR_S_MSK | ( nb_frame_org << A2DP_LHDC_HDR_NUM_SHIFT));
            } else if ( i == ( bt_buf_num - 1)) {
                p_buf->layer_specific |= A2DP_LHDC_HDR_L_MSK;
            }

            *( ( uint32_t*)( p_buf + 1)) = a2dp_lhdc_encoder_cb.timestamp;

            a2dp_lhdc_encoder_cb.enqueue_callback( p_buf, 1);
        }
    }

    a2dp_lhdc_encoder_cb.timestamp += ( nb_frame_org * LHDCBT_ENC_BLOCK_SIZE);

}

static bool a2dp_lhdc_read_feeding(uint8_t* read_buffer) {
  uint32_t read_size = LHDCBT_ENC_BLOCK_SIZE *
                       a2dp_lhdc_encoder_cb.feeding_params.channel_count *
                       a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;

  a2dp_lhdc_encoder_cb.stats.media_read_total_expected_reads_count++;
  a2dp_lhdc_encoder_cb.stats.media_read_total_expected_read_bytes += read_size;

  /* Read Data from UIPC channel */
  uint32_t nb_byte_read =
      a2dp_lhdc_encoder_cb.read_callback(read_buffer, read_size);
  //LOG_DEBUG(LOG_TAG, "%s: want to read size %u, read byte number %u",
//                    __func__, read_size, nb_byte_read);
  a2dp_lhdc_encoder_cb.stats.media_read_total_actual_read_bytes += nb_byte_read;

  if (nb_byte_read < read_size) {
    if (nb_byte_read == 0) return false;

    /* Fill the unfilled part of the read buffer with silence (0) */
    memset(((uint8_t*)read_buffer) + nb_byte_read, 0, read_size - nb_byte_read);
    nb_byte_read = read_size;
  }
  a2dp_lhdc_encoder_cb.stats.media_read_total_actual_reads_count++;

  return true;
}

static std::string quality_mode_index_to_name(int quality_mode_index) {
  switch (quality_mode_index) {
    case A2DP_LHDC_QUALITY_HIGH:
      return "HIGH";
    case A2DP_LHDC_QUALITY_MID:
      return "MID";
    case A2DP_LHDC_QUALITY_LOW:
      return "LOW";
    case A2DP_LHDC_QUALITY_ABR:
      return "ABR";
    default:
      return "Unknown";
  }
}

static std::string latency_mode_index_to_name(int latency_mode_index){
    switch (latency_mode_index) {
        case A2DP_LHDC_LATENCY_HIGH:
            return "Long Latency";
        case A2DP_LHDC_LATENCY_MID:
            return "Middle Latency";
        case A2DP_LHDC_LATENCY_LOW:
            return "Short Latency";
        default:
            return "Unknown";
    }
}

void a2dp_vendor_lhdc_set_transmit_queue_length(size_t transmit_queue_length) {
  a2dp_lhdc_encoder_cb.TxQueueLength = transmit_queue_length;
  tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params = &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
  LOG_DEBUG(LOG_TAG, "%s: transmit_queue_length %zu", __func__, transmit_queue_length);
  if (p_encoder_params->quality_mode_index == A2DP_LHDC_QUALITY_ABR) {
      LOG_DEBUG(LOG_TAG, "%s: Auto Bitrate Enabled!", __func__);
      if (lhdc_auto_adjust_bitrate_func != NULL) {
          lhdc_auto_adjust_bitrate_func(a2dp_lhdc_encoder_cb.lhdc_handle, transmit_queue_length);
      }
  }
}

period_ms_t A2dpCodecConfigLhdc::encoderIntervalMs() const {
  return a2dp_vendor_lhdc_get_encoder_interval_ms();
}

void A2dpCodecConfigLhdc::debug_codec_dump(int fd) {
  a2dp_lhdc_encoder_stats_t* stats = &a2dp_lhdc_encoder_cb.stats;
  tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params =
      &a2dp_lhdc_encoder_cb.lhdc_encoder_params;

  A2dpCodecConfig::debug_codec_dump(fd);

  dprintf(fd,
          "  Packet counts (expected/dropped)                        : %zu / "
          "%zu\n",
          stats->media_read_total_expected_packets,
          stats->media_read_total_dropped_packets);

  dprintf(fd,
          "  PCM read counts (expected/actual)                       : %zu / "
          "%zu\n",
          stats->media_read_total_expected_reads_count,
          stats->media_read_total_actual_reads_count);

  dprintf(fd,
          "  PCM read bytes (expected/actual)                        : %zu / "
          "%zu\n",
          stats->media_read_total_expected_read_bytes,
          stats->media_read_total_actual_read_bytes);

  dprintf(
      fd, "  LHDC quality mode                                       : %s\n",
      quality_mode_index_to_name(p_encoder_params->quality_mode_index).c_str());

  dprintf(fd,
          "  LHDC transmission bitrate (Kbps)                        : %d\n",
          lhdc_get_bitrate_func(a2dp_lhdc_encoder_cb.lhdc_handle));

  dprintf(fd,
          "  LHDC saved transmit queue length                        : %zu\n",
          a2dp_lhdc_encoder_cb.TxQueueLength);
/*
  if (a2dp_lhdc_encoder_cb.has_lhdc_abr_handle) {
    dprintf(fd,
            "  LHDC adaptive bit rate encode quality mode index        : %d\n",
            a2dp_lhdc_encoder_cb.last_lhdc_abr_eqmid);
    dprintf(fd,
            "  LHDC adaptive bit rate adjustments                      : %zu\n",
            a2dp_lhdc_encoder_cb.lhdc_abr_adjustments);
  }
  */
}
