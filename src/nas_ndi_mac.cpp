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
 * filename: nas_ndi_mac.c
 */

#include "std_error_codes.h"
#include "ds_common_types.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_mac.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_mac_utl.h"
#include "nas_ndi_obj_cache.h"
#include "sai.h"
#include "saistatus.h"
#include "saitypes.h"
#include "std_mac_utils.h"
#include <stdio.h>
#include <map>
#include <functional>
#include <string.h>

#define MAC_STR_LEN 20

#define NDI_MAC_LOG(LVL,msg, ...) EV_LOGGING(NDI,LVL,"NDI-MAC",msg, ##__VA_ARGS__)

static const unsigned int max_fdb_attr_get = NDI_MAC_ENTRY_ATTR_MAX;

static inline t_std_error sai_to_ndi_err_translate (sai_status_t sai_err)
{
    return ndi_utl_mk_std_err(e_std_err_MAC, sai_err);
}

/*  NDI L2 MAC specific APIs  */

static inline  sai_fdb_api_t *ndi_mac_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_fdb_api_tbl);
}


static bool ndi_mac_get_vlan_oid(hal_vlan_id_t vid, sai_object_id_t & oid){
    ndi_virtual_obj_t obj;
    obj.vid = vid;
    if(!nas_ndi_get_virtual_obj(&obj,ndi_virtual_obj_query_type_FROM_VLAN)){
        NDI_MAC_LOG(ERR,"Failed to find vlan object id for vlan id %d",vid);
        return false;
    }
    oid = obj.oid;
    return true;
}

bool ndi_mac_get_vlan_id(sai_object_id_t oid, hal_vlan_id_t & vlan_id){
    ndi_virtual_obj_t obj;
    obj.oid = oid;
    if(!nas_ndi_get_virtual_obj(&obj,ndi_virtual_obj_query_type_FROM_OBJ)){
        NDI_MAC_LOG(ERR,"Failed to find vlan object id for vlan obj id %llx",oid);
        return false;
    }
    vlan_id = obj.vid;
    return true;
}

static bool ndi_mac_get_brport(ndi_mac_entry_t * entry, sai_object_id_t & br_port){
     ndi_brport_obj_t brport_obj;
    if (entry->ndi_lag_id != 0) {
        brport_obj.port_obj_id = entry->ndi_lag_id;

    } else {
        if ((ndi_sai_port_id_get(entry->port_info.npu_id,
                        entry->port_info.npu_port, &brport_obj.port_obj_id)) != STD_ERR_OK) {
            NDI_MAC_LOG(ERR, "Not able to find SAI port id for npu:%d port:%d",
                entry->port_info.npu_id, entry->port_info.npu_port);
            return false;
        }
    }

    if(!nas_ndi_get_bridge_port_obj(&brport_obj,ndi_brport_query_type_FROM_PORT)){
        NDI_MAC_LOG(ERR,"Failed to find bridge port for port %llx",brport_obj.port_obj_id);
        return false;
    }

    br_port = brport_obj.brport_obj_id;
    return true;
}


static bool ndi_mac_get_port(sai_object_id_t brport, sai_object_id_t & port, ndi_port_type_t & port_type){
     ndi_brport_obj_t brport_obj;
    brport_obj.brport_obj_id = brport;

    if(!nas_ndi_get_bridge_port_obj(&brport_obj,ndi_brport_query_type_FROM_BRPORT)){
        NDI_MAC_LOG(ERR,"Failed to find bridge port for port %llx",brport_obj.port_obj_id);
        return false;
    }

    port = brport_obj.port_obj_id;
    port_type = brport_obj.port_type;
    return true;
}

