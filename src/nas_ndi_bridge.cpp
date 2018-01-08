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

#include "sai.h"
#include "saitypes.h"
#include "saibridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <vector>
#include <map>


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

#define MAX_PORT_BUF 256

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
            NDI_PORT_LOG_ERROR("Get  port_id failed for 1Q bridgeport : % " PRIx64 " ", bridge_port_id);
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

        /* Set bridgeport ingress_filtering to enable i.e drop unknown vlan packets */
        bridge_port_attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
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
        {ndi_brport_type_SUBPORT, SAI_BRIDGE_PORT_TYPE_SUB_PORT},
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


t_std_error ndi_delete_bridge_port(npu_id_t npu_id, sai_object_id_t brport_oid) {

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t bridge_port_attr[1];
    bridge_port_attr[0].id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
    bridge_port_attr[0].value.booldata = false;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }
    if (ndi_set_bridge_port_attribute (npu_id, brport_oid, bridge_port_attr) != STD_ERR_OK) {
        return STD_ERR(NPU, CFG, sai_ret);
    }
    if (ndi_flush_bridge_port_entry(brport_oid) != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("MAC flush failed for bridgeport id :%"  PRIx64 " ",
        brport_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }

    if(ndi_stg_delete_port_stp_ports(npu_id,brport_oid) != STD_ERR_OK){
         NDI_PORT_LOG_ERROR("STP Port delete failed for bridge port  %"  PRIx64 " ",
                                brport_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }

    if(ndi_vlan_delete_default_member_brports(npu_id,brport_oid,false) != STD_ERR_OK){
         NDI_PORT_LOG_ERROR("VLAN Port delete failed for bridge port id %"  PRIx64 " ",
                                    brport_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }

    if((sai_ret = ndi_sai_bridge_api(ndi_db_ptr)->remove_bridge_port(brport_oid))
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Delete  bridge ports failed for bridge port oid %" PRIx64 " ",
        brport_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    /* Remove from cache */
    ndi_brport_obj_t blk1;
    blk1.brport_obj_id = brport_oid;
    blk1.brport_type = ndi_brport_type_PORT;

    if (!nas_ndi_remove_bridge_port_obj(&blk1)) {
        NDI_PORT_LOG_ERROR("Remove cache  bridgeport failed. brport_oid % " PRIx64 " ",
        brport_oid);
    }
    NDI_PORT_LOG_TRACE("Delete bridge port success id %" PRIx64 " ", brport_oid);
    return STD_ERR_OK;
}

/* For tunnel , subport or port argument info should have all except brport_obj_id.*/

t_std_error ndi_create_bridge_port(npu_id_t npu_id, ndi_brport_obj_t *info, sai_object_id_t bridge_oid) {

    sai_bridge_port_type_t sai_po_ty;
    uint32_t attr_idx = 0;
    ndi_brport_type_t brport_type;
    sai_attribute_t bridge_port_attr [4];

    brport_type = info->brport_type;

    if (ndi2sai_brport_type(brport_type, &sai_po_ty) != true) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_TYPE;
    bridge_port_attr[attr_idx++].value.s32 = sai_po_ty;
    bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
    bridge_port_attr[attr_idx++].value.oid = bridge_oid;

    switch (brport_type) {
        case ndi_brport_type_PORT:
            {
                bridge_port_attr[attr_idx].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
                bridge_port_attr[attr_idx++].value.oid = info->port_obj_id;
                break;
            }
        case ndi_brport_type_SUBPORT:
        case ndi_brport_type_TUNNEL:
        case ndi_brport_type_1D_ROUTER:
        case ndi_brport_type_1Q_ROUTER:
        default:
            {
                NDI_PORT_LOG_ERROR("Unknown or unsupported bridge port create. Type %d  bridgeid  %" PRIx64 " " ,
                    brport_type, bridge_oid);
                return STD_ERR(NPU, CFG, 0);
            }
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, CFG,0);
    }

    if (ndi_sai_bridge_api(ndi_db_ptr)->create_bridge_port(&info->brport_obj_id,ndi_switch_id_get(), attr_idx, bridge_port_attr)
            != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_ERROR("Failed bridge port create br port type %d  port_oid %"  PRIx64 " ",  brport_type, info->port_obj_id );
        return STD_ERR(NPU, CFG, 0);
    }

    NDI_PORT_LOG_TRACE("Create  bridgeport: SAI type %d  brport id %" PRIx64 " " "port id %" PRIx64 " ",
                     sai_po_ty, info->brport_obj_id, info->port_obj_id);

    /* Set ADMIN UP  */
    sai_attribute_t br_port_attr[1];
    sai_status_t   sai_ret = SAI_STATUS_FAILURE;
    br_port_attr[0].id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
    br_port_attr[0].value.booldata = true;

    if (ndi_set_bridge_port_attribute (npu_id, info->brport_obj_id, br_port_attr) != STD_ERR_OK) {
        return STD_ERR(NPU, CFG, sai_ret);
    }
    /* Set ingress filtering. Drop unknown vlan packets */
    br_port_attr[0].id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
    br_port_attr[0].value.booldata = true;

    if (ndi_set_bridge_port_attribute (npu_id, info->brport_obj_id, br_port_attr) != STD_ERR_OK) {
        return STD_ERR(NPU, CFG, sai_ret);
    }

     if(!ndi_stg_create_default_stp_port(info->brport_obj_id)){
         return STD_ERR(NPU,CFG,0);
     }
    /* Add bridge port to cache stucture */
    nas_ndi_add_bridge_port_obj(info);
    return STD_ERR_OK;

}

bool ndi_get_1q_bridge_port(sai_object_id_t *brport_oid, sai_object_id_t saiport_oid) {

    /* Get bridge port id for sai port id */
    ndi_brport_obj_t blk;
    blk.port_obj_id = saiport_oid;

    if (!nas_ndi_get_bridge_port_obj(&blk, ndi_brport_query_type_FROM_PORT)) {
        NDI_PORT_LOG_ERROR("Failed to get bridge port mapping for port %" PRIx64 " " , saiport_oid);
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
     NDI_PORT_LOG_TRACE("Got SAI port: bridge port id %" PRIx64 " " "port id %" PRIx64 " ", brport_oid, saiport_oid);
     return true;
}


t_std_error nas_ndi_delete_bridge_port_1Q(npu_id_t npu_id, sai_object_id_t sai_port) {

     sai_object_id_t brport_oid;

     if (ndi_get_1q_bridge_port(&brport_oid, sai_port)) {
         ndi_delete_bridge_port(npu_id, brport_oid);
         return STD_ERR_OK;
     } else {
         NDI_PORT_LOG_ERROR("Failed to delete 1Q bridgeport  port id %"  PRIx64 " ", sai_port);
         return STD_ERR(NPU, CFG, 0);
     }
}


t_std_error nas_ndi_create_bridge_port_1Q(npu_id_t npu_id, sai_object_id_t sai_port, bool lag) {

     ndi_brport_obj_t blk;
     blk.port_obj_id = sai_port;
     blk.brport_type = ndi_brport_type_PORT;
     if (lag) {
         blk.port_type = ndi_port_type_LAG;
     } else {
         blk.port_type = ndi_port_type_PORT;
     }
     return ndi_create_bridge_port(npu_id, &blk, default_1q_br_oid);
}


t_std_error ndi_brport_attr_set_or_get_1Q(npu_id_t npu_id, sai_object_id_t port_id, bool set, sai_attribute_t *sai_attr) {

    sai_object_id_t  brport_id;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL || sai_attr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    if (!ndi_get_1q_bridge_port(&brport_id, port_id)) {
        NDI_PORT_LOG_ERROR("Failed to get bridge port for sai port %" PRIx64 " " , port_id);
        return STD_ERR(NPU, CFG, 0);
    }
    if (set) {
        return ndi_set_bridge_port_attribute(npu_id, brport_id, sai_attr);
    } else {
        return ndi_get_bridge_port_attribute(npu_id, brport_id, sai_attr,1);
    }
}

}


