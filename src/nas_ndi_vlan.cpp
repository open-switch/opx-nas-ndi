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
 * filename: nas_ndi_vlan.cpp
 */

#include "std_error_codes.h"
#include "std_assert.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_vlan.h"
#include "nas_ndi_utils.h"
#include "sai.h"
#include "saivlan.h"
#include "nas_vlan_consts.h"
#include "nas_ndi_map.h"
#include "nas_ndi_obj_cache.h"
#include "nas_ndi_bridge_port.h"
#include "nas_ndi_mac_utl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>


static hal_vlan_id_t default_vlan_id = 1;

static std::unordered_map <hal_vlan_id_t, sai_object_id_t> g_ndi_vlan_map;
static std_rw_lock_t ndi_vlan_map_rwlock;

extern "C" {


void ndi_set_default_vlan_id (npu_id_t npu_id, hal_vlan_id_t vlan_id)
{
    default_vlan_id = vlan_id;
    if (ndi_create_vlan(npu_id, vlan_id) != STD_ERR_OK) {
        NDI_VLAN_LOG_ERROR("Default vlan(%u) create failed.", vlan_id);
    }

}

sai_object_id_t ndi_get_sai_vlan_obj_id(npu_id_t npu_id,
        hal_vlan_id_t vlan_id)
{
    std_rw_lock_read_guard m(&ndi_vlan_map_rwlock);
    auto it = g_ndi_vlan_map.find(vlan_id);
    if (it != g_ndi_vlan_map.end()) {
        return it->second;
    }
    return SAI_NULL_OBJECT_ID;
}

static t_std_error ndi_add_sai_vlan_obj_id(npu_id_t npu_id,
        hal_vlan_id_t vlan_id,
        sai_object_id_t vlan_obj_id)
{
    t_std_error rc = STD_ERR(NPU, FAIL, 0);
    std_rw_lock_write_guard m(&ndi_vlan_map_rwlock);
    auto it = g_ndi_vlan_map.find(vlan_id);
    if (it != g_ndi_vlan_map.end()) {
        return rc;
    }
    g_ndi_vlan_map[vlan_id] = vlan_obj_id;
    return STD_ERR_OK;
}

static t_std_error ndi_del_sai_vlan_obj_id(npu_id_t npu_id,
        hal_vlan_id_t vlan_id)
{
    t_std_error rc = STD_ERR(NPU, FAIL, 0);
    std_rw_lock_write_guard m(&ndi_vlan_map_rwlock);
    auto it = g_ndi_vlan_map.find(vlan_id);
    if (it == g_ndi_vlan_map.end()) {
        return rc;
    }
    g_ndi_vlan_map.erase(it);
    return STD_ERR_OK;
}


t_std_error ndi_get_vlan_member_info_from_cache(npu_id_t npu_id,
        hal_vlan_id_t vlan_id,
        sai_object_id_t  port_id,
        sai_object_id_t *vlan_member_id,
        sai_vlan_tagging_mode_t *tagging_mode)
{
    nas_ndi_map_key_t map_key;
    nas_ndi_map_data_t map_data;
    nas_ndi_map_val_t map_val;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    map_key.type = NAS_NDI_MAP_TYPE_VLAN_MEMBER_ID;
    map_key.id1 = (((sai_object_id_t)npu_id << 32) |
            ((sai_object_id_t)vlan_id));
    map_key.id2 = port_id;

    map_data.val1 = SAI_NULL_OBJECT_ID;
    map_data.val2 = SAI_NULL_OBJECT_ID;

    map_val.count = 1;
    map_val.data = &map_data;

    if((rc = nas_ndi_map_get(&map_key,&map_val)) == STD_ERR_OK) {
        if(vlan_member_id) {
            *vlan_member_id = map_data.val1;
        }

        if(tagging_mode) {
            *tagging_mode = (sai_vlan_tagging_mode_t)map_data.val2;
        }
    }

    return rc;
}

t_std_error ndi_add_vlan_member_to_cache(npu_id_t npu_id,
        hal_vlan_id_t vlan_id,
        sai_object_id_t port_id,
        sai_object_id_t vlan_member_id,
        sai_vlan_tagging_mode_t tagging_mode)
{
    nas_ndi_map_key_t map_key;
    nas_ndi_map_data_t map_data;
    nas_ndi_map_val_t map_val;
    sai_object_id_t tmp_vlan_member_id = SAI_NULL_OBJECT_ID;
    sai_vlan_tagging_mode_t tmp_tagging_mode;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    if(ndi_get_vlan_member_info_from_cache(npu_id,vlan_id,port_id,&tmp_vlan_member_id,
                &tmp_tagging_mode) != STD_ERR_OK) {
        map_key.type = NAS_NDI_MAP_TYPE_VLAN_MEMBER_ID;
        map_key.id1 = (((sai_object_id_t)npu_id << 32) |
                ((sai_object_id_t)vlan_id));
        map_key.id2 = port_id;

        map_data.val1 = vlan_member_id;
        map_data.val2 = tagging_mode;

        map_val.count = 1;
        map_val.data = &map_data;

        rc = nas_ndi_map_insert(&map_key,&map_val);
        if(STD_ERR_OK != rc) {
            return rc;
        }
    } else {
        if(tmp_tagging_mode != tagging_mode) {
            map_key.type = NAS_NDI_MAP_TYPE_VLAN_MEMBER_ID;
            map_key.id1 = (((sai_object_id_t)npu_id << 32) |
                    ((sai_object_id_t)vlan_id));
            map_key.id2 = port_id;

            rc = nas_ndi_map_delete(&map_key);
            if(STD_ERR_OK != rc) {
                return rc;
            }

            map_data.val1 = vlan_member_id;
            map_data.val2 = tagging_mode;

            map_val.count = 1;
            map_val.data = &map_data;

            rc = nas_ndi_map_insert(&map_key,&map_val);
            if(STD_ERR_OK != rc) {
                return rc;
            }
        } else {
            rc = STD_ERR_OK;
        }
    }

    return rc;
}

t_std_error ndi_del_vlan_member_from_cache(npu_id_t npu_id,
        hal_vlan_id_t vlan_id,
        sai_object_id_t port_id)
{
    nas_ndi_map_key_t map_key;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);
    map_key.type = NAS_NDI_MAP_TYPE_VLAN_MEMBER_ID;
    map_key.id1 = (((sai_object_id_t)npu_id << 32) |
            ((sai_object_id_t)vlan_id));
    map_key.id2 = port_id;

    rc = nas_ndi_map_delete(&map_key);
    if(STD_ERR_OK != rc) {
        return rc;
    }

    return rc;
}

