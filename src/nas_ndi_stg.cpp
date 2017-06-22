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
 * filename: nas_ndi_stg.cpp
 */

#include "dell-base-stg.h"
#include "event_log.h"
#include "std_error_codes.h"

#include "nas_ndi_stg.h"
#include "nas_ndi_int.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_vlan_util.h"
#include "nas_ndi_map.h"

#include "saitypes.h"
#include "saiport.h"
#include "saistp.h"
#include "saivlan.h"
#include "saiswitch.h"

#include <unordered_map>
#include <functional>
#include <stdint.h>
#include <inttypes.h>
#include <vector>

#define NDI_STG_LOG(type,LVL,msg, ...) \
        EV_LOG( type, NAS_L2, LVL,"NDI-STG", msg, ##__VA_ARGS__)

static std::unordered_map<BASE_STG_INTERFACE_STATE_t, sai_stp_port_state_t,std::hash<int>>
ndi_to_sai_stp_state_map = {
        {BASE_STG_INTERFACE_STATE_DISABLED,SAI_STP_PORT_STATE_BLOCKING},
        {BASE_STG_INTERFACE_STATE_LEARNING,SAI_STP_PORT_STATE_LEARNING},
        {BASE_STG_INTERFACE_STATE_FORWARDING,SAI_STP_PORT_STATE_FORWARDING},
        {BASE_STG_INTERFACE_STATE_BLOCKING,SAI_STP_PORT_STATE_BLOCKING},
        {BASE_STG_INTERFACE_STATE_LISTENING,SAI_STP_PORT_STATE_BLOCKING}
};

static std::unordered_map<sai_stp_port_state_t, BASE_STG_INTERFACE_STATE_t, std::hash<int>>
sai_to_ndi_stp_state_map = {
    {SAI_STP_PORT_STATE_BLOCKING,BASE_STG_INTERFACE_STATE_BLOCKING},
    {SAI_STP_PORT_STATE_LEARNING,BASE_STG_INTERFACE_STATE_LEARNING},
    {SAI_STP_PORT_STATE_FORWARDING,BASE_STG_INTERFACE_STATE_FORWARDING}
};

static inline  sai_stp_api_t * ndi_stp_api_get(nas_ndi_db_t *ndi_db_ptr) {
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_stp_api_tbl);
}

static inline  sai_switch_api_t * ndi_switch_api_get(nas_ndi_db_t *ndi_db_ptr) {
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_switch_api_tbl);
}

static inline  sai_vlan_api_t * ndi_vlan_api_get(nas_ndi_db_t *ndi_db_ptr) {
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_vlan_api_tbl);
}

t_std_error ndi_stg_add_port_to_cache(sai_object_id_t stp_id,
        sai_object_id_t port_id,
        sai_object_id_t stp_port_id)
{
    nas_ndi_map_key_t map_key;
    nas_ndi_map_data_t map_data;
    nas_ndi_map_val_t map_val;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    map_key.type = NAS_NDI_MAP_TYPE_SAI_PORT_ID;
    map_key.id1 = stp_id;
    map_key.id2 = port_id;

    map_data.val1 = stp_port_id;
    map_data.val2 = SAI_NULL_OBJECT_ID;

    map_val.count = 1;
    map_val.data = &map_data;

    rc = nas_ndi_map_insert(&map_key,&map_val);

    if(STD_ERR_OK != rc) {
        return rc;
    }

    return STD_ERR_OK;
}

t_std_error ndi_stg_del_port_from_cache(sai_object_id_t stp_id,
        sai_object_id_t port_id)
{
    nas_ndi_map_key_t map_key;
    t_std_error rc = STD_ERR(NPU, FAIL, 0);

    map_key.type = NAS_NDI_MAP_TYPE_SAI_PORT_ID;
    map_key.id1 = stp_id;
    map_key.id2 = port_id;

    rc = nas_ndi_map_delete(&map_key);
    if(STD_ERR_OK != rc) {
        return rc;
    }

    return STD_ERR_OK;
}