t_std_error ndi_update_mac_entry(ndi_mac_entry_t *entry, ndi_mac_attr_flags attr_changed)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_fdb_entry_t sai_mac_entry;
    sai_attribute_t sai_attr;

    if (entry == NULL) {
        NDI_MAC_LOG(ERR,"Entry passed to update MAC entry is null");
        return STD_ERR(MAC,FAIL,0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(entry->npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_MAC_LOG(ERR, "Not able to find NDI API Table for npu_id: %d", entry->npu_id);
        return STD_ERR(MAC,FAIL,0);
    }

    memcpy(&(sai_mac_entry.mac_address), entry->mac_addr, HAL_MAC_ADDR_LEN);
    sai_object_id_t vlan_oid;
    if(!ndi_mac_get_vlan_oid(entry->vlan_id,vlan_oid)){
        return STD_ERR(MAC,FAIL,0);
    }
    sai_mac_entry.bv_id = vlan_oid;
    sai_mac_entry.switch_id = ndi_switch_id_get();

    switch (attr_changed) {
        case NDI_MAC_ENTRY_ATTR_PORT_ID:
        {

            sai_object_id_t brport;
            if(!ndi_mac_get_brport(entry,brport)){
                return STD_ERR(MAC,FAIL,0);
            }
            sai_attr.id = SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID;
            sai_attr.value.oid = brport;
        }
            break;
        case NDI_MAC_ENTRY_ATTR_PKT_ACTION:
            sai_attr.id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;
            sai_attr.value.s32 = ndi_mac_sai_packet_action_get(entry->action);
            break;
        default:
          NDI_MAC_LOG(INFO,"unsupported attribute value for update, attr %d", attr_changed);
          return STD_ERR_OK;
    }


    if ((sai_ret = ndi_mac_api_get(ndi_db_ptr)->set_fdb_entry_attribute(&sai_mac_entry, &sai_attr))
                          != SAI_STATUS_SUCCESS) {
        NDI_MAC_LOG(ERR,"Failed to update mac entry for vlan:%d: ret:%d",entry->vlan_id, sai_ret);
        return sai_to_ndi_err_translate(sai_ret);
    }

    return STD_ERR_OK;
}


t_std_error ndi_create_mac_entry(ndi_mac_entry_t *entry)
{

    uint32_t attr_idx = 0;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_fdb_entry_t sai_mac_entry;
    sai_attribute_t sai_attr[NDI_MAC_ENTRY_ATTR_MAX -1];

    if (entry == NULL) {
        NDI_MAC_LOG(ERR,"Entry passed to create MAC entry is null");
        return STD_ERR(MAC,FAIL,0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(entry->npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_MAC_LOG(ERR, "Not able to find NDI API Table for npu_id: %d", entry->npu_id);
        return STD_ERR(MAC,FAIL,0);
    }


    memcpy(&(sai_mac_entry.mac_address), entry->mac_addr, HAL_MAC_ADDR_LEN);
    sai_mac_entry.switch_id = ndi_switch_id_get();
    sai_object_id_t vlan_oid =0;
    if(!ndi_mac_get_vlan_oid(entry->vlan_id,vlan_oid)){
        return STD_ERR(MAC,FAIL,0);
    }
    sai_mac_entry.bv_id = vlan_oid;
    sai_object_id_t brport;
    if(!ndi_mac_get_brport(entry,brport)){
        return STD_ERR(MAC,FAIL,0);
    }

    sai_attr[attr_idx].id = SAI_FDB_ENTRY_ATTR_TYPE;
    sai_attr[attr_idx++].value.s32 = (entry->is_static) ? SAI_FDB_ENTRY_TYPE_STATIC : SAI_FDB_ENTRY_TYPE_DYNAMIC;

    sai_attr[attr_idx].id = SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID;
    sai_attr[attr_idx++].value.oid = brport;

    sai_attr[attr_idx].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;
    sai_attr[attr_idx++].value.s32 = ndi_mac_sai_packet_action_get(entry->action);

    if ((sai_ret = ndi_mac_api_get(ndi_db_ptr)->
            create_fdb_entry(&sai_mac_entry, attr_idx, sai_attr)) != SAI_STATUS_SUCCESS) {
        NDI_MAC_LOG(ERR,"Failed to configure mac entry ret:%d",sai_ret);
        return sai_to_ndi_err_translate(sai_ret);
    }

    return STD_ERR_OK;
}


 bool ndi_mac_handle_port_delete(ndi_mac_entry_t *entry, sai_attribute_t * sai_attrs,size_t & ix){
     sai_object_id_t brport;
     if(!ndi_mac_get_brport(entry,brport)){
         return false;
     }

     sai_attrs[ix].id = SAI_FDB_FLUSH_ATTR_BRIDGE_PORT_ID;
     sai_attrs[ix++].value.oid = brport;
     return true;
}

 bool ndi_mac_handle_vlan_delete(ndi_mac_entry_t *entry, sai_attribute_t * sai_attrs,size_t & ix){
    sai_object_id_t vlan_oid;
    if(!ndi_mac_get_vlan_oid(entry->vlan_id,vlan_oid)){
        return false;
    }
    sai_attrs[ix].id = SAI_FDB_FLUSH_ATTR_BV_ID;
    sai_attrs[ix++].value.oid = vlan_oid;
    return true;
}

 bool ndi_mac_handle_port_vlan_delete(ndi_mac_entry_t *entry, sai_attribute_t * sai_attrs,size_t & ix){
    return ndi_mac_handle_port_delete(entry,sai_attrs,ix) && ndi_mac_handle_vlan_delete(entry,sai_attrs,ix);
}


static auto _del_func_handlers = new  std::map<int,bool (*)
        (ndi_mac_entry_t * entry, sai_attribute_t * sai_attrs, size_t & ix)>
{
    {NDI_MAC_DEL_BY_PORT,ndi_mac_handle_port_delete },
    {NDI_MAC_DEL_BY_VLAN,ndi_mac_handle_vlan_delete },
    {NDI_MAC_DEL_BY_PORT_VLAN, ndi_mac_handle_port_vlan_delete},
};



t_std_error ndi_delete_mac_entry(ndi_mac_entry_t *entry, ndi_mac_delete_type_t delete_type, bool type_set)
{
    if (entry == NULL) {
        NDI_MAC_LOG(ERR, "MAC entry passed to delete is null");
        return STD_ERR(MAC,FAIL,0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(entry->npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_MAC_LOG(ERR, "Not able to find MAC NDI function table entry");
        return (STD_ERR(MAC, FAIL, 0));
    }

    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    if(delete_type == NDI_MAC_DEL_SINGLE_ENTRY){
        sai_fdb_entry_t sai_mac_entry;
        memcpy(&(sai_mac_entry.mac_address), entry->mac_addr, HAL_MAC_ADDR_LEN);
        sai_object_id_t vlan_oid;
        if(!ndi_mac_get_vlan_oid(entry->vlan_id,vlan_oid)){
            return STD_ERR(MAC,FAIL,0);
        }
        sai_mac_entry.bv_id = vlan_oid;
        sai_mac_entry.switch_id = ndi_switch_id_get();
        if ((sai_ret = ndi_mac_api_get(ndi_db_ptr)->remove_fdb_entry(&sai_mac_entry))
                                                                != SAI_STATUS_SUCCESS) {
            NDI_MAC_LOG(ERR,"Failed to remove mac entry for vlan:%d ret=%d",entry->vlan_id, sai_ret);
            return sai_to_ndi_err_translate(sai_ret);
        }
        return STD_ERR_OK;
    }

    size_t attr_idx = 0;
    sai_attribute_t fdb_flush_attr[3];
    memset(fdb_flush_attr, 0, sizeof(fdb_flush_attr));

    if (type_set) {
       fdb_flush_attr[attr_idx].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
       if (entry->is_static){
           fdb_flush_attr[attr_idx++].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_STATIC;
       }else{
           fdb_flush_attr[attr_idx++].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;
       }
   }


    if(delete_type != NDI_MAC_DEL_ALL_ENTRIES){
        auto it = _del_func_handlers->find(delete_type);
        if(it == _del_func_handlers->end()){
            NDI_MAC_LOG(ERR,"Invalid delete type passed %d",delete_type);
            return STD_ERR(MAC,PARAM,0);
        }

        if(!it->second(entry,fdb_flush_attr,attr_idx)){
            return STD_ERR(MAC,FAIL,0);
        }
    }

    if ((sai_ret = ndi_mac_api_get(ndi_db_ptr)->flush_fdb_entries(ndi_switch_id_get(),
                   attr_idx, (const sai_attribute_t *)fdb_flush_attr)) != SAI_STATUS_SUCCESS){
        NDI_MAC_LOG(ERR, "Failed to remove mac entries of type %d ret:%d",delete_type, sai_ret);
        return sai_to_ndi_err_translate(sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_mac_event_notify_register(ndi_mac_event_notification_fn reg_fn)
{
    t_std_error ret_code = STD_ERR_OK;
    npu_id_t npu_id = ndi_npu_id_get();
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_MAC_LOG(ERR, "Not able to find MAC NDI function table entry");
        return (STD_ERR(MAC, FAIL, 0));
    }

    STD_ASSERT(reg_fn != NULL);

    ndi_db_ptr->switch_notification->mac_event_notify_cb = reg_fn;

    return ret_code;
}

t_std_error ndi_get_mac_entry_attr(ndi_mac_entry_t *mac_entry)
{
    if (mac_entry == NULL) {
        EV_LOGGING(NDI,DEBUG,"NDI-MAC", "NULL MAC entry passed");
        return (STD_ERR(MAC,FAIL,0));
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(mac_entry->npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_MAC_LOG(ERR, "Not able to find MAC NDI function table entry");
        return (STD_ERR(MAC, FAIL, 0));
    }

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_fdb_entry_t sai_mac_entry;
    sai_attribute_t sai_attrs[max_fdb_attr_get];
    unsigned int sai_attr_ix = 0;
    memcpy(&(sai_mac_entry.mac_address), mac_entry->mac_addr, HAL_MAC_ADDR_LEN);
    sai_mac_entry.switch_id = ndi_switch_id_get();

    sai_attrs[sai_attr_ix++].id = SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID;
    sai_attrs[sai_attr_ix++].id = SAI_FDB_ENTRY_ATTR_TYPE;
    sai_attrs[sai_attr_ix++].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    sai_object_id_t vlan_oid = 0;
    if(!ndi_mac_get_vlan_oid(mac_entry->vlan_id,vlan_oid)){
        return STD_ERR(MAC,PARAM,0);
    }
    sai_mac_entry.bv_id = vlan_oid;

    if ((sai_ret = ndi_mac_api_get(ndi_db_ptr)->get_fdb_entry_attribute
                    (&sai_mac_entry,sai_attr_ix,sai_attrs))!= SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, DEBUG, "NDI-MAC","Failed to fetch mac entry from NDI");
        return sai_to_ndi_err_translate(sai_ret);
    }

    sai_attr_ix = 0;
    sai_object_id_t port_oid;
    ndi_port_type_t port_type;
    if(!ndi_mac_get_port(sai_attrs[sai_attr_ix++].value.oid,port_oid,port_type)){
        return STD_ERR(MAC,FAIL,0);
    }

    if(port_type == ndi_port_type_LAG){
        mac_entry->ndi_lag_id = port_oid;
    }else if(port_type == ndi_port_type_PORT){
        if (ndi_npu_port_id_get(port_oid,&mac_entry->port_info.npu_id,
                                &mac_entry->port_info.npu_port) != STD_ERR_OK) {
            EV_LOGGING(NDI,DEBUG, "NDI-MAC", "Port get failed: sai port 0x%llx", port_oid);
            return STD_ERR(INTERFACE, FAIL, 0);
        }
    }else{
        NDI_MAC_LOG(ERR,"Invalid port type retured %d",port_type);
    }

    mac_entry->is_static = (sai_attrs[sai_attr_ix++].value.s32 == SAI_FDB_ENTRY_TYPE_STATIC) ? true : false;
    mac_entry->action = ndi_mac_packet_action_get((sai_packet_action_t)sai_attrs[sai_attr_ix++].value.s32);

    return STD_ERR_OK;
}

/*
 * need to remove this function.
 */
t_std_error ndi_set_mac_entry_attr(ndi_mac_entry_t *mac_entry)
{
    return STD_ERR_OK;
}


/* Flush all entries for a bridge port */
t_std_error ndi_flush_bridge_port_entry(sai_object_id_t brport_oid) {

    npu_id_t npu_id = ndi_npu_id_get();
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_MAC_LOG(ERR, "Not able to find MAC NDI function table entry");
        return (STD_ERR(MAC, FAIL, 0));
    }

    sai_status_t  sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t fdb_flush_attr;
    size_t fdb_flush_attr_count = 1;
    memset(&fdb_flush_attr, 0, sizeof(fdb_flush_attr));
    fdb_flush_attr.id = SAI_FDB_FLUSH_ATTR_BRIDGE_PORT_ID;
    fdb_flush_attr.value.oid = brport_oid;
    if ((sai_ret = ndi_mac_api_get(ndi_db_ptr)->flush_fdb_entries(ndi_switch_id_get(),
        fdb_flush_attr_count, (const sai_attribute_t *)&fdb_flush_attr)) != SAI_STATUS_SUCCESS) {
        NDI_MAC_LOG(ERR,"NDI-MAC", "Failed to remove mac entries for bridge port 0x%llx", brport_oid);
        return sai_to_ndi_err_translate(sai_ret);
    }
    return STD_ERR_OK;
}


bool ndi_mac_flush_vlan(hal_vlan_id_t vlan_id){

    sai_status_t  sai_ret = SAI_STATUS_FAILURE;
    npu_id_t npu_id = ndi_npu_id_get();
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_MAC_LOG(ERR, "Not able to find MAC NDI function table entry");
        return (STD_ERR(MAC, FAIL, 0));
    }

    size_t attr_idx = 0;
    sai_attribute_t fdb_flush_attr[2];

    memset(fdb_flush_attr, 0, sizeof(fdb_flush_attr));
    fdb_flush_attr[attr_idx++].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;

    sai_object_id_t vlan_oid;
    if(!ndi_mac_get_vlan_oid(vlan_id,vlan_oid)){
        return false;
    }

    fdb_flush_attr[attr_idx].id = SAI_FDB_FLUSH_ATTR_BV_ID;
    fdb_flush_attr[attr_idx++].value.oid = vlan_oid;


    if ((sai_ret = ndi_mac_api_get(ndi_db_ptr)->flush_fdb_entries(ndi_switch_id_get(),
                    attr_idx, (const sai_attribute_t *)fdb_flush_attr)) != SAI_STATUS_SUCCESS){
        NDI_MAC_LOG(ERR, "Failed to remove mac entries for vlan %d",vlan_id);
        return sai_to_ndi_err_translate(sai_ret);
    }

    return STD_ERR_OK;

}
