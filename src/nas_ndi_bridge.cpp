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
 * filename: nas_ndi_bridge.cpp
 */

#include "std_error_codes.h"
#include "std_assert.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_obj_cache.h"
#include "nas_ndi_bridge_port.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_stg_util.h"
#include "nas_ndi_vlan_util.h"
#include "nas_ndi_port_utils.h"
#include "nas_ndi_1d_bridge.h"

#include "sai.h"
#include "saitypes.h"
#include "saibridge.h"
#include "dell-interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <vector>
#include <map>
#include <functional>
#include <unordered_map>

#define SAI_ATTRS_LEN (4 + 3)

typedef struct bridge_port_create_info {
    npu_id_t npu_id;
    ndi_brport_obj_t data;
    sai_object_id_t bridge_oid;
    bool tag;
} bridge_port_create_info_t;

extern "C"
{

static sai_object_id_t default_1q_br_oid;

t_std_error ndi_set_bridge_port_attribute(npu_id_t npu_id, sai_object_id_t brport_oid, sai_attribute_t *sai_attr);

static inline  sai_bridge_api_t *ndi_sai_bridge_api(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_bridge_api_tbl);
}

static t_std_error  ndi_init_get_default_bridge_oid (npu_id_t npu) {

    sai_attribute_t attr;
    t_std_error ret = STD_ERR_OK;
    attr.id = SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID;
    size_t count  =1;
    if ((ret = ndi_switch_attr_get(npu, &attr, count)) != STD_ERR_OK) {
       return ret;
    }
    default_1q_br_oid = attr.value.oid;
    NDI_PORT_LOG_TRACE("Default bridge id %" PRIx64 " ",default_1q_br_oid);
    return ret;
}