sai_object_id_t ndi_stg_get_stp_port_id_from_cache(sai_object_id_t stp_id,
        sai_object_id_t port_id)
{
    nas_ndi_map_key_t map_key;
    nas_ndi_map_data_t map_data;
    nas_ndi_map_val_t map_val;

    map_key.type = NAS_NDI_MAP_TYPE_SAI_PORT_ID;
    map_key.id1 = stp_id;
    map_key.id2 = port_id;

    map_data.val1 = SAI_NULL_OBJECT_ID;
    map_data.val2 = SAI_NULL_OBJECT_ID;

    map_val.count = 1;
    map_val.data = &map_data;

    if(nas_ndi_map_get(&map_key,&map_val) == STD_ERR_OK) {
        return map_data.val1;
    }

    return SAI_NULL_OBJECT_ID;
}

sai_object_id_t ndi_stg_get_stp_port_id(nas_ndi_db_t *ndi_db_ptr,
        sai_object_id_t stg_id,
        sai_object_id_t port_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t stp_port_id = SAI_NULL_OBJECT_ID;
    sai_attribute_t attr_list[SAI_STP_PORT_ATTR_END] = {0};
    uint32_t attr_count = 0;

    attr_list[attr_count].id = SAI_STP_PORT_ATTR_STP;
    attr_list[attr_count].value.oid = stg_id;
    attr_count++;

    attr_list[attr_count].id = SAI_STP_PORT_ATTR_PORT;
    attr_list[attr_count].value.oid = port_id;
    attr_count++;

    attr_list[attr_count].id = SAI_STP_PORT_ATTR_STATE;
    /* Default port state is blocking so setting it to same during STG
       port object creation */
    attr_list[attr_count].value.s32 = SAI_STP_PORT_STATE_BLOCKING;
    attr_count++;

    if((stp_port_id = ndi_stg_get_stp_port_id_from_cache(stg_id,port_id)) ==
        SAI_NULL_OBJECT_ID)
    {
        if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->create_stp_port(
                        &stp_port_id,ndi_switch_id_get(),attr_count,attr_list))
                != SAI_STATUS_SUCCESS) {
            NDI_STG_LOG(ERR,0,"NDI STG Port creation failed with return"
                    " code %d",sai_ret);
            return SAI_NULL_OBJECT_ID;
        }

        if(ndi_stg_add_port_to_cache(stg_id,port_id,stp_port_id) !=
                STD_ERR_OK) {
            NDI_STG_LOG(ERR,0,"NDI STG Port add to cache failed");
        }
    }

    return stp_port_id;
}

t_std_error ndi_stg_add(npu_id_t npu_id, ndi_stg_id_t * stg_id){
    sai_status_t sai_ret;
    sai_object_id_t  sai_stp_id;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if(ndi_db_ptr == NULL){
        NDI_STG_LOG(ERR,0,"Invalid NPU id %d",npu_id);
        return STD_ERR(STG,PARAM,0);
    }

    const unsigned int attr_count = 0;

    if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->create_stp(&sai_stp_id,
                    ndi_switch_id_get(),attr_count,NULL))
            != SAI_STATUS_SUCCESS) {
        NDI_STG_LOG(ERR,0,"NDI STG Creation Failed with return code %d",sai_ret);
        return STD_ERR(STG, FAIL, sai_ret);
    }

    NDI_STG_LOG(INFO,3,"New STG Id %" PRIu64 " created",sai_stp_id);
    *stg_id = sai_stp_id;
    return STD_ERR_OK;
}