static inline  sai_vlan_api_t *ndi_sai_vlan_api(nas_ndi_db_t *ndi_db_ptr)
{
     return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_vlan_api_tbl);
}

static t_std_error ndi_del_port_from_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id,
        sai_object_id_t port_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t vlan_member_id = SAI_NULL_OBJECT_ID;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    STD_ASSERT(ndi_db_ptr != NULL);

    rc = ndi_get_vlan_member_info_from_cache(npu_id,vlan_id,port_id,&vlan_member_id,
            NULL);
    if(vlan_member_id != SAI_NULL_OBJECT_ID) {
        if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->remove_vlan_member(
                        vlan_member_id))
                != SAI_STATUS_SUCCESS) {
            NDI_VLAN_LOG_ERROR("VLAN member del failed in SAI VLAN-id:%d"
                    " SAI-port:%lu SAI-status:%d",
                    vlan_id, port_id,sai_ret);
            return STD_ERR(NPU, FAIL, sai_ret);
        }

        if((rc = ndi_del_vlan_member_from_cache(npu_id,vlan_id,port_id)) !=
                STD_ERR_OK) {
            NDI_VLAN_LOG_ERROR("VLAN member cache del failed for VLAN-id:%d"
                    " SAI-port:%lu",
                    vlan_id, port_id);
            return rc;
        }
    } else {
        NDI_VLAN_LOG_ERROR("VLAN member cache search failed for VLAN-id:%d"
                " SAI-port:%lu",
                vlan_id, port_id);
    }

    return STD_ERR_OK;
}

