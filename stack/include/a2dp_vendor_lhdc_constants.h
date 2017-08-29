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

//
// A2DP constants for LHDC codec
//

#ifndef A2DP_VENDOR_LHDC_CONSTANTS_H
#define A2DP_VENDOR_LHDC_CONSTANTS_H

// LHDC Quality Mode Index
#define A2DP_LHDC_QUALITY_HIGH 0  // Equal to LHDCBT_EQMID_HQ 990kbps
#define A2DP_LHDC_QUALITY_MID 1   // Equal to LHDCBT_EQMID_SQ 660kbps
#define A2DP_LHDC_QUALITY_LOW 2   // Equal to LHDCBT_EQMID_MQ 330kbps
//LHDC not supported auto bit rate now.
//#define A2DP_LHDC_QUALITY_ABR 3   // ABR mode, range: 990,660,492,396,330(kbps)

// Length of the LHDC Media Payload header
#define A2DP_LHDC_MPL_HDR_LEN 1

// LHDC Media Payload Header
#define A2DP_LHDC_HDR_F_MSK 0x80
#define A2DP_LHDC_HDR_S_MSK 0x40
#define A2DP_LHDC_HDR_L_MSK 0x20
#define A2DP_LHDC_HDR_NUM_MSK 0x0F

// LHDC codec specific settings
#define A2DP_LHDC_CODEC_LEN 9
// [Octet 0-3] Vendor ID
#define A2DP_LHDC_VENDOR_ID 0x0000053a
// [Octet 4-5] Vendor Specific Codec ID
#define A2DP_LHDC_CODEC_ID 0x484C
// [Octet 6], [Bits 0-2] Sampling Frequency
#define A2DP_LHDC_SAMPLING_FREQ_MASK 0x07
#define A2DP_LHDC_SAMPLING_FREQ_44100 0x04
#define A2DP_LHDC_SAMPLING_FREQ_48000 0x02
#define A2DP_LHDC_SAMPLING_FREQ_96000 0x01

// [Octet 6], [Bits 3-4] Bit dipth
#define A2DP_LHDC_BIT_FMT_MASK 	 0x18
#define A2DP_LHDC_BIT_FMT_24	 0x08
#define A2DP_LHDC_BIT_FMT_16	 0x10



#define A2DP_LHDC_CHANNEL_MODE_MASK 0x07
#define A2DP_LHDC_CHANNEL_MODE_MONO 0x04
#define A2DP_LHDC_CHANNEL_MODE_DUAL 0x02
#define A2DP_LHDC_CHANNEL_MODE_STEREO 0x01

#endif  // A2DP_VENDOR_LHDC_CONSTANTS_H
