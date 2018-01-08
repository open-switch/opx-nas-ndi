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
 * filename: nas_ndi_vlan_util.h
 */

#ifndef NAS_NDI_VLAN_UTIL_H_
#define NAS_NDI_VLAN_UTIL_H_

#include "std_error_codes.h"
#include "ds_common_types.h"
#include "nas_ndi_common.h"
#include "saitypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Change the STP instance for given VLAN Id
 *
 * @param[in] npu_id - NPU ID
 *
 * @param[in] vlan_id - VLAN ID
 *
 * @param[in] stp_id - SAI STP id to be associated with
 *
 * @return STD_ERR_OK if operation is successful otherwise a different
 *  error code is returned.
 */

t_std_error ndi_set_vlan_stp_instance(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                                  sai_object_id_t stp_id);

/**
 * @brief Get SAI VLAN UOID for given VLAN Id and NPU Id
 *
 * @param[in] npu_id - NPU ID
 *
 * @param[in] vlan_id - VLAN ID
 *
 * @return valid SAI VLAN UOID else SAI_NULL_OBJECT_ID
 */
sai_object_id_t ndi_get_sai_vlan_obj_id(npu_id_t npu_id,
        hal_vlan_id_t vlan_id);

/**
 * @brief Get VLAN Id for given NPU Id and SAI VLAN UOID
 *
 * @param[in] npu_id - NPU ID
 *
 * @param[in] vlan_obj_id - SAI VLAN UOID
 *
 * @param[out] vlan_id - VLAN Id
 *
 * @return STD_ERR_OK if operation is successful otherwise a different
 *  error code is returned.
 */
t_std_error ndi_get_sai_vlan_id(npu_id_t npu_id, sai_object_id_t vlan_obj_id,
        hal_vlan_id_t *vlan_id);

t_std_error ndi_vlan_delete_default_member_brports(npu_id_t npu_id, sai_object_id_t brport, bool del_all);

bool ndi_vlan_get_default_obj_id(npu_id_t npu_id);

#ifdef __cplusplus
}
#endif
#endif