static t_std_error ndi_add_port_to_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id,
        sai_object_id_t port_id, bool istagged)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t vlan_mem_attr[SAI_VLAN_MEMBER_ATTR_END];
    uint32_t attr_count=0;
    sai_object_id_t vlan_member_id = SAI_NULL_OBJECT_ID;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    sai_vlan_tagging_mode_t tagging_mode =
        istagged?SAI_VLAN_TAGGING_MODE_TAGGED:SAI_VLAN_TAGGING_MODE_UNTAGGED;
    sai_vlan_tagging_mode_t cur_tagging_mode;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    STD_ASSERT(ndi_db_ptr != NULL);

    ndi_get_vlan_member_info_from_cache(npu_id,vlan_id,port_id,&vlan_member_id,
            &cur_tagging_mode);
    /* Return OK if port is already added and in same tagging mode */
    if(SAI_NULL_OBJECT_ID == vlan_member_id) {
        vlan_mem_attr[attr_count].id = SAI_VLAN_MEMBER_ATTR_VLAN_ID;
        vlan_mem_attr[attr_count].value.oid =
            ndi_get_sai_vlan_obj_id(npu_id,vlan_id);
        attr_count++;

        /* Get bridge port id for sai port id */
        sai_object_id_t  brport_id;

        if (!ndi_get_1q_bridge_port(&brport_id, port_id)) {
            NDI_VLAN_LOG_ERROR("VLAN member : get bridge port failed for port %lu \n", port_id);
            return STD_ERR(NPU, CFG, 0);
        }
        vlan_mem_attr[attr_count].id = SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID;
        vlan_mem_attr[attr_count].value.oid = brport_id;
        attr_count++;

        vlan_mem_attr[attr_count].id =
            SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE;
        vlan_mem_attr[attr_count].value.s32 = tagging_mode;
        attr_count++;

        if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->create_vlan_member(
                        &vlan_member_id, 0, attr_count, vlan_mem_attr)) !=
                SAI_STATUS_SUCCESS) {
            NDI_VLAN_LOG_ERROR("VLAN member add failed in SAI VLAN-id:%d"
                    " SAI-port:%lu SAI-status:%d",
                    vlan_id, port_id,sai_ret);
            return STD_ERR(NPU, FAIL, sai_ret);
        }

        if((rc = ndi_add_vlan_member_to_cache(npu_id,vlan_id,port_id,
                    vlan_member_id,tagging_mode)) != STD_ERR_OK) {
            NDI_VLAN_LOG_ERROR("VLAN member cache add failed for VLAN-id:%d"
                    " SAI-port:%lu",
                    vlan_id, port_id);
            ndi_del_port_from_vlan(npu_id,vlan_id,port_id);
            return rc;
        }
    } else {
        if(cur_tagging_mode != tagging_mode) {
            attr_count = 0;
            vlan_mem_attr[attr_count].id =
                SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE;
            vlan_mem_attr[attr_count].value.s32 = tagging_mode;

            if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)-> \
                        set_vlan_member_attribute(
                            vlan_member_id, &vlan_mem_attr[attr_count])) !=
                    SAI_STATUS_SUCCESS) {
                NDI_VLAN_LOG_ERROR("VLAN member tagging mode change failed in"
                        " SAI VLAN-id:%d SAI-port:%lu SAI-status:%d",
                        vlan_id, port_id,sai_ret);
                return STD_ERR(NPU, FAIL, sai_ret);
            }

            if((rc = ndi_add_vlan_member_to_cache(npu_id,vlan_id,port_id,
                        vlan_member_id,tagging_mode)) != STD_ERR_OK) {
                NDI_VLAN_LOG_ERROR("VLAN member cache modify failed for"
                        " VLAN-id:%d SAI-port:%lu",
                        vlan_id, port_id);
                return rc;
            }
        } else {
            NDI_VLAN_LOG_ERROR("VLAN member SAI-port:%lu already added to VLAN-id:%d",
                    port_id,vlan_id);
        }
    }

    return STD_ERR_OK;
}

