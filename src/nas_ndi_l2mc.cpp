/*
 * Copyright (c) 2017 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/*
 * filename: nas_ndi_l2mc.cpp
 */

#include "std_ip_utils.h"
#include "sail2mcgroup.h"
#include "saistatus.h"
#include "nas_ndi_int.h"
#include "nas_ndi_l2mc.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_bridge_port.h"

#include <inttypes.h>

static inline sai_l2mc_group_api_t *ndi_l2mc_group_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return ndi_db_ptr->ndi_sai_api_tbl.n_sai_l2mc_grp_api_tbl;
}

t_std_error ndi_l2mc_group_create(npu_id_t npu_id, ndi_obj_id_t *mc_grp_id_p)
{
    if (mc_grp_id_p == nullptr) {
        NDI_MCAST_LOG_ERROR("NULL pointer is not accepted for returning group ID");
        return STD_ERR(MCAST, PARAM, 0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_object_id_t sai_grp_id;
    sai_status_t sai_ret = ndi_l2mc_group_api_get(ndi_db_ptr)->create_l2mc_group(&sai_grp_id,
                                    ndi_switch_id_get(), 0, nullptr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to create multicast group");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    *mc_grp_id_p = (ndi_obj_id_t)sai_grp_id;

    return STD_ERR_OK;
}

t_std_error ndi_l2mc_group_delete(npu_id_t npu_id, ndi_obj_id_t mc_grp_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_status_t sai_ret = ndi_l2mc_group_api_get(ndi_db_ptr)->remove_l2mc_group(
                                    (sai_object_id_t)mc_grp_id);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to delete multicast group");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

static t_std_error ndi_l2mc_group_add_member_int(npu_id_t npu_id, sai_object_id_t group_id,
                                                  sai_object_id_t output_id,
                                                  sai_object_id_t *member_id_p)
{
    if (member_id_p == nullptr) {
        NDI_MCAST_LOG_ERROR("NULL pointer is not accepted for returning member ID");
        return STD_ERR(MCAST, PARAM, 0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_object_id_t sai_grp_member_id;
    sai_attribute_t sai_attr[NDI_MAX_MC_GRP_MEMBER_ATTR];
    sai_attr[0].id = SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_GROUP_ID;
    sai_attr[0].value.oid = group_id;
    sai_attr[1].id = SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_OUTPUT_ID;
    sai_attr[1].value.oid = output_id;

    sai_status_t sai_ret = ndi_l2mc_group_api_get(ndi_db_ptr)->create_l2mc_group_member(
                                    &sai_grp_member_id,
                                    ndi_switch_id_get(),
                                    NDI_MAX_MC_GRP_MEMBER_ATTR, sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to add multicast group member");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    *member_id_p = sai_grp_member_id;

    return STD_ERR_OK;
}

t_std_error ndi_l2mc_group_add_port_member(npu_id_t npu_id,
                                            ndi_obj_id_t group_id, port_t port_id,
                                            ndi_obj_id_t *member_id_p)
{
    sai_object_id_t sai_port;
    t_std_error ret_code = ndi_sai_port_id_get(npu_id, port_id, &sai_port);
    if (ret_code != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Could not find SAI port ID for npu %d port %d",
                            npu_id, port_id);
        return ret_code;
    }
    sai_object_id_t bridge_port;
    if (!ndi_get_1q_bridge_port(&bridge_port, sai_port)) {
        NDI_MCAST_LOG_ERROR("Could not find bridge port ID for SAI port 0x%" PRIx64,
                            sai_port);
        return STD_ERR(MCAST, FAIL, 0);
    }

    return ndi_l2mc_group_add_member_int(npu_id, (sai_object_id_t)group_id, bridge_port,
                                          (sai_object_id_t *)member_id_p);
}

t_std_error ndi_l2mc_group_add_lag_member(npu_id_t npu_id,
                                           ndi_obj_id_t group_id, ndi_obj_id_t lag_id,
                                           ndi_obj_id_t *member_id_p)
{
    sai_object_id_t bridge_port;
    if (!ndi_get_1q_bridge_port(&bridge_port, static_cast<sai_object_id_t>(lag_id))) {
        NDI_MCAST_LOG_ERROR("Could not find bridge port ID for LAG ID 0x%" PRIx64,
                            lag_id);
        return STD_ERR(MCAST, FAIL, 0);
    }
    return ndi_l2mc_group_add_member_int(npu_id, (sai_object_id_t)group_id, bridge_port,
                                          (sai_object_id_t *)member_id_p);
}

t_std_error ndi_l2mc_group_delete_member(npu_id_t npu_id, ndi_obj_id_t member_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_status_t sai_ret = ndi_l2mc_group_api_get(ndi_db_ptr)->remove_l2mc_group_member(
                                    (sai_object_id_t)member_id);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to delete multicast group member");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}
