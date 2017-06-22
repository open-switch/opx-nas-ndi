/*
 * Copyright (c) 2017 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/*
 * filename: nas_ndi_sw_profile.h
 */


#ifndef __NAS_NDI_SW_PROFILE_H
#define __NAS_NDI_SW_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * As part of nas_ndi_init, this function initializes KEY,VALUE pair.
 * As part of nas_switch_init switch.xml file is read and configurations are
 * stored in nas-common. This function reads configurable values if any, and
 * populates the KEY,VALUE pair.
 * As part of SAI initialization, SAI gets these key-value pair and initalizes the
 * NPU.
 * @return  void
 */
void nas_ndi_populate_cfg_key_value_pair (uint32_t switch_id);
const char* ndi_profile_get_value(sai_switch_profile_id_t profile_id, const char* variable);
int ndi_profile_get_next_value(sai_switch_profile_id_t profile_id, const char** variable,
                           const char** value);

/**
 *  \}
 */

#ifdef __cplusplus
}
#endif
#endif