static t_std_error ndi_bridge_port_stats_helper(npu_id_t npu_id, ndi_obj_id_t brport_oid,
                                         ndi_stat_id_t *ndi_stat_ids,
                                         uint64_t* stats_val, size_t len)
{
    const unsigned int list_len = len;
    sai_bridge_port_stat_t sai_bridge_port_stats_ids[list_len];
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_VLAN_LOG_ERROR("Invalid NPU Id %d passed",npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_bridge_port_stats(ndi_stat_ids[ix], &sai_bridge_port_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->get_bridge_port_stats(brport_oid,
                    len, (const _sai_bridge_port_stat_t*) sai_bridge_port_stats_ids, stats_val)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Bridge port stats Get failed for npu %d, Bridge port %d, ret %d \n",
                            npu_id, brport_oid, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }
    return STD_ERR_OK;
}

static t_std_error ndi_bridge_port_stats_clear_helper(npu_id_t npu_id, ndi_obj_id_t brport_oid,
                                                      ndi_stat_id_t *ndi_stat_ids,
                                                      size_t len)
{
    const unsigned int list_len = len;
    sai_bridge_port_stat_t sai_bridge_port_stats_ids[list_len];
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_VLAN_LOG_ERROR("Invalid NPU Id %d passed",npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_bridge_port_stats(ndi_stat_ids[ix], &sai_bridge_port_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->clear_bridge_port_stats(brport_oid,
                    len, (const _sai_bridge_port_stat_t*) sai_bridge_port_stats_ids)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Bridge port stats clear failed for npu %d, Bridge port %llu, ret %d \n",
                            npu_id, brport_oid, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }
    return STD_ERR_OK;
}

#define MAX_PORT_BUF 512

/* Called at init to initialize bridge port map table */
t_std_error ndi_init_brport_for_1Q(void)
{

    sai_status_t  sai_ret = SAI_STATUS_FAILURE;
    ndi_brport_obj_t bp_map;
    sai_attribute_t bridge_port_attr;
    memset(&bp_map, 0, sizeof(ndi_brport_obj_t));
    sai_attribute_t bridge_attr;
    uint_t npu_id = 0;
    std::vector<sai_object_id_t> tmp_lst(MAX_PORT_BUF);


    if (ndi_init_get_default_bridge_oid(npu_id) != STD_ERR_OK){
        return STD_ERR(NPU, CFG,0);
    }
    bridge_attr.id = SAI_BRIDGE_ATTR_PORT_LIST;
    bridge_attr.value.objlist.count = MAX_PORT_BUF;
    bridge_attr.value.objlist.list = &tmp_lst[0];

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }
    if((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->get_bridge_attribute(default_1q_br_oid, 1, &bridge_attr))
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Get bridge ports for 1Q bridge failed");
        return STD_ERR(NPU, CFG, sai_ret);
    }

    sai_object_id_t bridge_port_id;
    /* get physical port id from bridge port id & store in mapping */
    for (uint32_t i = 0; i < bridge_attr.value.objlist.count; ++i) {
        bridge_port_id = bridge_attr.value.objlist.list[i];
        bridge_port_attr.id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
        if((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->get_bridge_port_attribute(bridge_port_id,
              1, &bridge_port_attr)) != SAI_STATUS_SUCCESS) {
            NDI_PORT_LOG_ERROR("Get  port_id failed for 1Q bridgeport : %" PRIx64 " ", bridge_port_id);
            return STD_ERR(NPU, CFG, sai_ret);
        }

        bp_map.brport_type = ndi_brport_type_PORT;
        bp_map.port_type = ndi_port_type_PORT;
        bp_map.brport_obj_id = bridge_port_id;
        bp_map.port_obj_id = bridge_port_attr.value.oid;
        /* Add to cache */
        if (!nas_ndi_add_bridge_port_obj(&bp_map)) {
            NDI_PORT_LOG_ERROR("INIT map add port failed: bridge port id %" PRIx64 " " "port id %" PRIx64 " ",
                                           bridge_port_id, bridge_port_attr.value.oid);

            return STD_ERR(NPU, CFG, sai_ret);
        }
        NDI_PORT_LOG_TRACE("INIT : bridge port id %" PRIx64 " " "port id %" PRIx64 " ",
            bridge_port_id, bridge_port_attr.value.oid);

        /* Set bridgeport to ADMIN UP */
        bridge_port_attr.id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
        bridge_port_attr.value.booldata = true;

        if ((sai_ret = ndi_set_bridge_port_attribute (npu_id, bridge_port_id, &bridge_port_attr)) != STD_ERR_OK) {
            return STD_ERR(NPU, CFG, sai_ret);
        }

        /* Set bridgeport ingress & egress filtering to enable i.e drop unknown vlan packets */
        bridge_port_attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
        bridge_port_attr.value.booldata = true;

        if ((sai_ret = ndi_set_bridge_port_attribute (npu_id, bridge_port_id, &bridge_port_attr)) != STD_ERR_OK) {
            return STD_ERR(NPU, CFG, sai_ret);
        }
        bridge_port_attr.id = SAI_BRIDGE_PORT_ATTR_EGRESS_FILTERING;
        bridge_port_attr.value.booldata = true;

        if ((sai_ret = ndi_set_bridge_port_attribute (npu_id, bridge_port_id, &bridge_port_attr)) != STD_ERR_OK) {
            return STD_ERR(NPU, CFG, sai_ret);
        }
    }

    if(!ndi_vlan_get_default_obj_id(npu_id)){
        return STD_ERR(NPU,FAIL,0);
    }

    return STD_ERR_OK;
}


static bool ndi2sai_brport_type(ndi_brport_type_t br_type, sai_bridge_port_type_t *sai_id)
{
    static const auto ndi2sai_brport_type_map =
             new std::map<ndi_brport_type_t, sai_bridge_port_type_t>
    {
        {ndi_brport_type_PORT ,SAI_BRIDGE_PORT_TYPE_PORT},
        {ndi_brport_type_SUBPORT_TAG, SAI_BRIDGE_PORT_TYPE_SUB_PORT},
        {ndi_brport_type_SUBPORT_UNTAG, SAI_BRIDGE_PORT_TYPE_SUB_PORT},
        {ndi_brport_type_1Q_ROUTER, SAI_BRIDGE_PORT_TYPE_1Q_ROUTER},
        {ndi_brport_type_1D_ROUTER, SAI_BRIDGE_PORT_TYPE_1D_ROUTER},
        {ndi_brport_type_TUNNEL, SAI_BRIDGE_PORT_TYPE_TUNNEL },
    };
    auto it = ndi2sai_brport_type_map->find(br_type);
    if(it == ndi2sai_brport_type_map->end()|| (sai_id == NULL)){
        NDI_PORT_LOG_ERROR("Invalid get sai type for ndi_bridge_port type %d", br_type);
        return false;
    }

    *sai_id = it->second;
    return true;
}

t_std_error ndi_create_bridge_1d(npu_id_t npu_id, sai_object_id_t *br_oid) {

    sai_attribute_t  bridge_attr_list;
    sai_status_t   sai_ret = SAI_STATUS_FAILURE;
    bridge_attr_list.id=SAI_BRIDGE_ATTR_TYPE;
    bridge_attr_list.value.s32=SAI_BRIDGE_TYPE_1D;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }

    if ((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->create_bridge(br_oid,ndi_switch_id_get(), 1, &bridge_attr_list))
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Create 1D bridge failed in SAI  error %u", sai_ret);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    NDI_PORT_LOG_TRACE(" 1D bridge created with br OID : % " PRIx64 " ", *br_oid);
    return STD_ERR_OK;

}

t_std_error ndi_delete_bridge_1d(npu_id_t npu_id, sai_object_id_t br_oid) {

    sai_status_t  sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }
    if ((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->remove_bridge(br_oid))
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Delete 1D bridge failed in SAI  error %u", sai_ret);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    NDI_PORT_LOG_TRACE(" 1D bridge deleted with br OID : % " PRIx64 " ", br_oid);
    return STD_ERR_OK;


}

t_std_error ndi_bridge_port_stats_get(npu_id_t npu_id, npu_port_t port, hal_vlan_id_t vlan_id,
                                      ndi_stat_id_t *ndi_stat_ids,
                                      uint64_t* stats_val, size_t len)
{
    sai_object_id_t sai_port;
    sai_object_id_t brport_oid;
    t_std_error ret_code;

    if ((ret_code = ndi_sai_port_id_get(npu_id, port, &sai_port) != STD_ERR_OK)) {
        NDI_PORT_LOG_ERROR("Del 1D port: failed to get sai_port id for NPU port %u", port);
        return ret_code;
    }

    if (!ndi_get_1d_bridge_port(&brport_oid, sai_port , vlan_id)) {
        NDI_PORT_LOG_ERROR("1D port stats failed to get bridge port for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            sai_port, vlan_id);
        return STD_ERR(NPU, CFG, 0);
    }

    NDI_PORT_LOG_TRACE("port stats for 1D npu_port %" PRIx64 " " "vlan_id %u", port, vlan_id);
    return ndi_bridge_port_stats_helper(npu_id, brport_oid, ndi_stat_ids, stats_val, len);

}

t_std_error ndi_bridge_port_stats_clear(npu_id_t npu_id, npu_port_t port, hal_vlan_id_t vlan_id,
                                        ndi_stat_id_t *ndi_stat_ids,size_t len)
{
    sai_object_id_t sai_port;
    sai_object_id_t brport_oid;
    t_std_error ret_code;

    if ((ret_code = ndi_sai_port_id_get(npu_id, port, &sai_port) != STD_ERR_OK)) {
        NDI_PORT_LOG_ERROR("Del 1D port: failed to get sai_port id for NPU port %u", port);
        return ret_code;
    }

    if (!ndi_get_1d_bridge_port(&brport_oid, sai_port , vlan_id)) {
        NDI_PORT_LOG_ERROR("1D port stats failed to get bridge port for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            sai_port, vlan_id);
        return STD_ERR(NPU, CFG, 0);
    }

    NDI_PORT_LOG_TRACE("port stats for 1D npu_port %" PRIx64 " " "vlan_id %u", port, vlan_id);
    return ndi_bridge_port_stats_clear_helper(npu_id, brport_oid, ndi_stat_ids, len);

}

t_std_error ndi_lag_bridge_port_stats_get(npu_id_t npu_id, ndi_lag_id_t ndi_lag_id,
                                           hal_vlan_id_t vlan_id, ndi_stat_id_t *ndi_stat_ids,
                                           uint64_t* stats_val, size_t len){
    sai_object_id_t brport_oid;

    if (!ndi_get_1d_bridge_port(&brport_oid, ndi_lag_id , vlan_id)) {
        NDI_PORT_LOG_ERROR("1D port stats failed to get bridge port for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            ndi_lag_id, vlan_id);
        return STD_ERR(NPU, CFG, 0);
    }

    NDI_PORT_LOG_TRACE("port stats for 1D lag id %" PRIx64 " " "vlan_id %u", ndi_lag_id, vlan_id);
    return ndi_bridge_port_stats_helper(npu_id, brport_oid, ndi_stat_ids, stats_val,len);

}

t_std_error ndi_lag_bridge_port_stats_clear(npu_id_t npu_id, ndi_lag_id_t ndi_lag_id,
                                            hal_vlan_id_t vlan_id, ndi_stat_id_t *ndi_stat_ids,
                                            size_t len)
{
    sai_object_id_t brport_oid;

    /* Find brport oid from ndi_lag_id and vlan_id */
    if (!ndi_get_1d_bridge_port(&brport_oid, ndi_lag_id , vlan_id)) {
        NDI_PORT_LOG_ERROR("1D lag stats failed to get bridge port for ndi_lag_id %" PRIx64 " " "and vlan id %d",
                                                                                            ndi_lag_id, vlan_id);
        return STD_ERR(NPU, CFG, 0);

    }
    NDI_PORT_LOG_TRACE("Stat clear for 1D lag_id  %" PRIx64 " " "vlan id %d", ndi_lag_id, vlan_id);
    return ndi_bridge_port_stats_clear_helper(npu_id, brport_oid, ndi_stat_ids, len);
}

t_std_error ndi_bridge_port_tunnel_stats_get(npu_id_t npu_id, ndi_obj_id_t tun_bridge_port_oid,
                                             ndi_stat_id_t *ndi_stat_ids,
                                             uint64_t* stats_val, size_t len)
{
    return ndi_bridge_port_stats_helper(npu_id, tun_bridge_port_oid, ndi_stat_ids, stats_val, len);
}

t_std_error ndi_bridge_1d_stats_get(npu_id_t npu_id, bridge_id_t br_oid,
                                    ndi_stat_id_t *ndi_stat_ids,
                                    uint64_t* stats_val, size_t len)
{
    const unsigned int list_len = len;
    sai_bridge_stat_t sai_bridge_stats_ids[list_len];
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_VLAN_LOG_ERROR("Invalid NPU Id %d passed",npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_bridge_1d_stats(ndi_stat_ids[ix],&sai_bridge_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->get_bridge_stats(br_oid,
                    len, sai_bridge_stats_ids, stats_val)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Bridge stats Get failed for npu %d, Bridge %d, ret %d \n",
                            npu_id, br_oid, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_bridge_1d_stats_clear(npu_id_t npu_id, bridge_id_t br_oid,
                                      ndi_stat_id_t *ndi_stat_ids, size_t len)
{
    const unsigned int list_len = len;
    sai_bridge_stat_t sai_bridge_stats_ids[list_len];
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_VLAN_LOG_ERROR("Invalid NPU Id %d passed",npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_bridge_1d_stats(ndi_stat_ids[ix],&sai_bridge_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->clear_bridge_stats(br_oid,
                    len, sai_bridge_stats_ids)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Bridge stats clear failed for npu %d, Bridge %llu, ret %d \n",
                            npu_id, br_oid, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_set_bridge_attribute(npu_id_t npu_id, sai_object_id_t br_oid, sai_attribute_t *sai_attr) {

    sai_status_t              sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }
    if((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->set_bridge_attribute(br_oid, sai_attr))
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Sai set bridge attr failed for sai_id %u bridge id %" PRIx64 " ",
        sai_attr->id, br_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;


}
t_std_error ndi_set_bridge_port_attribute(npu_id_t npu_id, sai_object_id_t brport_oid, sai_attribute_t *sai_attr) {

    sai_status_t              sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }
    if((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->set_bridge_port_attribute(brport_oid, sai_attr))
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Sai set bridge port attr failed for sai_id %u bridge port oid %" PRIx64 " ",
        sai_attr->id, brport_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;

}

t_std_error ndi_get_bridge_port_attribute(npu_id_t npu_id, sai_object_id_t brport_oid, sai_attribute_t *sai_attr, int count) {

    sai_status_t              sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }
    if((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->get_bridge_port_attribute(brport_oid, count, sai_attr))
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Sai get bridge port attr failed for sai_id %u bridge port oid %" PRIx64 " ",
        sai_attr->id ,brport_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


bool ndi_get_1d_bridge_port(sai_object_id_t *brport_oid, sai_object_id_t saiport_oid,  hal_vlan_id_t vlan_id ) {

    /* Get sai port id from bridge port */
    ndi_brport_obj_t blk;
    blk.port_obj_id = saiport_oid;
    blk.vlan_id = vlan_id;

    if (!nas_ndi_get_bridge_port_obj(&blk, ndi_brport_query_type_FROM_PORT_VLAN)) {
        NDI_PORT_LOG_ERROR("Failed to get bridge port mapping for port id %" PRIx64 " ",
                         saiport_oid);
        return false;
    }
    *brport_oid = blk.brport_obj_id;
    NDI_PORT_LOG_TRACE("Got bridge port: for 1d bridge port id %" PRIx64 " " "port id %" PRIx64 " ",
               blk.brport_obj_id, saiport_oid);
    return true;

}
/* Common function to delete all types of bridge port : tunnel , subport or ports in 1Q */

static t_std_error ndi_delete_bridge_port(npu_id_t npu_id, ndi_brport_obj_t *info ) {

    ndi_brport_obj_t blk;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t bridge_port_attr[1];
    bridge_port_attr[0].id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
    bridge_port_attr[0].value.booldata = false;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }
    if (ndi_set_bridge_port_attribute (npu_id, info->brport_obj_id, bridge_port_attr) != STD_ERR_OK) {
        return STD_ERR(NPU, CFG, sai_ret);
    }
    if (ndi_flush_bridge_port_entry(info->brport_obj_id) != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("MAC flush failed for bridgeport id :%"  PRIx64 " ",
        info->brport_obj_id);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    if (info->brport_type == ndi_brport_type_PORT) {
        if(ndi_stg_delete_port_stp_ports(npu_id,info->brport_obj_id) != STD_ERR_OK) {
            NDI_PORT_LOG_ERROR("STP Port delete failed for bridge port  %"  PRIx64 " ",
                   info->brport_obj_id);
            return STD_ERR(NPU, CFG, sai_ret);
        }

        if(ndi_vlan_delete_default_member_brports(npu_id,info->brport_obj_id,false) != STD_ERR_OK){
            NDI_PORT_LOG_ERROR("VLAN Port delete failed for bridge port id %"  PRIx64 " ",
                                    info->brport_obj_id);
            return STD_ERR(NPU, CFG, sai_ret);
        }
    }

    if((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->remove_bridge_port(info->brport_obj_id))
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Delete  bridge ports failed for bridge port oid %" PRIx64 " ",
        info->brport_obj_id);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    /* Remove from cache */
    blk.brport_obj_id = info->brport_obj_id;
    blk.brport_type = info->brport_type;
    blk.vlan_id = info->vlan_id; /* Valid for 1D subport  type */

    if (!nas_ndi_remove_bridge_port_obj(&blk)) {
        NDI_PORT_LOG_ERROR("Remove cache  bridgeport failed. brport_oid % " PRIx64 " ",
        info->brport_obj_id);
    }
    NDI_PORT_LOG_TRACE("Delete bridge port success id %" PRIx64 " ", info->brport_obj_id);
    return STD_ERR_OK;
}


/* Bridge port create for  tunnel , subport or port. info should have all except brport_obj_id.*/
static
t_std_error ndi_create_bridge_port(bridge_port_create_info_t *info, sai_object_id_t *br_port_id, sai_attribute_t *brpo_attr, size_t len_attr) {

    sai_bridge_port_type_t sai_po_ty;
    uint32_t attr_idx = 0;
    ndi_brport_type_t brport_type;

    sai_attribute_t bridge_port_attr[SAI_ATTRS_LEN];

    brport_type = info->data.brport_type;

    if (ndi2sai_brport_type(brport_type, &sai_po_ty) != true) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_TYPE;
    bridge_port_attr[attr_idx++].value.s32 = sai_po_ty;
    bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
    bridge_port_attr[attr_idx++].value.oid = info->bridge_oid;

    switch (brport_type) {
        case ndi_brport_type_PORT:
            {
                bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
                bridge_port_attr[attr_idx++].value.oid = info->data.port_obj_id;
                break;
            }
        case ndi_brport_type_SUBPORT_TAG:
        case ndi_brport_type_SUBPORT_UNTAG:
            {
                bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
                bridge_port_attr[attr_idx++].value.oid = info->data.port_obj_id;
                sai_uint16_t sai_vlan_id= info->data.vlan_id;
                bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_VLAN_ID;
                bridge_port_attr[attr_idx++].value.oid = sai_vlan_id;
                break;
            }
        case ndi_brport_type_TUNNEL:
           {
                bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_TUNNEL_ID;
                bridge_port_attr[attr_idx++].value.oid = info->data.port_obj_id;
                break;
           }

        case ndi_brport_type_1D_ROUTER:
        case ndi_brport_type_1Q_ROUTER:
        default:
            {
                NDI_PORT_LOG_ERROR("Unknown or unsupported bridge port create. Type %d  bridgeid  %" PRIx64 " " ,
                    brport_type, info->bridge_oid);
                return STD_ERR(NPU, CFG, 0);
            }
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(info->npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }

    if (attr_idx + len_attr > SAI_ATTRS_LEN) {
        NDI_PORT_LOG_ERROR("bridge port_create :insufficient array size allocated");
        return STD_ERR(NPU, CFG, 0);
    }

    for (size_t sz = 0; sz < len_attr; sz++) {
        bridge_port_attr[attr_idx].id = brpo_attr[sz].id;
        bridge_port_attr[attr_idx++].value.oid = brpo_attr[sz].value.oid;
    }

    if (ndi_sai_bridge_api(ndi_db_ptr)->create_bridge_port(&info->data.brport_obj_id,ndi_switch_id_get(), attr_idx, bridge_port_attr)
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Failed bridge port create br port type %d  port_oid %"  PRIx64 " ",  brport_type, info->data.port_obj_id );
        return STD_ERR(NPU, CFG, 0);
    }

    NDI_PORT_LOG_TRACE("Create  bridgeport: SAI type %d  brport id %" PRIx64 " " "port id %" PRIx64 " ",
                     sai_po_ty, info->data.brport_obj_id, info->data.port_obj_id);

    /* Set ADMIN UP  */
    sai_attribute_t br_port_attr[1];
    sai_status_t   sai_ret = SAI_STATUS_FAILURE;
    br_port_attr[0].id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
    br_port_attr[0].value.booldata = true;

    if (ndi_set_bridge_port_attribute (info->npu_id, info->data.brport_obj_id, br_port_attr) != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("bridgeport create: failed ADMIN state set for port_id %" PRIx64 " ", info->data.port_obj_id);
        if((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->remove_bridge_port(info->data.brport_obj_id))
            != SAI_STATUS_SUCCESS) {
            NDI_PORT_LOG_ERROR("rollback brport_create :Delete  bridge ports failed for bridge port oid %" PRIx64 " ",
            info->data.brport_obj_id);
         }
        return STD_ERR(NPU, CFG, sai_ret);
    }

    if (brport_type != ndi_brport_type_PORT) {

        nas_ndi_add_bridge_port_obj(&info->data);
        *(br_port_id) = info->data.brport_obj_id;
        return STD_ERR_OK;
    }

    /* Set ingress & egress filtering to enable i.e drop unknown vlan packets */
    br_port_attr[0].id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
    br_port_attr[0].value.booldata = true;

    if (ndi_set_bridge_port_attribute (info->npu_id, info->data.brport_obj_id, br_port_attr) != STD_ERR_OK) {
        return STD_ERR(NPU, CFG, sai_ret);
    }

    br_port_attr[0].id = SAI_BRIDGE_PORT_ATTR_EGRESS_FILTERING;
    br_port_attr[0].value.booldata = true;

    if (ndi_set_bridge_port_attribute (info->npu_id, info->data.brport_obj_id, br_port_attr) != STD_ERR_OK) {
        return STD_ERR(NPU, CFG, sai_ret);
    }

    if(!ndi_stg_create_default_stp_port(info->data.brport_obj_id)){
        return STD_ERR(NPU,CFG,0);
    }

    nas_ndi_add_bridge_port_obj(&info->data);
    return STD_ERR_OK;
}

/* Delete bridge port of type : SUBPORT which has lag or port */
static t_std_error ndi_delete_all_type_subports(npu_id_t npu_id, sai_object_id_t brport_oid, hal_vlan_id_t  vlan_id)
{
   ndi_brport_obj_t blk1;

   memset(&blk1, 0, sizeof(ndi_brport_obj_t));
   blk1.brport_obj_id = brport_oid;
   /* SUBPORT_TAG or SUBPORT_UNTAG handled the same at delete */
   blk1.brport_type = ndi_brport_type_SUBPORT_TAG;
   blk1.vlan_id = vlan_id;

    if (STD_ERR_OK != ndi_delete_bridge_port(npu_id, &blk1)) {
        NDI_PORT_LOG_ERROR("Remove ID  bridgeport failed. brport_oid % " PRIx64 " ",
        brport_oid);
        return STD_ERR(NPU, CFG, 0);
    }
    NDI_PORT_LOG_TRACE("Delete 1D bridge port success   %" PRIx64 " ", brport_oid);
    return STD_ERR_OK;
}


t_std_error
ndi_1d_bridge_member_lag_add(npu_id_t npu_id, sai_object_id_t br_oid, ndi_obj_id_t ndi_lag_id, hal_vlan_id_t vlan_id, bool tag)
{

    bridge_port_create_info_t info;
    sai_object_id_t bridgeport_id;
    sai_attribute_t bridge_port_attr[2];
    t_std_error rc;

    memset(&info, 0, sizeof(bridge_port_create_info_t));
    info.data.port_obj_id = ndi_lag_id;
    info.data.port_type = ndi_port_type_LAG;
    if (tag) {
        info.data.brport_type = ndi_brport_type_SUBPORT_TAG;
    } else {
        info.data.brport_type = ndi_brport_type_SUBPORT_UNTAG;
    }
    info.data.vlan_id = vlan_id;
    info.npu_id = npu_id;
    info.bridge_oid = br_oid;
    info.tag = tag;

    size_t cnt =0;
    bridge_port_attr[cnt].value.u32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DISABLE;
    bridge_port_attr[cnt++].id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;

    bridge_port_attr[cnt].id = SAI_BRIDGE_PORT_ATTR_TAGGING_MODE;

    if (tag) {
        bridge_port_attr[cnt++].value.u32 = (sai_bridge_port_tagging_mode_t)SAI_BRIDGE_PORT_TAGGING_MODE_TAGGED;
    } else {
        bridge_port_attr[cnt++].value.u32 = (sai_bridge_port_tagging_mode_t)SAI_BRIDGE_PORT_TAGGING_MODE_UNTAGGED;
    }


    if ((rc =  ndi_create_bridge_port(&info, &bridgeport_id, bridge_port_attr, cnt)) != STD_ERR_OK) {
       NDI_PORT_LOG_ERROR("ADD 1D LAG bridgeport failed. br_oid % " PRIx64 " ",
        br_oid);
       return rc;

    }
    NDI_PORT_LOG_TRACE("Add Lag to 1D br %" PRIx64 " " "lag_id  %" PRIx64 " " "is_tag %d, vlan_id %d",
                                                                    br_oid, ndi_lag_id, tag, vlan_id);
    return STD_ERR_OK;

}

t_std_error ndi_1d_bridge_member_lag_delete(npu_id_t npu_id, ndi_obj_id_t ndi_lag_id, hal_vlan_id_t vlan_id) {

    sai_object_id_t brport_oid;

    /* Find brport oid from ndi_lag_id and vlan_id */
    if (!ndi_get_1d_bridge_port(&brport_oid, ndi_lag_id , vlan_id)) {
        NDI_PORT_LOG_ERROR("1D lag delete failed to get bridge port for ndi_lag_id %" PRIx64 " " "and vlan id %d",
                                                                                            ndi_lag_id, vlan_id);
        return STD_ERR(NPU, CFG, 0);

    }
    NDI_PORT_LOG_TRACE("Del from 1D lag_id  %" PRIx64 " " "vlan id %d", ndi_lag_id, vlan_id);
    return ndi_delete_all_type_subports(npu_id, brport_oid, vlan_id);
}

t_std_error
ndi_1d_bridge_tunnel_port_add(npu_id_t npu_id, sai_object_id_t br_oid, sai_object_id_t tunnel_oid, sai_object_id_t *bridge_port_id)
{

    bridge_port_create_info_t info;
    memset(&info, 0, sizeof(bridge_port_create_info_t));
    info.npu_id = npu_id;
    info.bridge_oid = br_oid;
    info.tag = false;
    sai_attribute_t bridge_port_attr;

    info.data.port_obj_id = tunnel_oid;
    info.data.port_type = ndi_port_type_PORT;
    info.data.brport_type = ndi_brport_type_TUNNEL;
    bridge_port_attr.value.u32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DISABLE;
    bridge_port_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
    return ndi_create_bridge_port( &info, bridge_port_id, &bridge_port_attr, 1);
}

t_std_error
ndi_1d_bridge_tunnel_delete(npu_id_t npu_id, sai_object_id_t tun_brport_oid) {

    ndi_brport_obj_t blk1;
    memset(&blk1, 0, sizeof(ndi_brport_obj_t));
    blk1.brport_obj_id = tun_brport_oid;
    blk1.brport_type = ndi_brport_type_TUNNEL;

    if (STD_ERR_OK != ndi_delete_bridge_port(npu_id, &blk1)) {
        NDI_PORT_LOG_ERROR("Remove bridgeport failed. tunnel brport_oid % " PRIx64 " ",
        tun_brport_oid);
        return STD_ERR(NPU, CFG, 0);
    }
    NDI_PORT_LOG_TRACE("Delete tunnel bridge port success id %" PRIx64 " ", tun_brport_oid);
    return STD_ERR_OK;
}



t_std_error
ndi_1d_bridge_member_port_add(npu_id_t npu_id, sai_object_id_t br_oid , npu_port_t port, hal_vlan_id_t vlan_id, bool tag) {

    sai_object_id_t sai_port;
    bridge_port_create_info_t info;
    t_std_error ret_code;
    sai_object_id_t br_po_id;
    sai_attribute_t bridge_port_attr[2];
    t_std_error rc;

    memset(&info, 0, sizeof(bridge_port_create_info_t));
    info.npu_id = npu_id;
    info.bridge_oid = br_oid;
    info.tag = tag;


    if ((ret_code = ndi_sai_port_id_get(npu_id, port, &sai_port) != STD_ERR_OK)) {
         NDI_PORT_LOG_ERROR("ADD 1D port: failed to get sai_port id for NPU port %u", port);
        return ret_code;
    }
    info.data.port_obj_id = sai_port;
    info.data.port_type = ndi_port_type_PORT;
    if (tag) {
        info.data.brport_type = ndi_brport_type_SUBPORT_TAG;
    } else {
        info.data.brport_type = ndi_brport_type_SUBPORT_UNTAG;
    }


    info.data.vlan_id = vlan_id;

    size_t cnt =0;
    bridge_port_attr[cnt].value.u32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DISABLE;
    bridge_port_attr[cnt++].id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;

    bridge_port_attr[cnt].id = SAI_BRIDGE_PORT_ATTR_TAGGING_MODE;
    if (tag) {
        bridge_port_attr[cnt++].value.u32 = (sai_bridge_port_tagging_mode_t)SAI_BRIDGE_PORT_TAGGING_MODE_TAGGED;
    } else {
        bridge_port_attr[cnt++].value.u32 = (sai_bridge_port_tagging_mode_t)SAI_BRIDGE_PORT_TAGGING_MODE_UNTAGGED;
    }

    if((rc  = ndi_create_bridge_port(&info, &br_po_id, bridge_port_attr, cnt)) != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("ADD 1D port bridgeport failed. br_oid % " PRIx64 " ",
        br_oid);
        return rc;
    }
    NDI_PORT_LOG_TRACE("ADD port to 1D br %" PRIx64 " " "npu port %u, is_tag %d, vlan_id %d", br_oid, port, tag, vlan_id);

    return STD_ERR_OK;
}



t_std_error
ndi_1d_bridge_member_port_delete(npu_id_t npu_id, npu_port_t port, hal_vlan_id_t vlan_id) {

    sai_object_id_t sai_port;
    ndi_brport_obj_t info;
    sai_object_id_t brport_oid;
    t_std_error ret_code;
    memset(&info, 0, sizeof(ndi_brport_obj_t));

    if ((ret_code = ndi_sai_port_id_get(npu_id, port, &sai_port) != STD_ERR_OK)) {
        NDI_PORT_LOG_ERROR("Del 1D port: failed to get sai_port id for NPU port %u", port);
        return ret_code;
    }
    if (!ndi_get_1d_bridge_port(&brport_oid, sai_port , vlan_id)) {
        NDI_PORT_LOG_ERROR("1D portdelete failed to get bridge port for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            sai_port, vlan_id);
        return STD_ERR(NPU, CFG, 0);

    }
    NDI_PORT_LOG_TRACE("Del port from 1D npu_port %" PRIx64 " " "vlan_id %u", port, vlan_id);
    return ndi_delete_all_type_subports(npu_id, brport_oid, vlan_id);
}


bool ndi_get_1q_bridge_port(sai_object_id_t *brport_oid, sai_object_id_t saiport_oid) {

    /* Get bridge port id for sai port id */
    ndi_brport_obj_t blk;
    blk.port_obj_id = saiport_oid;

    if (!nas_ndi_get_bridge_port_obj(&blk, ndi_brport_query_type_FROM_PORT)) {
        NDI_PORT_LOG_TRACE ("Failed to get bridge port mapping for port %" PRIx64 " " , saiport_oid);
        return false;
    }
    *brport_oid = blk.brport_obj_id;
    NDI_PORT_LOG_TRACE("Got bridge port: bridge port id %" PRIx64 " " "port id %" PRIx64 " ", blk.brport_obj_id, saiport_oid);
    return true;
}

bool ndi_get_1q_sai_port(sai_object_id_t brport_oid, sai_object_id_t *saiport_oid) {
    /* Get sai port id from bridge port */
    ndi_brport_obj_t blk;
    blk.brport_obj_id = brport_oid;

    if (!nas_ndi_get_bridge_port_obj(&blk, ndi_brport_query_type_FROM_BRPORT)) {
        NDI_PORT_LOG_ERROR("Failed to get sai port maping for bridge port id %" PRIx64 " ",
                         brport_oid);
        return false;
     }
     *saiport_oid = blk.port_obj_id;
     NDI_PORT_LOG_TRACE("Got SAI port: bridge port id %" PRIx64 " " "port id %" PRIx64 " ", brport_oid, *saiport_oid);
     return true;
}


t_std_error nas_ndi_delete_bridge_port_1Q(npu_id_t npu_id, sai_object_id_t sai_port) {

     sai_object_id_t brport_oid;
     ndi_brport_obj_t del_info;
     memset(&del_info, 0, sizeof(ndi_brport_obj_t));
     if (ndi_get_1q_bridge_port(&brport_oid, sai_port)) {
         del_info.brport_type = ndi_brport_type_PORT;
         del_info.brport_obj_id = brport_oid;
         if (STD_ERR_OK != ndi_delete_bridge_port(npu_id, &del_info)) {
            return STD_ERR(NPU, CFG, 0);
         }
         return STD_ERR_OK;
     } else {
         NDI_PORT_LOG_ERROR("Failed to delete 1Q bridgeport  port id %"  PRIx64 " ", sai_port);
         return STD_ERR(NPU, CFG, 0);
     }
}

t_std_error nas_ndi_create_bridge_port_1Q(npu_id_t npu_id, sai_object_id_t sai_port, bool lag) {


    bridge_port_create_info_t info;
    memset(&info, 0, sizeof(bridge_port_create_info_t));
    info.npu_id = npu_id;
    info.bridge_oid = default_1q_br_oid;
    info.tag = false;

    info.data.port_obj_id = sai_port;
    sai_object_id_t br_po_id;

    info.data.brport_type = ndi_brport_type_PORT;
    if (lag) {
        info.data.port_type = ndi_port_type_LAG;
    } else {
        info.data.port_type = ndi_port_type_PORT;
    }
    return ndi_create_bridge_port(&info, &br_po_id, NULL,0);
}


t_std_error ndi_brport_attr_set_or_get_1Q(npu_id_t npu_id, sai_object_id_t port_id, bool set, sai_attribute_t *sai_attr) {

    sai_object_id_t  brport_id;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL || sai_attr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    if (!ndi_get_1q_bridge_port(&brport_id, port_id)) {
        if (set) {
            NDI_PORT_LOG_ERROR("Failed to get bridge port for sai port %" PRIx64 " " , port_id);
        }
        return STD_ERR(NPU, CFG, 0);
    }
    if (set) {
        return ndi_set_bridge_port_attribute(npu_id, brport_id, sai_attr);
    } else {
        return ndi_get_bridge_port_attribute(npu_id, brport_id, sai_attr,1);
    }
}

t_std_error ndi_tunport_mac_learn_mode_set(npu_id_t npu_id, ndi_obj_id_t tun_brport,
                                        BASE_IF_PHY_MAC_LEARN_MODE_t mode){
    sai_attribute_t sai_attr;
    sai_attr.value.u32 = (sai_bridge_port_fdb_learning_mode_t )ndi_port_get_sai_mac_learn_mode(mode);
    sai_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;

    NDI_PORT_LOG_TRACE("Set  MAC learn mode for tun brport id %" PRIx64 " ", tun_brport);
    return ndi_set_bridge_port_attribute(npu_id, tun_brport, &sai_attr);

}

t_std_error ndi_tunport_mac_learn_mode_get(npu_id_t npu_id, ndi_obj_id_t tun_brport,
                                        BASE_IF_PHY_MAC_LEARN_MODE_t *mode) {
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;

    t_std_error rc;
    if ((rc = ndi_get_bridge_port_attribute(npu_id, tun_brport, &sai_attr,1)) != STD_ERR_OK) {
         NDI_PORT_LOG_ERROR("Get MAC learn mode  failed for tunnel brport BRIDGE % " PRIx64 " " , tun_brport);
         return rc;
    }

    *mode = ndi_port_get_mac_learn_mode((sai_bridge_port_fdb_learning_mode_t)sai_attr.value.u32);
    NDI_PORT_LOG_TRACE("MAC learn mode %d for tun brport id %" PRIx64 " ", *(mode), tun_brport);
    return STD_ERR_OK;
}

t_std_error
ndi_set_subport_flooding(npu_id_t npu_id, ndi_obj_id_t bridge_id,  ndi_bridge_packets_type_t pkt_type,
                                            bool enable_flood) {

    sai_attribute_t bridge_attr[1];
    t_std_error ret;

    switch(pkt_type) {
        case NDI_BRIDGE_PKT_UNICAST:
            {
                bridge_attr[0].id = SAI_BRIDGE_ATTR_UNKNOWN_UNICAST_FLOOD_CONTROL_TYPE;
                if (enable_flood) {
                    bridge_attr[0].value.oid = SAI_BRIDGE_FLOOD_CONTROL_TYPE_SUB_PORTS;
                } else {
                    bridge_attr[0].value.oid = SAI_BRIDGE_FLOOD_CONTROL_TYPE_NONE;
                }
                break;
            }
        case NDI_BRIDGE_PKT_MULTICAST:
            {
                bridge_attr[0].id = SAI_BRIDGE_ATTR_UNKNOWN_MULTICAST_FLOOD_CONTROL_TYPE;
                if (enable_flood) {
                    bridge_attr[0].value.oid = SAI_BRIDGE_FLOOD_CONTROL_TYPE_SUB_PORTS;
                } else {
                    bridge_attr[0].value.oid = SAI_BRIDGE_FLOOD_CONTROL_TYPE_NONE;
                }
                break;
            }
        case NDI_BRIDGE_PKT_BROADCAST:
            {
                bridge_attr[0].id = SAI_BRIDGE_ATTR_BROADCAST_FLOOD_CONTROL_TYPE;
                if (enable_flood) {
                    bridge_attr[0].value.oid = SAI_BRIDGE_FLOOD_CONTROL_TYPE_SUB_PORTS;
                } else {
                    bridge_attr[0].value.oid = SAI_BRIDGE_FLOOD_CONTROL_TYPE_NONE;
                }
                break;
            }
        default:
            {
                NDI_PORT_LOG_ERROR("Subport flood :Unsupported pkt type  %d" , pkt_type);
                return STD_ERR(NPU, CFG, 0);
            }
        }
    if ((ret = ndi_set_bridge_attribute (npu_id, bridge_id, bridge_attr)) != STD_ERR_OK) {
        return STD_ERR(NPU, PARAM, 0);
    }
    return STD_ERR_OK;

}

t_std_error
ndi_set_mcast_flooding(npu_id_t npu_id, ndi_obj_id_t bridge_id,  ndi_obj_id_t multicast_grp, ndi_bridge_packets_type_t pkt_type,
                                            bool enable_flood) {

    sai_attribute_t bridge_attr[1];
    sai_attribute_t bridge_fl_attr[1];

    switch(pkt_type) {
    case NDI_BRIDGE_PKT_UNICAST:
        {
            bridge_attr[0].id = SAI_BRIDGE_ATTR_UNKNOWN_UNICAST_FLOOD_CONTROL_TYPE;
            bridge_attr[0].value.oid =  SAI_BRIDGE_FLOOD_CONTROL_TYPE_L2MC_GROUP;

            bridge_fl_attr[0].id = SAI_BRIDGE_ATTR_UNKNOWN_UNICAST_FLOOD_GROUP;

            if (enable_flood ) {
                bridge_fl_attr[0].value.oid =  multicast_grp;
            } else {
                bridge_fl_attr[0].value.oid =  SAI_NULL_OBJECT_ID;

            }
            break;
        }
        case NDI_BRIDGE_PKT_MULTICAST:
        {
            bridge_attr[0].id = SAI_BRIDGE_ATTR_UNKNOWN_MULTICAST_FLOOD_CONTROL_TYPE;
            bridge_attr[0].value.oid =  SAI_BRIDGE_FLOOD_CONTROL_TYPE_L2MC_GROUP;

            bridge_fl_attr[0].id = SAI_BRIDGE_ATTR_UNKNOWN_MULTICAST_FLOOD_GROUP;

            if (enable_flood ) {
                bridge_fl_attr[0].value.oid =  multicast_grp;
            } else {
                bridge_fl_attr[0].value.oid =  SAI_NULL_OBJECT_ID;
            }
            break;
        }
        case NDI_BRIDGE_PKT_BROADCAST:
        {
            bridge_attr[0].id = SAI_BRIDGE_ATTR_BROADCAST_FLOOD_CONTROL_TYPE;
            bridge_attr[0].value.oid =  SAI_BRIDGE_FLOOD_CONTROL_TYPE_L2MC_GROUP;
            bridge_fl_attr[0].id = SAI_BRIDGE_ATTR_BROADCAST_FLOOD_GROUP;

            if (enable_flood ) {
                bridge_fl_attr[0].value.oid =  multicast_grp;
            } else {
                bridge_fl_attr[0].value.oid =  SAI_NULL_OBJECT_ID;

            }
            break;
        }
        default:
        {
            NDI_PORT_LOG_ERROR("Unsupported type  % d" , pkt_type);
            return STD_ERR(NPU, PARAM, 0);

        }
    }

    if ((ndi_set_bridge_attribute (npu_id, bridge_id, bridge_fl_attr)) != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("Failed to set multicast flood group %llu , enable %d", multicast_grp, enable_flood);
        return STD_ERR(NPU, CFG, 0);
    }
    if ((ndi_set_bridge_attribute (npu_id, bridge_id, bridge_attr)) != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("Failed to set multicast flood control pkt type %d",  pkt_type);
        return STD_ERR(NPU, CFG, 0);
    }
    NDI_PORT_LOG_TRACE("Attribute set for flood control for pkt type %d , bridge_id %" PRIx64 " ",
                       pkt_type, bridge_id);
    return STD_ERR_OK;
}

t_std_error ndi_flood_control_1d_bridge(npu_id_t npu_id, ndi_obj_id_t br_oid,  ndi_obj_id_t multicast_grp,
           ndi_bridge_packets_type_t pkt_type, bool enable_flood)
{
    t_std_error rc;

    NDI_PORT_LOG_TRACE("Flood ctrl for br %" PRIx64 " " "mcast %" PRIx64 " " "pkt_type %d  enable_flood %d",
           br_oid, multicast_grp, pkt_type, enable_flood);
    if (multicast_grp != 0) {
        if (pkt_type == NDI_BRIDGE_PKT_ALL)  {
            if ((rc = ndi_set_mcast_flooding(npu_id, br_oid, multicast_grp, NDI_BRIDGE_PKT_UNICAST, enable_flood)) != STD_ERR_OK) {
                NDI_PORT_LOG_ERROR("In all Unicast flood control failed for mcast grp %llu enable %d", multicast_grp, enable_flood);
                return rc;
            }
            if ((rc =ndi_set_mcast_flooding(npu_id, br_oid, multicast_grp, NDI_BRIDGE_PKT_MULTICAST, enable_flood)) != STD_ERR_OK) {
                NDI_PORT_LOG_ERROR("In all Multicast flood control failed for mcast grp %llu enable %d", multicast_grp, enable_flood);
                return rc;

            }
            if ((rc = ndi_set_mcast_flooding(npu_id, br_oid, multicast_grp, NDI_BRIDGE_PKT_BROADCAST, enable_flood)) != STD_ERR_OK) {
                NDI_PORT_LOG_ERROR("In all Broadcast flood control failed for mcast grp %llu enable %d", multicast_grp, enable_flood);
                return rc;
            }
        } else {
            if ((rc = ndi_set_mcast_flooding(npu_id, br_oid, multicast_grp, pkt_type, enable_flood)) != STD_ERR_OK) {
                NDI_PORT_LOG_ERROR("Flood control failed for mcast grp %llu enable %d pkt_type %d" ,
                         multicast_grp, enable_flood, pkt_type);
                return rc;
            }
       }
    } else {
        if (pkt_type == NDI_BRIDGE_PKT_ALL)  {

            if ((rc = ndi_set_subport_flooding(npu_id, br_oid, NDI_BRIDGE_PKT_MULTICAST, enable_flood)) != STD_ERR_OK) {
                NDI_PORT_LOG_ERROR("In all multicast flood control failed for br_oid %llu enable %d", br_oid, enable_flood);
                return rc;
            }
            if ((rc = ndi_set_subport_flooding(npu_id, br_oid, NDI_BRIDGE_PKT_BROADCAST, enable_flood)) != STD_ERR_OK) {
                NDI_PORT_LOG_ERROR("In all broadcast flood control failed for br_oid %llu enable %d", br_oid, enable_flood);
                return rc;
            }
            if ((rc = ndi_set_subport_flooding(npu_id, br_oid, NDI_BRIDGE_PKT_UNICAST, enable_flood)) != STD_ERR_OK) {
                NDI_PORT_LOG_ERROR("In all unicast flood control failed for br_oid %llu enable %d", br_oid, enable_flood);
                return rc;
            }
        } else {
            if ((rc = ndi_set_subport_flooding(npu_id, br_oid, pkt_type,enable_flood)) != STD_ERR_OK) {
                NDI_PORT_LOG_ERROR("Set subport flooding failed for br_oid %llu  pkt_type %d" ,br_oid, pkt_type);
                return rc;
            }
        }
    }
    return STD_ERR_OK;


     /* If multicast group is not sent ir means control subports */
}

using sub_port_attr_fn = std::function<t_std_error (npu_id_t npu_id, sai_object_id_t brport,
                                                    nas_com_id_value_t & attr)>;

static t_std_error _sub_port_mac_learn_handler(npu_id_t npu_id, sai_object_id_t brport,nas_com_id_value_t & attr){

    static const auto ndi_to_sai_fdb_learn_mode = new std::unordered_map<BASE_IF_MAC_LEARN_MODE_t,
                                                        sai_bridge_port_fdb_learning_mode_t,std::hash<int>>
    {
        {BASE_IF_MAC_LEARN_MODE_DROP, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DROP},
        {BASE_IF_MAC_LEARN_MODE_DISABLE, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DISABLE},
        {BASE_IF_MAC_LEARN_MODE_HW, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW},
        {BASE_IF_MAC_LEARN_MODE_CPU_TRAP, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_TRAP},
        {BASE_IF_MAC_LEARN_MODE_CPU_LOG, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_LOG},
    };

    BASE_IF_MAC_LEARN_MODE_t mac_lrn_mode = *(BASE_IF_MAC_LEARN_MODE_t *)attr.val;
    auto it = ndi_to_sai_fdb_learn_mode->find(mac_lrn_mode);
    if(it== ndi_to_sai_fdb_learn_mode->end()){
        EV_LOGGING(NDI,ERR,"SUB-PORT-MAC-LRN","Failed to convert MAC learn mode %d to sai "
                "mac learn mode",mac_lrn_mode);
        return STD_ERR(NPU,PARAM,0);
    }
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
    sai_attr.value.u32 = it->second;
    EV_LOGGING(NDI,INFO,"SUB-PORT-MAC-LRN","Setting mac learn mode to %d for bridge port %llx "
                "mac learn mode",it->second,brport);

    return ndi_set_bridge_port_attribute(npu_id, brport, &sai_attr);

}

static t_std_error _sub_port_ing_sh_handler(npu_id_t npu_id, sai_object_id_t brport,nas_com_id_value_t & attr){


    sai_attribute_t sai_attr;
    sai_attr.id =  (attr.attr_id == DELL_IF_IF_INTERFACES_INTERFACE_INGRESS_SPLIT_HORIZON_ID) ?
            SAI_BRIDGE_PORT_ATTR_INGRESS_SPLIT_HORIZON_ID : SAI_BRIDGE_PORT_ATTR_EGRESS_SPLIT_HORIZON_ID;
    sai_attr.value.u32 = *(uint32_t *)attr.val;
    EV_LOGGING(NDI,INFO,"SUB-PORT-SH-SET","Setting ingress/egress split horizon to %d for bridge port %llx "
                ,sai_attr.value.u32,brport);
    return ndi_set_bridge_port_attribute(npu_id, brport, &sai_attr);

}

static auto bridge_sub_port_attr_fns = new std::unordered_map<uint64_t, sub_port_attr_fn>{
    {DELL_IF_IF_INTERFACES_INTERFACE_MAC_LEARN, _sub_port_mac_learn_handler},
    {DELL_IF_IF_INTERFACES_INTERFACE_INGRESS_SPLIT_HORIZON_ID,_sub_port_ing_sh_handler},
    {DELL_IF_IF_INTERFACES_INTERFACE_EGRESS_SPLIT_HORIZON_ID,_sub_port_ing_sh_handler}
};

static bool _get_sai_port_id(npu_id_t npu_id ,ndi_obj_id_t port_id,
                             ndi_port_type_t port_type, ndi_brport_obj_t & brport_obj){
    if (port_type == ndi_port_type_LAG) {
        brport_obj.port_obj_id = port_id;
        return true;
    }

    if (ndi_sai_port_id_get(npu_id,(port_t)port_id, &brport_obj.port_obj_id) == STD_ERR_OK) {
        return true;
    }

    NDI_PORT_LOG_ERROR("Not able to find SAI port id for npu:%d port:%d",
                             npu_id, (port_t)port_id);
    return false;
}


static bool ndi_bridge_get_bridge_port (npu_id_t npu_id ,ndi_obj_id_t port_id,
                                        ndi_port_type_t port_type, hal_vlan_id_t vlan_id,
                                        sai_object_id_t & br_port){
    ndi_brport_obj_t brport_obj;
    brport_obj.vlan_id = vlan_id;
    if(!_get_sai_port_id(npu_id,port_id,port_type,brport_obj)){
        return false;
    }

    if(!nas_ndi_get_bridge_port_obj(&brport_obj,ndi_brport_query_type_FROM_PORT_VLAN)){
        NDI_PORT_LOG_ERROR("Failed to find bridge port for port %llx",brport_obj.port_obj_id);
        return false;
    }

    br_port = brport_obj.brport_obj_id;
    return true;
}

t_std_error ndi_bridge_sub_port_attr_set(npu_id_t npu_id, ndi_obj_id_t port_id,
                                         ndi_port_type_t port_type,hal_vlan_id_t vlan_id,
                                         nas_com_id_value_t attribute_key_val[], size_t key_val_size){

    if(key_val_size == 0){
        EV_LOGGING(NDI,NOTICE,"SUB-PORT-ATTR","NO key value pair passed to set subport attributes");
        return STD_ERR_OK;
    }

    sai_object_id_t brport_id;
    if(!ndi_bridge_get_bridge_port(npu_id,port_id,port_type,vlan_id,brport_id)){
        return STD_ERR(NPU,PARAM,0);
    }

    t_std_error rc = STD_ERR_OK;

    for(size_t ix = 0 ; ix < key_val_size ;  ++ix){
        auto it = bridge_sub_port_attr_fns->find(attribute_key_val[ix].attr_id);
        if(it != bridge_sub_port_attr_fns->end()){
            rc = it->second(npu_id,brport_id,attribute_key_val[ix]);
            if(rc != STD_ERR_OK) break;
        }
    }

    return rc;
}

}