t_std_error ndi_create_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t vlan_attr;
    uint32_t attr_count=0;
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id);

    if(SAI_NULL_OBJECT_ID == vlan_obj_id) {

        vlan_attr.id = SAI_VLAN_ATTR_VLAN_ID;
        vlan_attr.value.u16 = (sai_vlan_id_t)vlan_id;
        attr_count++;

        if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->create_vlan(
                        &vlan_obj_id,
                        0,
                        attr_count,
                        &vlan_attr))
                != SAI_STATUS_SUCCESS) {
            return STD_ERR(NPU, FAIL, sai_ret);
        }

        if((rc = ndi_add_sai_vlan_obj_id(npu_id,vlan_id,vlan_obj_id)) !=
                STD_ERR_OK) {
            NDI_VLAN_LOG_ERROR("VLAN add failed for VLAN-id:%d",
                    vlan_id);
            return rc;
        }
        ndi_virtual_obj_t v_obj = {vlan_obj_id,vlan_id};
        if(!nas_ndi_add_virtual_obj(&v_obj)){
            NDI_VLAN_LOG_ERROR("Failed to add virtual obj cache mapping");
        }
    } else {
        NDI_VLAN_LOG_INFO("VLAN-id:%d already exists",
                vlan_id);
    }

    return STD_ERR_OK;
}
t_std_error ndi_delete_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id);

    if(SAI_NULL_OBJECT_ID != vlan_obj_id) {
            if(!ndi_mac_flush_vlan(vlan_id)){
                NDI_VLAN_LOG_ERROR("MAC flush on VLAN %d failed",vlan_id);
            }
            if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->remove_vlan(
                            vlan_obj_id))
                    != SAI_STATUS_SUCCESS) {
                return STD_ERR(NPU, FAIL, sai_ret);
            }

            if((rc = ndi_del_sai_vlan_obj_id(npu_id,vlan_id)) !=
                    STD_ERR_OK) {
                NDI_VLAN_LOG_ERROR("VLAN add failed for VLAN-id:%d",
                        vlan_id);
                return rc;
            }

            ndi_virtual_obj_t v_obj = {vlan_obj_id,vlan_id};
            if(!nas_ndi_remove_virtual_obj(&v_obj)){
                NDI_VLAN_LOG_ERROR("Failed to remove virtual obj cache mapping");
            }
        }
    return STD_ERR_OK;
}

t_std_error ndi_add_ports_to_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id,  \
        ndi_port_list_t *p_t_port_list, ndi_port_list_t *p_ut_port_list)
{
    int iter = 0;
    int t_count, ut_count;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);
    sai_object_id_t port_id = SAI_NULL_OBJECT_ID;
    ndi_port_t *p_ndi_port = NULL;

    t_count = p_t_port_list?(p_t_port_list->port_count):0;
    ut_count = p_ut_port_list?(p_ut_port_list->port_count):0;

    while((iter < t_count) || (iter < ut_count)) {
        if(iter < t_count) {
            p_ndi_port = &(p_t_port_list->port_list[iter]);
            if ((rc = ndi_sai_port_id_get(p_ndi_port->npu_id,
                            p_ndi_port->npu_port, &port_id)) != STD_ERR_OK) {
                NDI_VLAN_LOG_ERROR("SAI port id get failed for NPU-id:%d"
                        " NPU-port:%d",
                        p_ndi_port->npu_id, p_ndi_port->npu_port);
            } else {
                if((rc = ndi_add_port_to_vlan(npu_id,vlan_id,port_id,true)) !=
                    STD_ERR_OK) {
                    return rc;
                }
            }
        }

        if(iter < ut_count) {
            p_ndi_port = &(p_ut_port_list->port_list[iter]);
            if ((rc = ndi_sai_port_id_get(p_ndi_port->npu_id,
                            p_ndi_port->npu_port, &port_id)) != STD_ERR_OK) {
                NDI_VLAN_LOG_ERROR("SAI port id get failed for NPU-id:%d"
                        " NPU-port:%d",
                        p_ndi_port->npu_id, p_ndi_port->npu_port);
            } else {
                if((rc = ndi_add_port_to_vlan(npu_id,vlan_id,port_id,false)) !=
                    STD_ERR_OK) {
                    return rc;
                }
            }
        }

        iter++;
        if((iter >= t_count) && (iter >= ut_count)) {
            break;
        }
    }

    return STD_ERR_OK;
}