t_std_error ndi_stg_delete_all_stp_ports(nas_ndi_db_t *ndi_db_ptr,
        sai_object_id_t stg_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t attr;
    int count = 0;

    attr.id = SAI_STP_ATTR_PORT_LIST;
    attr.value.objlist.count = 0;
    attr.value.objlist.list = NULL;

    sai_ret = ndi_stp_api_get(ndi_db_ptr)->get_stp_attribute(stg_id,1,&attr);

    if((sai_ret != SAI_STATUS_BUFFER_OVERFLOW) &&
            (sai_ret != SAI_STATUS_SUCCESS)) {
        NDI_STG_LOG(ERR,0,"STG Id %" PRIu64 " port list count get failed with"
                        " return code %d",stg_id,sai_ret);
        return STD_ERR(STG, FAIL, sai_ret);
    }

    count = attr.value.objlist.count;

    if(count > 0) {
        sai_object_id_t stp_port_list[count];
        sai_attribute_t attr_list[SAI_STP_PORT_ATTR_END] = {0};
        uint32_t attr_count = 0;
        int iter=0;

        attr.value.objlist.list = stp_port_list;

        if((sai_ret = ndi_stp_api_get(ndi_db_ptr)->get_stp_attribute(stg_id,
                        1,&attr)) != SAI_STATUS_SUCCESS) {
            NDI_STG_LOG(ERR,0,"STG Id %" PRIu64 " port list get failed with"
                    " return code %d",stg_id,sai_ret);
            return STD_ERR(STG, FAIL, sai_ret);
        }

        attr_list[attr_count].id = SAI_STP_PORT_ATTR_STP;
        attr_count++;

        attr_list[attr_count].id = SAI_STP_PORT_ATTR_PORT;
        attr_count++;

        for(iter=0; iter<count; iter++) {
            if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->get_stp_port_attribute(
                            stp_port_list[iter],attr_count,attr_list))
                    != SAI_STATUS_SUCCESS) {
                NDI_STG_LOG(ERR,0,"STG port Id %" PRIu64 " port attribute get"
                        " failed with return code %d",stg_id,sai_ret);
                return STD_ERR(STG, FAIL, sai_ret);
            }

            if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->remove_stp_port(
                            stp_port_list[iter])) != SAI_STATUS_SUCCESS) {
                NDI_STG_LOG(ERR,0,"STG port Id %" PRIu64 " remove failed with"
                        " return code %d",stg_id,sai_ret);
                return STD_ERR(STG, FAIL, sai_ret);
            }

            if(ndi_stg_del_port_from_cache(attr_list[0].value.oid,
                        attr_list[1].value.oid) != STD_ERR_OK) {
                NDI_STG_LOG(ERR,0,"STG port Id %" PRIu64 " remove from cache"
                        " failed",stg_id);
                return STD_ERR(STG, FAIL, 0);
            }
        }
    }

    return STD_ERR_OK;
}

t_std_error ndi_stg_delete(npu_id_t npu_id, ndi_stg_id_t stg_id){
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if(ndi_db_ptr == NULL){
        NDI_STG_LOG(ERR,0,"Invalid NPU id %d",npu_id);
        return STD_ERR(STG,PARAM,0);
    }

    if(ndi_stg_delete_all_stp_ports(ndi_db_ptr, stg_id) != STD_ERR_OK) {
        NDI_STG_LOG(ERR,0,"STG Id %" PRIu64 " stp ports deletion failed",stg_id);
        return STD_ERR(STG, FAIL, 0);
    }

    if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->remove_stp(stg_id))!= SAI_STATUS_SUCCESS) {
        NDI_STG_LOG(ERR,0,"STG Id %" PRIu64 " deletion failed with return code %d",stg_id,sai_ret);
        return STD_ERR(STG, FAIL, sai_ret);
    }

    NDI_STG_LOG(INFO,3,"STG Id %" PRIu64 " deleted",stg_id);

    return STD_ERR_OK;
}


t_std_error ndi_stg_update_vlan(npu_id_t npu_id, ndi_stg_id_t  stg_id, hal_vlan_id_t vlan_id){
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if(ndi_db_ptr == NULL){
        NDI_STG_LOG(ERR,0,"Invalid NPU id %d",npu_id);
        return STD_ERR(STG,PARAM,0);
    }

    if(ndi_set_vlan_stp_instance(npu_id,vlan_id,
                (sai_object_id_t)stg_id) != STD_ERR_OK) {
        NDI_STG_LOG(ERR,0,"Associating VLAN ID %d to STG ID %" PRIu64
                " failed",vlan_id,stg_id);
        return STD_ERR(STG, FAIL, SAI_STATUS_FAILURE);
    }

    NDI_STG_LOG(INFO,3,"Associated VLAN ID %d to STG ID %" PRIu64 " ",vlan_id,stg_id);
    return STD_ERR_OK;
}


