/*
 * Copyright (c) 2019 Dell Inc.
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
 * filename: nas_ndi_stg_util.h
 */

#ifndef NAS_NDI_STG_UTIL_H_
#define NAS_NDI_STG_UTIL_H_

#include "std_error_codes.h"
#include "ds_common_types.h"
#include "nas_ndi_common.h"
#include "std_mutex_lock.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Delete all STP port association for a given port
 *
 * @param npu_id - NPU ID
 *
 * @param npu_port_id - NPU Port ID
 *
 * @return STD_ERR_OK if operation is successful otherwise a different
 *  error code is returned.
 */

t_std_error ndi_stg_delete_port_stp_ports(npu_id_t npu_id,
        sai_object_id_t brport);

std_mutex_type_t * ndi_stg_get_member_mutex();

#ifdef __cplusplus
}
#endif
#endif
