/*
 * Copyright (c) 2018 Dell Inc.
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
 * filename: nas_ndi_mcast.cpp
 */

#include "std_ip_utils.h"
#include "sail2mc.h"
#include "saistatus.h"
#include "nas_ndi_int.h"
#include "nas_ndi_mcast.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_vlan_util.h"
#include "nas_ndi_bridge_port.h"

#include <inttypes.h>

static inline sai_l2mc_api_t *ndi_mcast_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return ndi_db_ptr->ndi_sai_api_tbl.n_sai_mcast_api_tbl;
}

static void ndi_sai_ip_address_copy(sai_ip_address_t *sai_ip_addr,const hal_ip_addr_t *ip_addr)
{
    if (STD_IP_IS_AFINDEX_V4(ip_addr->af_index)) {
        sai_ip_addr->addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        sai_ip_addr->addr.ip4 = ip_addr->u.v4_addr;
    } else {
        sai_ip_addr->addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        memcpy (sai_ip_addr->addr.ip6, ip_addr->u.v6_addr, sizeof (sai_ip6_t));
    }
}

static bool ndi_mcast_entry_params_copy(sai_l2mc_entry_t *p_sai_entry,
                                        npu_id_t npu_id,
                                        const ndi_mcast_entry_t *p_mc_entry)
{
    memset(p_sai_entry, 0, sizeof(sai_l2mc_entry_t));
    p_sai_entry->switch_id = ndi_switch_id_get();
    p_sai_entry->bv_id = ndi_get_sai_vlan_obj_id(npu_id, p_mc_entry->vlan_id);
    if (p_sai_entry->bv_id == SAI_NULL_OBJECT_ID) {
        NDI_MCAST_LOG_ERROR("Failed to get VLAN object ID from VID %d",
                             p_mc_entry->vlan_id);
        return false;
    }
    ndi_sai_ip_address_copy(&p_sai_entry->destination, &p_mc_entry->dst_ip);
    ndi_sai_ip_address_copy(&p_sai_entry->source, &p_mc_entry->src_ip);
    if (p_mc_entry->type == NAS_NDI_MCAST_ENTRY_TYPE_SG) {
        p_sai_entry->type = SAI_L2MC_ENTRY_TYPE_SG;
    } else {
        p_sai_entry->type = SAI_L2MC_ENTRY_TYPE_XG;
    }
    return true;
}

t_std_error ndi_mcast_entry_create(npu_id_t npu_id, const ndi_mcast_entry_t *mc_entry_p)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_l2mc_entry_t sai_entry;
    if (!ndi_mcast_entry_params_copy(&sai_entry, npu_id, mc_entry_p)) {
        return STD_ERR(MCAST, FAIL, 0);
    }

    sai_attribute_t sai_attr[NDI_MAX_MC_ENTRY_ATTR];
    sai_attr[0].id = SAI_L2MC_ENTRY_ATTR_PACKET_ACTION;
    sai_attr[0].value.s32 = SAI_PACKET_ACTION_FORWARD;
    sai_attr[1].id = SAI_L2MC_ENTRY_ATTR_OUTPUT_GROUP_ID;
    sai_attr[1].value.oid = (sai_object_id_t)mc_entry_p->group_id;

    sai_status_t sai_ret = ndi_mcast_api_get(ndi_db_ptr)->create_l2mc_entry(&sai_entry,
                                NDI_MAX_MC_ENTRY_ATTR, sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to create multicast entry");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_mcast_entry_delete(npu_id_t npu_id, const ndi_mcast_entry_t *mc_entry_p)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_l2mc_entry_t sai_entry;
    if (!ndi_mcast_entry_params_copy(&sai_entry, npu_id, mc_entry_p)) {
        return STD_ERR(MCAST, FAIL, 0);
    }

    sai_status_t sai_ret = ndi_mcast_api_get(ndi_db_ptr)->remove_l2mc_entry(&sai_entry);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to delete multicast entry");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}