t_std_error ndi_del_ports_from_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id, \
        ndi_port_list_t *p_t_port_list, ndi_port_list_t *p_ut_port_list)
{
    int iter = 0;
    int t_count, ut_count;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);
    ndi_port_t *p_ndi_port = NULL;
    sai_object_id_t port_id = SAI_NULL_OBJECT_ID;

    t_count = p_t_port_list?(p_t_port_list->port_count):0;
    ut_count = p_ut_port_list?(p_ut_port_list->port_count):0;

    while((iter < t_count) || (iter < ut_count)) {
        if(iter < t_count) {
            p_ndi_port = &(p_t_port_list->port_list[iter]);
            if ((rc = ndi_sai_port_id_get(p_ndi_port->npu_id,
                            p_ndi_port->npu_port, &port_id)) != STD_ERR_OK) {
                NDI_VLAN_LOG_ERROR("SAI port id get failed for NPU-id:%d"
                        " NPU-port:%d",
                        p_ndi_port->npu_id, p_ndi_port->npu_port);
            } else {
                if((rc = ndi_del_port_from_vlan(npu_id,vlan_id,port_id)) !=
                        STD_ERR_OK) {
                    return rc;
                }
            }
        }

        if(iter < ut_count) {
            p_ndi_port = &(p_ut_port_list->port_list[iter]);
            if ((rc = ndi_sai_port_id_get(p_ndi_port->npu_id,
                            p_ndi_port->npu_port, &port_id)) != STD_ERR_OK) {
                NDI_VLAN_LOG_ERROR("SAI port id get failed for NPU-id:%d"
                        " NPU-port:%d",
                        p_ndi_port->npu_id, p_ndi_port->npu_port);
            } else {
                if((rc = ndi_del_port_from_vlan(npu_id,vlan_id,port_id)) !=
                        STD_ERR_OK) {
                    return rc;
                }
            }
        }

        iter++;
        if((iter >= t_count) && (iter >= ut_count)) {
            break;
        }
    }
    return STD_ERR_OK;
}

t_std_error ndi_vlan_stats_get(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                               ndi_stat_id_t *ndi_stat_ids,
                               uint64_t* stats_val, size_t len)
{
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    const unsigned int list_len = len;
    sai_vlan_stat_t sai_vlan_stats_ids[list_len];
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_VLAN_LOG_ERROR("Invalid NPU Id %d passed",npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    if ((vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id))
                == SAI_NULL_OBJECT_ID) {
        return STD_ERR(NPU, FAIL, SAI_STATUS_FAILURE);
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_vlan_stats(ndi_stat_ids[ix],&sai_vlan_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->get_vlan_stats(vlan_obj_id,
                    len, sai_vlan_stats_ids, stats_val)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Vlan stats Get failed for npu %d, vlan %d, ret %d \n",
                            npu_id, vlan_id, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}


t_std_error ndi_add_or_del_ports_to_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                                         ndi_port_list_t *p_tagged_list,
                                         ndi_port_list_t *p_untagged_list,
                                         bool add_vlan)
{
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;

    if ((vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id))
                == SAI_NULL_OBJECT_ID) {
        return STD_ERR(NPU, FAIL, SAI_STATUS_FAILURE);
    }

    if(add_vlan) {
        return(ndi_add_ports_to_vlan(npu_id, vlan_id,
                    p_tagged_list, p_untagged_list));
    }
    else {
        return(ndi_del_ports_from_vlan(npu_id, vlan_id,
                    p_tagged_list, p_untagged_list));
    }
    return STD_ERR_OK;
}

t_std_error ndi_set_vlan_learning(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                                  bool learning_mode)
{
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if(ndi_db_ptr == NULL){
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_status_t sai_ret;
    sai_attribute_t vlan_attr;

    vlan_attr.id = SAI_VLAN_ATTR_LEARN_DISABLE;
    vlan_attr.value.booldata = learning_mode;

    if ((vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id))
                == SAI_NULL_OBJECT_ID) {
        return STD_ERR(NPU, FAIL, SAI_STATUS_FAILURE);
    }

    if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->set_vlan_attribute(
                    vlan_obj_id,&vlan_attr))!= SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Returned failure %d while setting learning mode"
                " for VLAN ID %d",
                sai_ret, vlan_id);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_set_vlan_stp_instance(npu_id_t npu_id, hal_vlan_id_t vlan_id,
        sai_object_id_t stp_id)
{
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if(ndi_db_ptr == NULL){
        return STD_ERR(NPU, PARAM, 0);
    }
    sai_status_t sai_ret;
    sai_attribute_t vlan_attr;

    vlan_attr.id = SAI_VLAN_ATTR_STP_INSTANCE;
    vlan_attr.value.oid = stp_id;

    if ((vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id))
                == SAI_NULL_OBJECT_ID) {
        return STD_ERR(NPU, FAIL, SAI_STATUS_FAILURE);
    }

    if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->set_vlan_attribute(
                    vlan_obj_id,&vlan_attr)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Associating VLAN ID %d to STP instance ID %lu"
                " failed with error %d",vlan_id,stp_id,sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

