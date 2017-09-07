/*
 * Copyright (c) 2016 Dell Inc.
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
 * filename: nas_ndi_vlan.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "std_error_codes.h"
#include "std_assert.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_vlan.h"
#include "nas_ndi_utils.h"
#include "sai.h"
#include "saivlan.h"
#include "nas_vlan_consts.h"
#include "nas_ndi_map.h"

sai_object_id_t ndi_get_sai_vlan_obj_id(npu_id_t npu_id,
        hal_vlan_id_t vlan_id)
{
    nas_ndi_map_key_t map_key;
    nas_ndi_map_data_t map_data;
    nas_ndi_map_val_t map_val;

    if((vlan_id > 0) && (vlan_id < NAS_MAX_VLAN_ID)) {
        map_key.type = NAS_NDI_MAP_TYPE_NPU_VLAN_ID;
        map_key.id1 = (((sai_object_id_t)npu_id << 32) |
                ((sai_object_id_t)vlan_id));
        map_key.id2 = SAI_NULL_OBJECT_ID;

        map_data.val1 = SAI_NULL_OBJECT_ID;
        map_data.val2 = SAI_NULL_OBJECT_ID;

        map_val.count = 1;
        map_val.data = &map_data;

        nas_ndi_map_get(&map_key,&map_val);

        return(map_data.val1);
    }
    return SAI_NULL_OBJECT_ID;
}

t_std_error ndi_add_sai_vlan_obj_id(npu_id_t npu_id,
        hal_vlan_id_t vlan_id,
        sai_object_id_t vlan_obj_id)
{
    nas_ndi_map_key_t map_key;
    nas_ndi_map_data_t map_data;
    nas_ndi_map_val_t map_val;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    if((vlan_id > 0) && (vlan_id < NAS_MAX_VLAN_ID)) {
        if(SAI_NULL_OBJECT_ID ==
                ndi_get_sai_vlan_obj_id(npu_id,vlan_id)) {
            map_key.type = NAS_NDI_MAP_TYPE_NPU_VLAN_ID;
            map_key.id1 = (((sai_object_id_t)npu_id << 32) |
                    ((sai_object_id_t)vlan_id));
            map_key.id2 = SAI_NULL_OBJECT_ID;

            map_data.val1 = vlan_obj_id;
            map_data.val2 = SAI_NULL_OBJECT_ID;

            map_val.count = 1;
            map_val.data = &map_data;

            rc = nas_ndi_map_insert(&map_key,&map_val);
        } else {
            rc = STD_ERR_OK;
        }
    } else {
        rc = STD_ERR(NPU, PARAM, 0);
    }
    return rc;
}

t_std_error ndi_del_sai_vlan_obj_id(npu_id_t npu_id,
        hal_vlan_id_t vlan_id)
{
    nas_ndi_map_key_t map_key;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    if((vlan_id > 0) && (vlan_id < NAS_MAX_VLAN_ID)) {
        map_key.type = NAS_NDI_MAP_TYPE_NPU_VLAN_ID;
        map_key.id1 = (((sai_object_id_t)npu_id << 32) |
                ((sai_object_id_t)vlan_id));
        map_key.id2 = SAI_NULL_OBJECT_ID;

        rc = nas_ndi_map_delete(&map_key);
    } else {
        rc = STD_ERR(NPU, PARAM, 0);
    }
    return rc;
}

t_std_error ndi_get_vlan_member_list_from_cache(npu_id_t npu_id,
        hal_vlan_id_t vlan_id,
        nas_ndi_map_data_t *p_map_data,
        size_t *count)
{
    nas_ndi_map_key_t map_key;
    nas_ndi_map_val_t map_val;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    if(!count) {
        return rc;
    }

    map_key.type = NAS_NDI_MAP_TYPE_VLAN_PORTS;
    map_key.id1 = (((sai_object_id_t)npu_id << 32) |
            ((sai_object_id_t)vlan_id));
    map_key.id2 = SAI_NULL_OBJECT_ID;

    if(p_map_data) {
        map_val.count = *count;
        map_val.data = p_map_data;

        rc = nas_ndi_map_get(&map_key, &map_val);
        if(STD_ERR_OK == rc) {
            *count = map_val.count;
        }
    } else {
        rc = nas_ndi_map_get_val_count(&map_key, count);
    }

    if(STD_ERR(NPU, NEXIST, 0) == rc) {
        *count = 0;
        rc = STD_ERR_OK;
    }

    return rc;
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
            *tagging_mode = map_data.val2;
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
        map_key.type = NAS_NDI_MAP_TYPE_VLAN_PORTS;
        map_key.id1 = (((sai_object_id_t)npu_id << 32) |
                ((sai_object_id_t)vlan_id));
        map_key.id2 = SAI_NULL_OBJECT_ID;

        map_data.val1 = port_id;
        map_data.val2 = vlan_member_id;

        map_val.count = 1;
        map_val.data = &map_data;

        rc = nas_ndi_map_insert(&map_key,&map_val);
        if(STD_ERR_OK != rc) {
            return rc;
        }

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
    nas_ndi_map_val_filter_t filter;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    map_key.type = NAS_NDI_MAP_TYPE_VLAN_PORTS;
    map_key.id1 = (((sai_object_id_t)npu_id << 32) |
            ((sai_object_id_t)vlan_id));
    map_key.id2 = SAI_NULL_OBJECT_ID;

    filter.value.val1 = port_id;
    filter.value.val2 = SAI_NULL_OBJECT_ID;
    filter.type = NAS_NDI_MAP_VAL_FILTER_VAL1;

    rc = nas_ndi_map_delete_elements(&map_key,&filter);
    if(STD_ERR_OK != rc) {
        return rc;
    }

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
                " SAI-port:%d",
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

        vlan_mem_attr[attr_count].id = SAI_VLAN_MEMBER_ATTR_PORT_ID;
        vlan_mem_attr[attr_count].value.oid = port_id;
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
    } else {
        NDI_VLAN_LOG_ERROR("VLAN-id:%d already exists",
                vlan_id);
    }

    return STD_ERR_OK;
}

t_std_error ndi_delete_vlan_members(npu_id_t npu_id, hal_vlan_id_t vlan_id)
{
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    size_t count = 0;
    int i = 0;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    STD_ASSERT(ndi_db_ptr != NULL);

    vlan_obj_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id);

    if(SAI_NULL_OBJECT_ID != vlan_obj_id) {
        if((rc = ndi_get_vlan_member_list_from_cache(npu_id,vlan_id,NULL,
                        &count)) == STD_ERR_OK) {
            if(count > 0) {
                nas_ndi_map_data_t map_data[count];

                if((rc = ndi_get_vlan_member_list_from_cache(npu_id,vlan_id,
                                map_data,&count)) == STD_ERR_OK) {
                    for(i=0; i<count; i++) {
                        rc |= ndi_del_port_from_vlan(npu_id,vlan_id,
                                map_data[i].val1);
                    }
                }
            }
        }
    } else {
        NDI_VLAN_LOG_ERROR("VLAN delete members failed for VLAN-id:%d",
                vlan_id);
        return STD_ERR(NPU, FAIL, 0);
    }
    return rc;
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
        if((rc = ndi_delete_vlan_members(npu_id, vlan_id)) == STD_ERR_OK) {
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
        } else {
            return rc;
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
                    sai_vlan_stats_ids, len, stats_val)) != SAI_STATUS_SUCCESS) {
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

t_std_error ndi_del_new_member_from_default_vlan(npu_id_t npu_id,
        npu_port_t npu_port, bool del_all)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if(ndi_db_ptr == NULL){
        return STD_ERR(NPU, PARAM, 0);
    }
    sai_status_t sai_ret;
    sai_attribute_t sai_attr[2];
    hal_vlan_id_t vlan_id = (hal_vlan_id_t)1;
    int count = 0;
    sai_object_id_t sai_vlan_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t port_id = SAI_NULL_OBJECT_ID;

    if(!(del_all) &&
            (ndi_sai_port_id_get(npu_id,npu_port,&port_id) != STD_ERR_OK)) {
        NDI_VLAN_LOG_ERROR("SAI port id get failed for NPU-id:%d"
                " NPU-port:%d",npu_id,npu_port);
        return STD_ERR(NPU, PARAM, 0);
    }

    if((sai_vlan_id = ndi_get_sai_vlan_obj_id(npu_id,vlan_id))
            == SAI_NULL_OBJECT_ID) {
        sai_attr[0].id = SAI_SWITCH_ATTR_DEFAULT_VLAN_ID;
        if ((sai_ret = ndi_sai_switch_api_tbl_get(ndi_db_ptr)->get_switch_attribute(
                        ndi_switch_id_get(),1,&sai_attr[0])) != SAI_STATUS_SUCCESS) {
            NDI_VLAN_LOG_ERROR("Default VLAN SAI obj id get failed %d",sai_ret);
            return STD_ERR(NPU, FAIL, sai_ret);
        }
        sai_vlan_id = sai_attr[0].value.oid;
        if(SAI_NULL_OBJECT_ID != sai_vlan_id) {
            ndi_add_sai_vlan_obj_id(npu_id,vlan_id,sai_vlan_id);
        }
    }

    if(SAI_NULL_OBJECT_ID == sai_vlan_id) {
        NDI_VLAN_LOG_ERROR("Default VLAN SAI obj id is SAI_NULL_OBJECT_ID");
        return STD_ERR(NPU, FAIL, 0);
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

        sai_attr[0].id = SAI_VLAN_MEMBER_ATTR_PORT_ID;
        sai_attr[1].id = SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE;
        for(iter=0; iter<count; iter++) {
            if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->get_vlan_member_attribute(
                            member_list[iter],2,sai_attr)) !=
                    SAI_STATUS_SUCCESS) {
                NDI_VLAN_LOG_ERROR("Vlan member port get failed %d"
                        " for VLAN member ID %d",sai_ret,member_list[iter]);
                continue;
            }

            if((del_all) || (sai_attr[0].value.oid == port_id)){
                if ((sai_ret = ndi_sai_vlan_api(ndi_db_ptr)->remove_vlan_member(
                                member_list[iter]))
                        != SAI_STATUS_SUCCESS) {
                    NDI_VLAN_LOG_ERROR("Default VLAN member del failed"
                            " SAI-port:%lu SAI-status:%d",
                            sai_attr[0].value.oid,sai_ret);
                    return STD_ERR(NPU, FAIL, sai_ret);
                }

                if(!(del_all)) {
                    break;
                }
            }
        }

        if((iter == count) && !(del_all)) {
            NDI_VLAN_LOG_ERROR("SAI-port:%lu not found in default"
                    " VLAN member list", port_id);
            return STD_ERR(NPU, FAIL, 0);
        }
    } else {
        NDI_LOG_TRACE("NDI-VLAN","Default vlan memberlist get is empty");
    }

    return STD_ERR_OK;
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