t_std_error ndi_stg_set_stp_port_state(npu_id_t npu_id, ndi_stg_id_t stg_id,
        npu_port_t port_id, BASE_STG_INTERFACE_STATE_t port_stp_state)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    sai_object_id_t stp_port_id = SAI_NULL_OBJECT_ID;
    sai_attribute_t attr;

    if(ndi_db_ptr == NULL){
        NDI_STG_LOG(ERR,0,"Invalid NPU id %d",npu_id);
        return STD_ERR(STG,PARAM,0);
    }

    sai_status_t sai_ret ;
    auto it = ndi_to_sai_stp_state_map.find(port_stp_state);
    if(it == ndi_to_sai_stp_state_map.end()) {
        NDI_STG_LOG(ERR,0,"NO SAI STP State found for %d",port_stp_state);
        return STD_ERR(STG,PARAM,0);
    }

    sai_object_id_t obj_id;
    if(ndi_sai_port_id_get( npu_id,port_id,&obj_id)!= STD_ERR_OK){
        NDI_STG_LOG(ERR,0,"Failed to get oid for npu %d and port %d",
                              npu_id,port_id);
        return STD_ERR(STG,FAIL,0);
    }

    if((stp_port_id = ndi_stg_get_stp_port_id(ndi_db_ptr,
                    (sai_object_id_t)stg_id, obj_id))
            != SAI_NULL_OBJECT_ID) {
        attr.id = SAI_STP_PORT_ATTR_STATE;
        attr.value.s32 = it->second;

        if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->set_stp_port_attribute(
                        stp_port_id, &attr)) != SAI_STATUS_SUCCESS) {
            NDI_STG_LOG(ERR,0,"Failed to Set stp state %d to port %d in stg id"
                    " %d with return code %d",
                    it->second,port_id,stg_id,sai_ret);
            return STD_ERR(STG, FAIL, sai_ret);
        }
    } else {
        NDI_STG_LOG(ERR,0,"Failed to get STG port object id for stg id %d and"
                " port %d",stg_id,obj_id);
        return STD_ERR(STG, FAIL, 0);
    }

    NDI_STG_LOG(INFO,3,"Set stp state %d to port %d in stg id %" PRIu64 "",
                                                 it->second,port_id,stg_id);
    return STD_ERR_OK;
}


t_std_error ndi_stg_get_stp_port_state(npu_id_t npu_id, ndi_stg_id_t stg_id,
        npu_port_t port_id, BASE_STG_INTERFACE_STATE_t *port_stp_state)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    sai_object_id_t stp_port_id = SAI_NULL_OBJECT_ID;
    sai_attribute_t attr;

    if(ndi_db_ptr == NULL){
        NDI_STG_LOG(ERR,0,"Invalid NPU id %d",npu_id);
        return STD_ERR(STG,PARAM,0);
    }

    sai_status_t sai_ret;
    sai_stp_port_state_t  sai_stp_state;

    sai_object_id_t obj_id;
    if(ndi_sai_port_id_get(npu_id,port_id,&obj_id)!= STD_ERR_OK){
        NDI_STG_LOG(ERR,0,"Failed to get oid for npu %d and port %d",
                          npu_id,port_id);
        return STD_ERR(STG,FAIL,0);
    }

    if((stp_port_id = ndi_stg_get_stp_port_id(ndi_db_ptr,
                    (sai_object_id_t)stg_id, obj_id))
            != SAI_NULL_OBJECT_ID) {
        attr.id = SAI_STP_PORT_ATTR_STATE;

        if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->get_stp_port_attribute(
                        stp_port_id,1,&attr)) != SAI_STATUS_SUCCESS) {
            NDI_STG_LOG(ERR,0,"Failed to get the STP Port State for STG id %" PRIu64 ""
                    "and Port id %d with return code %d",stg_id,port_id,sai_ret);
            return STD_ERR(STG, FAIL, sai_ret);
        }

        sai_stp_state = (sai_stp_port_state_t)attr.value.s32;
    } else {
        NDI_STG_LOG(ERR,0,"Failed to get STG port object id for stg id %d and"
                " port %d",stg_id,obj_id);
        return STD_ERR(STG, FAIL, 0);
    }

    NDI_STG_LOG(INFO,3,"Got the STP Port State for STG id %" PRIu64 " "
                                        "and Port id %d",stg_id,port_id);

    auto it = sai_to_ndi_stp_state_map.find(sai_stp_state);
    if(it == sai_to_ndi_stp_state_map.end()){
        NDI_STG_LOG(ERR,0,"NO SAI STP State found for %d",sai_stp_state);
        return STD_ERR(STG,PARAM,0);
    }
    *port_stp_state = it->second;
    return STD_ERR_OK;
}