bool ndi_vlan_get_default_obj_id(npu_id_t npu_id){

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if(ndi_db_ptr == NULL){
        return false;
    }

    hal_vlan_id_t vlan_id = (hal_vlan_id_t)default_vlan_id;
    sai_object_id_t sai_vlan_id = SAI_NULL_OBJECT_ID;
    sai_status_t sai_ret;
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_SWITCH_ATTR_DEFAULT_VLAN_ID;
    if ((sai_ret = ndi_sai_switch_api_tbl_get(ndi_db_ptr)->get_switch_attribute(
                   ndi_switch_id_get(),1,&sai_attr)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Default VLAN SAI obj id get failed %d",sai_ret);
        return false;
    }
    sai_vlan_id = sai_attr.value.oid;
    if(SAI_NULL_OBJECT_ID != sai_vlan_id) {
        ndi_add_sai_vlan_obj_id(npu_id,vlan_id,sai_vlan_id);
        ndi_virtual_obj_t v_obj = {sai_vlan_id,vlan_id};
        return nas_ndi_add_virtual_obj(&v_obj);
    }

    return false;
}

t_std_error ndi_vlan_delete_default_member_brports(npu_id_t npu_id, sai_object_id_t brport, bool del_all) {

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if(ndi_db_ptr == NULL){
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_status_t sai_ret;
    sai_attribute_t sai_attr[2];
    hal_vlan_id_t vlan_id = (hal_vlan_id_t)default_vlan_id;
    int count = 0;
    sai_object_id_t sai_vlan_id = SAI_NULL_OBJECT_ID;

    if((sai_vlan_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id)) == SAI_NULL_OBJECT_ID) {
        NDI_VLAN_LOG_ERROR("Default VLAN SAI obj id is SAI_NULL_OBJECT_ID");
        return STD_ERR(NPU,FAIL,0);
    }

    sai_attr[0].id = SAI_VLAN_ATTR_MEMBER_LIST;
    sai_attr[0].value.objlist.list = NULL;
    sai_attr[0].value.objlist.count = 0;

    sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->get_vlan_attribute(
            sai_vlan_id,1,&sai_attr[0]);

    if((sai_ret != SAI_STATUS_BUFFER_OVERFLOW) &&
        (sai_ret != SAI_STATUS_SUCCESS)) {
        NDI_VLAN_LOG_ERROR("Vlan member list count get failed %d"
                " for default VLAN",sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    count = sai_attr[0].value.objlist.count;

    if(count > 0)
    {
        sai_object_id_t member_list[count];
        int iter=0;

        sai_attr[0].value.objlist.list = member_list;

        if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->get_vlan_attribute(
                        sai_vlan_id,1,&sai_attr[0])) !=
                SAI_STATUS_SUCCESS) {
            NDI_VLAN_LOG_ERROR("Vlan member list get failed %d"
                    " for default VLAN",sai_ret);
            return STD_ERR(NPU, FAIL, sai_ret);
        }
        /* TODO: check if vlan member is correct */
        sai_attr[0].id = SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID;
        sai_attr[1].id = SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE;
        for(iter=0; iter<count; iter++) {
            if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->get_vlan_member_attribute(
                            member_list[iter],2,sai_attr)) !=
                    SAI_STATUS_SUCCESS) {
                NDI_VLAN_LOG_ERROR("Vlan member port get failed %d"
                        " for VLAN member ID %lu",sai_ret,member_list[iter]);
                continue;
            }

            if((sai_attr[0].value.oid == brport) || del_all) {
                if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->remove_vlan_member(
                                member_list[iter]))
                        != SAI_STATUS_SUCCESS) {
                    NDI_VLAN_LOG_ERROR("Default VLAN member del failed"
                            " SAI-port:%lu SAI-status:%d",
                            sai_attr[0].value.oid,sai_ret);
                    return STD_ERR(NPU, FAIL, sai_ret);
                }

                if(!del_all){
                    break;
                }
            }
        }
    }

    return STD_ERR_OK;
}


