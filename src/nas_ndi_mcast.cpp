/*
 * Copyright (c) 2019 Dell Inc.
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
#include "saiipmcextensions.h"
#include "saistatus.h"
#include "nas_ndi_int.h"
#include "nas_ndi_mcast.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_ipmc_utl.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_vlan_util.h"
#include "nas_ndi_bridge_port.h"
#include "std_ip_utils.h"

#include <inttypes.h>
#include <sstream>

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
    sai_attr[0].value.s32 =
        (mc_entry_p->copy_to_cpu ? SAI_PACKET_ACTION_LOG : SAI_PACKET_ACTION_FORWARD);
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

t_std_error ndi_mcast_entry_update(npu_id_t npu_id, const ndi_mcast_entry_t *mc_entry_p,
                                   ndi_mcast_update_type_t upd_type)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_l2mc_entry_t sai_entry;
    if (!ndi_mcast_entry_params_copy(&sai_entry, npu_id, mc_entry_p)) {
        return STD_ERR(MCAST, FAIL, 0);
    }

    sai_attribute_t sai_attr;
    switch(upd_type) {
    case NAS_NDI_MCAST_UPD_GRP:
        sai_attr.id = SAI_L2MC_ENTRY_ATTR_OUTPUT_GROUP_ID;
        sai_attr.value.oid = (sai_object_id_t)mc_entry_p->group_id;
        break;
    case NAS_NDI_MCAST_UPD_COPY_TO_CPU:
        sai_attr.id = SAI_L2MC_ENTRY_ATTR_PACKET_ACTION;
        sai_attr.value.s32 =
            (mc_entry_p->copy_to_cpu ? SAI_PACKET_ACTION_LOG : SAI_PACKET_ACTION_FORWARD);
        break;
    default:
        NDI_MCAST_LOG_ERROR("Invalid update type");
        return STD_ERR(MCAST, PARAM, 0);
    }
    sai_status_t sai_ret = ndi_mcast_api_get(ndi_db_ptr)->set_l2mc_entry_attribute(&sai_entry,
                                                                &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to update multicast entry");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

static inline sai_ipmc_api_t *ndi_ipmc_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return ndi_db_ptr->ndi_sai_api_tbl.n_sai_ipmc_api_tbl;
}

static bool ndi_ipmc_entry_params_copy(sai_ipmc_entry_extn_t *p_sai_entry,
                                       npu_id_t npu_id,
                                       const ndi_ipmc_entry_t *p_mc_entry)
{
    memset(p_sai_entry, 0, sizeof(sai_ipmc_entry_extn_t));
    p_sai_entry->switch_id = ndi_switch_id_get();
    p_sai_entry->vr_id = static_cast<sai_object_id_t>(p_mc_entry->vrf_id);
    if (p_sai_entry->vr_id == SAI_NULL_OBJECT_ID) {
        NDI_MCAST_LOG_ERROR("Failed to get VRF object ID from VID %ld",
                             p_mc_entry->vrf_id);
        return false;
    }
    p_sai_entry->inrif_id = static_cast<sai_object_id_t>(p_mc_entry->iif_rif_id);
    ndi_sai_ip_address_copy(&p_sai_entry->destination, &p_mc_entry->dst_ip);
    ndi_sai_ip_address_copy(&p_sai_entry->source, &p_mc_entry->src_ip);
    if (p_mc_entry->type == NAS_NDI_IPMC_ENTRY_TYPE_SG) {
        p_sai_entry->type = SAI_IPMC_ENTRY_TYPE_SG;
    } else {
        p_sai_entry->type = SAI_IPMC_ENTRY_TYPE_XG;
    }
    return true;
}

std::string ndi_ipmc_ip_to_string(const hal_ip_addr_t& ip_addr)
{
    static char ip_buf[HAL_INET6_TEXT_LEN + 1];
    std::string ret_ip_str{};
    const char* ip_str = std_ip_to_string(&ip_addr, ip_buf, sizeof(ip_buf));
    if (ip_str != nullptr) {
        ret_ip_str = ip_str;
    }
    return ret_ip_str;
}

static std::string ndi_ipmc_entry_to_string(const ndi_ipmc_entry_t& ipmc_entry)
{
    std::ostringstream ss;
    ss << "VRF=" << std::hex << std::showbase << ipmc_entry.vrf_id << " ";
    ss << "IIF=" << std::hex << std::showbase << ipmc_entry.iif_rif_id << " ";
    ss << "GRP=" << ndi_ipmc_ip_to_string(ipmc_entry.dst_ip) << " ";
    ss << "SRC=";
    if (ipmc_entry.type == NAS_NDI_IPMC_ENTRY_TYPE_XG) {
        ss << "*";
    } else {
        ss << ndi_ipmc_ip_to_string(ipmc_entry.src_ip);
    }
    return ss.str();
}

t_std_error ndi_ipmc_entry_create(npu_id_t npu_id, const ndi_ipmc_entry_t *ipmc_entry_p)
{
    NDI_MCAST_LOG_INFO("Create IPMC entry: %s", ndi_ipmc_entry_to_string(*ipmc_entry_p).c_str());
    NDI_MCAST_LOG_INFO("Copy to CPU: %s", ipmc_entry_p->copy_to_cpu ? "TRUE" : "FALSE");

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_ipmc_entry_extn_t sai_entry;
    if (!ndi_ipmc_entry_params_copy(&sai_entry, npu_id, ipmc_entry_p)) {
        NDI_MCAST_LOG_ERROR("Failed to copy IPMC entry parameters");
        return STD_ERR(MCAST, FAIL, 0);
    }

    ndi_obj_id_t rpf_grp_id, ipmc_grp_id;
    ipmc_grp_id = 0;
    if (ndi_ipmc_get_repl_subgrp_id(npu_id, ipmc_entry_p->repl_group_id, rpf_grp_id, ipmc_grp_id)
        != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Failed get RPF and IPMC group id for repl id %ld",
                            ipmc_entry_p->repl_group_id);
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_attribute_t sai_attr[3];
    size_t attr_cnt = 1;
    sai_attr[0].id = SAI_IPMC_ENTRY_ATTR_PACKET_ACTION;
    sai_attr[0].value.s32 =
        (ipmc_entry_p->copy_to_cpu ? SAI_PACKET_ACTION_LOG : SAI_PACKET_ACTION_FORWARD);
    sai_attr[1].id = SAI_IPMC_ENTRY_ATTR_RPF_GROUP_ID;
    sai_attr[1].value.oid = static_cast<sai_object_id_t>(rpf_grp_id);
    attr_cnt++;
    if (ipmc_grp_id > 0) {
        sai_attr[2].id = SAI_IPMC_ENTRY_ATTR_OUTPUT_GROUP_ID;
        sai_attr[2].value.oid = static_cast<sai_object_id_t>(ipmc_grp_id);
        attr_cnt++;
    }

    sai_status_t sai_ret = ndi_ipmc_api_get(ndi_db_ptr)->create_ipmc_entry((sai_ipmc_entry_t *)&sai_entry,
                                attr_cnt, sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to create IPMC entry");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    ndi_ipmc_cache_add_entry(npu_id, *ipmc_entry_p);

    return STD_ERR_OK;
}

t_std_error ndi_ipmc_entry_delete(npu_id_t npu_id, const ndi_ipmc_entry_t *ipmc_entry_p)
{
    NDI_MCAST_LOG_INFO("Delete IPMC entry: %s", ndi_ipmc_entry_to_string(*ipmc_entry_p).c_str());

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_ipmc_entry_extn_t sai_entry;
    if (!ndi_ipmc_entry_params_copy(&sai_entry, npu_id, ipmc_entry_p)) {
        return STD_ERR(MCAST, FAIL, 0);
    }

    sai_status_t sai_ret = ndi_ipmc_api_get(ndi_db_ptr)->remove_ipmc_entry((sai_ipmc_entry_t *)&sai_entry);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to delete IPMC entry");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    ndi_ipmc_cache_del_entry(npu_id, *ipmc_entry_p);

    return STD_ERR_OK;
}

t_std_error ndi_ipmc_entry_update(npu_id_t npu_id, const ndi_ipmc_entry_t *ipmc_entry_p,
                                  ndi_ipmc_update_type_t upd_type)
{
    NDI_MCAST_LOG_INFO("Update IPMC entry: %s", ndi_ipmc_entry_to_string(*ipmc_entry_p).c_str());

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_ipmc_entry_extn_t sai_entry;
    if (!ndi_ipmc_entry_params_copy(&sai_entry, npu_id, ipmc_entry_p)) {
        return STD_ERR(MCAST, FAIL, 0);
    }

    ndi_obj_id_t rpf_grp_id, ipmc_grp_id;
    ipmc_grp_id = 0;
    if (ndi_ipmc_get_repl_subgrp_id(npu_id, ipmc_entry_p->repl_group_id, rpf_grp_id, ipmc_grp_id)
        != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Failed get RPF and IPMC group id for repl id %ld",
                            ipmc_entry_p->repl_group_id);
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_attribute_t sai_attr[3];
    size_t attr_cnt = 0;
    switch(upd_type) {
    case NAS_NDI_IPMC_UPD_REPL_GRP:
        NDI_MCAST_LOG_INFO("Update replication group to %ld: RPF_GRP_ID=%ld IPMC_GRP_ID=%ld",
                            ipmc_entry_p->repl_group_id, rpf_grp_id, ipmc_grp_id);
        NDI_MCAST_LOG_INFO("Update copy_to_cpu to %s", ipmc_entry_p->copy_to_cpu ? "TRUE" : "FALSE");
        sai_attr[0].id = SAI_IPMC_ENTRY_ATTR_RPF_GROUP_ID;
        sai_attr[0].value.oid = static_cast<sai_object_id_t>(rpf_grp_id);
        attr_cnt++;

        //for now SAI doesn't manage internally COPY_TO_CPU for a route.
        //On certain platforms, when CopyToCpu is enabled, the CPU port is added to
        //l2 ports of the repl. group entry. Hence it always results in an update
        //to repl. group id.
        //Hence irrespective of whether CopyToCpu changed or not, anytime there is an
        //update to repl. group id, CopyToCpu attribute is also updated to SAI.
        sai_attr[1].id = SAI_IPMC_ENTRY_ATTR_PACKET_ACTION;
        sai_attr[1].value.s32 =
            (ipmc_entry_p->copy_to_cpu ? SAI_PACKET_ACTION_LOG : SAI_PACKET_ACTION_FORWARD);
        attr_cnt++;
        // IPMC can be 0 in case route with no OIF's, do not send ipmc attr
        if (ipmc_grp_id > 0) {
            sai_attr[2].id = SAI_IPMC_ENTRY_ATTR_OUTPUT_GROUP_ID;
            sai_attr[2].value.oid = static_cast<sai_object_id_t>(ipmc_grp_id);
            attr_cnt++;
        }
        break;
    case NAS_NDI_IPMC_UPD_COPY_TO_CPU:
        NDI_MCAST_LOG_INFO("Update copy_to_cpu to %s", ipmc_entry_p->copy_to_cpu ? "TRUE" : "FALSE");
        sai_attr[0].id = SAI_IPMC_ENTRY_ATTR_PACKET_ACTION;
        sai_attr[0].value.s32 =
            (ipmc_entry_p->copy_to_cpu ? SAI_PACKET_ACTION_LOG : SAI_PACKET_ACTION_FORWARD);
        attr_cnt = 1;
        break;
    default:
        NDI_MCAST_LOG_ERROR("Invalid update type");
        return STD_ERR(MCAST, PARAM, 0);
    }

    for (size_t idx = 0; idx < attr_cnt; idx ++) {
        sai_status_t sai_ret = ndi_ipmc_api_get(ndi_db_ptr)->set_ipmc_entry_attribute(
                                    (sai_ipmc_entry_t *)&sai_entry, &sai_attr[idx]);
        if (sai_ret != SAI_STATUS_SUCCESS) {
            NDI_MCAST_LOG_ERROR("Failed to set IPMC entry attribute");
            return STD_ERR(MCAST, FAIL, sai_ret);
        }
    }

    ndi_ipmc_cache_update_entry(npu_id, *ipmc_entry_p, upd_type);

    return STD_ERR_OK;
}

t_std_error ndi_ipmc_entry_get(npu_id_t npu_id, ndi_ipmc_entry_t *ipmc_entry_p)
{
    NDI_MCAST_LOG_INFO("Get IPMC entry: %s", ndi_ipmc_entry_to_string(*ipmc_entry_p).c_str());
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_ipmc_entry_extn_t sai_entry;
    if (!ndi_ipmc_entry_params_copy(&sai_entry, npu_id, ipmc_entry_p)) {
        return STD_ERR(MCAST, FAIL, 0);
    }

    sai_attribute_t sai_attr[2] = {};
    size_t attr_count = 2;
    sai_attr[0].id = SAI_IPMC_ENTRY_ATTR_PACKET_ACTION;
    sai_attr[1].id = SAI_IPMC_ENTRY_EXTENSION_ATTR_IPMC_SG_HIT;

    sai_status_t sai_ret = ndi_ipmc_api_get(ndi_db_ptr)->get_ipmc_entry_attribute(
                                (sai_ipmc_entry_t *)&sai_entry, attr_count, sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to get IPMC entry attribute");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }
    ipmc_entry_p->copy_to_cpu = (sai_attr[0].value.s32 == SAI_PACKET_ACTION_LOG);
    ipmc_entry_p->route_hit = sai_attr[1].value.booldata;

    NDI_MCAST_LOG_INFO("route HIT bit status %d ", sai_attr[1].value.booldata);

    return STD_ERR_OK;
}