t_std_error ndi_stg_get_default_id(npu_id_t npu_id, ndi_stg_id_t *stg_id, hal_vlan_id_t *vlan_id){

    if( (stg_id == NULL) || (vlan_id == NULL) ){
        NDI_STG_LOG(ERR,0,"Null Pointers passed to get default STG instance info");
        return STD_ERR(STG,PARAM,0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if(ndi_db_ptr == NULL){
        NDI_STG_LOG(ERR,0,"Invalid NPU id %d",npu_id);
        return STD_ERR(STG,PARAM,0);
    }

    sai_attribute_t default_stp;
    const unsigned int attr_count = 1;
    sai_status_t sai_ret;
    default_stp.id = SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID;


    if ((sai_ret = ndi_switch_api_get(ndi_db_ptr)->get_switch_attribute
                        (ndi_switch_id_get(),attr_count,&default_stp))!= SAI_STATUS_SUCCESS) {
        NDI_STG_LOG(ERR,0,"Failed to get the Default STP Id with return code %d",sai_ret);
        return STD_ERR(STG, FAIL, sai_ret);
    }

    *stg_id = default_stp.value.oid;

    NDI_STG_LOG(INFO,3,"Got the default STG instance id %" PRIu64 " ",*stg_id);

    sai_attribute_t default_vlan;
    default_vlan.id = SAI_STP_ATTR_VLAN_LIST;
    default_vlan.value.vlanlist.list = vlan_id;
    default_vlan.value.vlanlist.count = 1;


    if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->get_stp_attribute
                        (*stg_id,attr_count,&default_vlan))!= SAI_STATUS_SUCCESS) {
        NDI_STG_LOG(ERR,0,"Failed to get the VLAN associated with default STG"
                          " with return code %d",sai_ret);
        return STD_ERR(STG, FAIL, sai_ret);
    }

    NDI_STG_LOG(INFO,3,"Got the VLAN for default STG instance id %d ",*vlan_id);

    return STD_ERR_OK;

}

extern "C"{

t_std_error ndi_stg_set_all_stp_port_state(npu_id_t npu_id, ndi_stg_id_t stg_id,
                                                          BASE_STG_INTERFACE_STATE_t port_stp_state){
    size_t len=0;
    sai_object_id_t stp_port_id = SAI_NULL_OBJECT_ID;
    sai_attribute_t attr;

    if (ndi_port_get_sai_ports_len(npu_id,&len) != STD_ERR_OK){
        EV_LOGGING(NAS_L2,ERR,"SET-ALL-PORT-STATE","Couldn't get list len for sai port list");
        return STD_ERR(STG,FAIL,0);
    }

    std::vector<sai_object_id_t> sai_port_list;
    sai_port_list.resize(len);

    if (ndi_port_get_all_sai_ports(npu_id,&sai_port_list[0],len) != STD_ERR_OK){
        EV_LOGGING(NAS_L2,ERR,"SET-ALL-PORT-STATE","Couldn't get sai port list");
        return STD_ERR(STG,FAIL,0);
    }

    auto it = ndi_to_sai_stp_state_map.find(port_stp_state);
    if(it == ndi_to_sai_stp_state_map.end()) {
        EV_LOGGING(NAS_L2,ERR,"SET-ALL-PORT-STATE","NO SAI STP State found for %d",port_stp_state);
        return STD_ERR(STG,PARAM,0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if(ndi_db_ptr == NULL){
        EV_LOGGING(NAS_L2,ERR,"SET-ALL-PORT-STATE","Invalid NPU id %d",npu_id);
        return STD_ERR(STG,PARAM,0);
    }

    sai_status_t sai_ret;
    for (auto sai_obj : sai_port_list){
        if((stp_port_id = ndi_stg_get_stp_port_id(ndi_db_ptr,
                        (sai_object_id_t)stg_id,sai_obj))
                != SAI_NULL_OBJECT_ID) {
            attr.id = SAI_STP_PORT_ATTR_STATE;
            attr.value.s32 = it->second;

            if ((sai_ret = ndi_stp_api_get(ndi_db_ptr)->set_stp_port_attribute(
                            stp_port_id, &attr)) != SAI_STATUS_SUCCESS) {
                EV_LOGGING(NAS_L2,ERR,"SET-ALL-PORT-STATE","Failed to Set stp"
                        " state %d in stg id %d with return code %d",
                        it->second,stg_id,sai_ret);
                return STD_ERR(STG,FAIL,sai_ret);
            }
        } else {
            NDI_STG_LOG(ERR,0,"Failed to get STG port object id for stg id %d"
                    " and port %d",stg_id,sai_obj);
            return STD_ERR(STG, FAIL, 0);
        }
    }

    return STD_ERR_OK;
}

}