t_std_error ndi_del_new_member_from_default_vlan(npu_id_t npu_id,
        npu_port_t npu_port, bool del_all)
{
    sai_object_id_t sai_port_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t brport_id = SAI_NULL_OBJECT_ID;
    if(!del_all){
        if(ndi_sai_port_id_get(npu_id, npu_port, &sai_port_id) != STD_ERR_OK) {
            NDI_VLAN_LOG_ERROR("SAI port id get failed for NPU-id:%d"
                               " NPU-port:%d",npu_id,npu_port);
            return STD_ERR(NPU, PARAM, 0);
        }

        if (!ndi_get_1q_bridge_port(&brport_id, sai_port_id)) {
            NDI_VLAN_LOG_ERROR("VLAN member : get bridge port failed for port %lx \n", sai_port_id);
            return STD_ERR(NPU,PARAM,0);
        }
    }
    return ndi_vlan_delete_default_member_brports(npu_id, brport_id, del_all);
}




t_std_error ndi_get_sai_vlan_id(npu_id_t npu_id, sai_object_id_t vlan_obj_id,
        hal_vlan_id_t *vlan_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if(ndi_db_ptr == NULL){
        return STD_ERR(NPU, PARAM, 0);
    }
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t sai_attr;

    sai_attr.id = SAI_VLAN_ATTR_VLAN_ID;

    if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->get_vlan_attribute(
                    vlan_obj_id,1,&sai_attr)) !=
            SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Get VLAN ID from SAI object ID failed %d",sai_ret);

        return STD_ERR(NPU, FAIL, sai_ret);
    }

    *vlan_id = sai_attr.value.u16;

    return STD_ERR_OK;
}


t_std_error ndi_add_lag_to_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                                           ndi_obj_id_t *tagged_lag_list, size_t  tagged_lag_cnt,
                                           ndi_obj_id_t *untagged_lag_list ,size_t untag_lag_cnt) {

    size_t iter = 0;
    t_std_error rc;
    sai_object_id_t lag_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;

    if ((vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id))
                == SAI_NULL_OBJECT_ID) {
        return STD_ERR(NPU, FAIL, SAI_STATUS_FAILURE);
    }

    while((iter < tagged_lag_cnt) || (iter < untag_lag_cnt )) {
        if(iter < tagged_lag_cnt) {
            lag_id = tagged_lag_list[iter];
            if((rc = ndi_add_port_to_vlan(npu_id,vlan_id,lag_id,true)) !=
                STD_ERR_OK) {
                return rc;
            }
            NDI_LOG_TRACE("NDI_VLAN","Add Tag lag to vlan %d success for lagid 0x%lx",
               vlan_id, lag_id);
        }
        if(iter < untag_lag_cnt) {
            lag_id = untagged_lag_list[iter];
            if((rc = ndi_add_port_to_vlan(npu_id,vlan_id,lag_id,false)) !=
                STD_ERR_OK) {
                return rc;
            }
            NDI_LOG_TRACE("NDI_VLAN","Add Untag lag to vlan %d success for lagid 0x%lx",
               vlan_id, lag_id);
        }
        iter++;
    }
    return STD_ERR_OK;
}

t_std_error ndi_del_lag_from_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                                           ndi_obj_id_t *tagged_lag_list, size_t  tagged_lag_cnt,
                                           ndi_obj_id_t *untagged_lag_list ,size_t untag_lag_cnt) {

    size_t iter = 0;
    sai_object_id_t lag_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    t_std_error rc;

    if ((vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id))
                == SAI_NULL_OBJECT_ID) {
        return STD_ERR(NPU, FAIL, SAI_STATUS_FAILURE);
    }

    while((iter < tagged_lag_cnt) || (iter < untag_lag_cnt )) {
        if(iter < tagged_lag_cnt) {
            lag_id = tagged_lag_list[iter];
            if((rc = ndi_del_port_from_vlan(npu_id,vlan_id,lag_id)) !=
                        STD_ERR_OK) {
                return rc;
            }
            NDI_LOG_TRACE("NDI_VLAN","Del tag lag from vlan %d success for lagid 0x%lx",
                            vlan_id, lag_id);
        }
        if(iter < untag_lag_cnt) {
            lag_id = untagged_lag_list[iter];
            if((rc = ndi_del_port_from_vlan(npu_id,vlan_id,lag_id)) !=
                        STD_ERR_OK) {
                return rc;
            }
            NDI_LOG_TRACE("NDI_VLAN","Del untag lag from vlan %d success for lagid 0x%lx",
                            vlan_id, lag_id);
        }
        iter++;
    }

    return STD_ERR_OK;
}

t_std_error ndi_fill_vlan_mcast_attr(uint32_t af, ndi_vlan_mcast_lookup_key_type_t key,
                                     sai_attribute_t *vlan_attr)
{
    if (af == NDI_IPV4_VERSION)
        vlan_attr->id = SAI_VLAN_ATTR_IPV4_MCAST_LOOKUP_KEY_TYPE;
    else if (af == NDI_IPV6_VERSION)
        vlan_attr->id = SAI_VLAN_ATTR_IPV6_MCAST_LOOKUP_KEY_TYPE;
    else
        return STD_ERR(NPU, PARAM, 0);

    switch(key) {
        case NAS_NDI_VLAN_MCAST_LOOKUP_KEY_MACDA:
            vlan_attr->value.u32 = SAI_VLAN_MCAST_LOOKUP_KEY_TYPE_MAC_DA;
            break;
        case NAS_NDI_VLAN_MCAST_LOOKUP_KEY_XG:
            vlan_attr->value.u32 = SAI_VLAN_MCAST_LOOKUP_KEY_TYPE_XG;
            break;
        case NAS_NDI_VLAN_MCAST_LOOKUP_KEY_SG:
            vlan_attr->value.u32 = SAI_VLAN_MCAST_LOOKUP_KEY_TYPE_SG;
            break;
        case NAS_NDI_VLAN_MCAST_LOOKUP_KEY_XG_AND_SG:
            vlan_attr->value.u32 = SAI_VLAN_MCAST_LOOKUP_KEY_TYPE_XG_AND_SG;
            break;
        default:
            NDI_LOG_TRACE("NDI-VLAN-MCAST", "Unsupported mcast lookup key %d", key);
            return STD_ERR(NPU, PARAM, 0);
    }
    return STD_ERR_OK;
}

t_std_error ndi_vlan_set_mcast_lookup_key(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                                          uint32_t af,
                                          ndi_vlan_mcast_lookup_key_type_t key)
{
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if(ndi_db_ptr == NULL){
        return STD_ERR(NPU, PARAM, 0);
    }

    if ((vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id))
                == SAI_NULL_OBJECT_ID) {
        return STD_ERR(NPU, FAIL, SAI_STATUS_FAILURE);
    }

    sai_status_t sai_ret;
    sai_attribute_t vlan_attr;

    if(ndi_fill_vlan_mcast_attr(af, key, &vlan_attr) != STD_ERR_OK) {
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->set_vlan_attribute(vlan_obj_id,&vlan_attr);

    if (sai_ret == SAI_STATUS_SUCCESS) {
        NDI_LOG_TRACE("NDI-VLAN-MCAST", "Succesfully set mcast lookup key to %d"
                " for VLAN ID %d", key , vlan_id);
        return STD_ERR_OK;
    }
    if ((sai_ret == SAI_STATUS_NOT_IMPLEMENTED) ||
        (sai_ret ==  SAI_STATUS_ATTR_NOT_IMPLEMENTED_0)){
        NDI_LOG_TRACE("NDI-VLAN-MCAST", "Set mcast lookup key to %d"
                " for VLAN ID %d NOT implemented", key , vlan_id);
        return STD_ERR_OK;
    } else {
        NDI_VLAN_LOG_ERROR("Failed to set mcast lookup key to %d"
                " for VLAN ID %d (ret = %lx)", key, vlan_id, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

} // extern "C"
