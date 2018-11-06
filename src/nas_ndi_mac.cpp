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
#include "nas_ndi_mac_utl.h"

#include <stdio.h>
#include <map>
#include <functional>
#include <string.h>
#include <inttypes.h>

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

static bool _get_sai_port_id(ndi_mac_entry_t *entry, ndi_brport_obj_t & brport_obj){
    if (entry->ndi_lag_id != 0) {
        brport_obj.port_obj_id = entry->ndi_lag_id;
        return true;
    }

    if ((ndi_sai_port_id_get(entry->port_info.npu_id,
            entry->port_info.npu_port, &brport_obj.port_obj_id)) == STD_ERR_OK) {
            return true;
    }

    NDI_MAC_LOG(ERR, "Not able to find SAI port id for npu:%d port:%d",
                entry->port_info.npu_id, entry->port_info.npu_port);
    return false;
}


static bool ndi_mac_get_brport(ndi_mac_entry_t * entry, sai_object_id_t & br_port){
    ndi_brport_obj_t brport_obj;
    if(!_get_sai_port_id(entry,brport_obj)){
        return false;
    }

    if(!nas_ndi_get_bridge_port_obj(&brport_obj,ndi_brport_query_type_FROM_PORT)){
        NDI_MAC_LOG(ERR,"Failed to find bridge port for port %llx",brport_obj.port_obj_id);
        return false;
    }

    br_port = brport_obj.brport_obj_id;
    if (br_port == SAI_NULL_OBJECT_ID) {
         NDI_MAC_LOG(ERR,"Failed to find bridge port for sai lag or port 0x%llx",brport_obj.port_obj_id);
         return false;
    }
    return true;
}


static bool _ndi_mac_get_bridge_port_from_pv(ndi_mac_entry_t * _entry, sai_object_id_t & sai_oid){

    ndi_brport_obj_t _br_port;

    if(!_get_sai_port_id(_entry,_br_port)){
        return false;
    }

    _br_port.vlan_id = _entry->vlan_id;

     if(!nas_ndi_get_bridge_port_obj(&_br_port,ndi_brport_query_type_FROM_PORT_VLAN)){
         NDI_MAC_LOG(ERR,"Failed to find bridge port for port %llx",_br_port.port_obj_id);
         return false;
     }

    sai_oid = _br_port.brport_obj_id;
    return true;
}


static bool _get_brport_from_entry(ndi_mac_entry_t * entry, sai_object_id_t & brport){
    if(entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1D_LOCAL){
        if(_ndi_mac_get_bridge_port_from_pv(entry,brport)){
            return true;
        }
    }else if(entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1D_REMOTE){
        brport = entry->endpoint_ip_port;
        return true;
    }else{
         if(ndi_mac_get_brport(entry,brport)){
           return true;
         }
    }

    return false;
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
    if(entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1Q){
        if(!ndi_mac_get_vlan_oid(entry->vlan_id,vlan_oid)){
            return STD_ERR(MAC,FAIL,0);
        }
        sai_mac_entry.bv_id = vlan_oid;
    }else {
        sai_mac_entry.bv_id = entry->bridge_id;
    }

    sai_mac_entry.switch_id = ndi_switch_id_get();

    switch (attr_changed) {
        case NDI_MAC_ENTRY_ATTR_PORT_ID:
        {

            sai_object_id_t brport;
            if(!_get_brport_from_entry(entry,brport)){
                NDI_MAC_LOG(ERR,"Failed to get bridge port for entry type %d",entry->mac_entry_type);
                return STD_ERR(MAC,PARAM,0);
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


static bool _get_port_vlan_from_bridge_port(sai_object_id_t & brport ,ndi_mac_entry_t * entry){
    ndi_brport_obj_t _br_port;
    _br_port.brport_obj_id  = brport;

    if(!nas_ndi_get_bridge_port_obj(&_br_port,ndi_brport_query_type_FROM_BRPORT)){
        NDI_MAC_LOG(ERR,"Failed to find bridge port for port %llx",_br_port.port_obj_id);
        return false;
    }

    if(entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1D_LOCAL){
        if (_br_port.brport_type == ndi_brport_type_SUBPORT_UNTAG) {
            EV_LOGGING(NDI,DEBUG,"NDI-MAC","returning 0 vlan id for brport %llx with real vlan id %d",
                                _br_port.port_obj_id, _br_port.vlan_id);
            entry->vlan_id = 0;
        } else {
            entry->vlan_id = _br_port.vlan_id;
        }
    }

    if(_br_port.port_type == ndi_port_type_PORT){
        if (ndi_npu_port_id_get(_br_port.port_obj_id,&entry->port_info.npu_id,
                                &entry->port_info.npu_port) != STD_ERR_OK) {
            EV_LOGGING(NDI,DEBUG, "NDI-MAC", "Port get failed: sai port 0x%llx",_br_port.port_obj_id);
            return false;
        }
    }else if (_br_port.port_type == ndi_port_type_LAG){
        entry->ndi_lag_id = _br_port.port_obj_id;
    }else{
        NDI_MAC_LOG(ERR,"Invalid Port type %d when getting the bridge port",_br_port.port_type);
        return false;
    }

    return true;
}


t_std_error ndi_create_mac_entry(ndi_mac_entry_t *entry)
{
    uint32_t attr_idx = 0;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_fdb_entry_t sai_mac_entry;
    sai_attribute_t sai_attr[NDI_MAC_ENTRY_ATTR_MAX -1];
    sai_object_id_t sai_brport;

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

    if(!_get_brport_from_entry(entry,sai_brport)){
        NDI_MAC_LOG(ERR,"Failed to get bridge port for entry type %d",entry->mac_entry_type);
        return STD_ERR(MAC,PARAM,0);
    }

    if(entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1D_LOCAL){
        sai_mac_entry.bv_id = entry->bridge_id;
    }else if(entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1D_REMOTE){
        sai_mac_entry.bv_id = entry->bridge_id;
        sai_attr[attr_idx].id= SAI_FDB_ENTRY_ATTR_ENDPOINT_IP;
        if(entry->endpoint_ip.af_index == HAL_INET4_FAMILY){
            sai_attr[attr_idx].value.ipaddr.addr.ip4 = entry->endpoint_ip.u.ipv4.s_addr;
            sai_attr[attr_idx++].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        }else if(entry->endpoint_ip.af_index == HAL_INET6_FAMILY){
            memcpy(sai_attr[attr_idx].value.ipaddr.addr.ip6 ,entry->endpoint_ip.u.ipv6.s6_addr,
                    sizeof(sai_attr[attr_idx].value.ipaddr.addr.ip6));
            sai_attr[attr_idx++].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        }else{
            NDI_MAC_LOG(ERR,"Invalid address family for remote IP %d",entry->endpoint_ip.af_index);
            return STD_ERR(MAC,FAIL,0);
        }
    }else{
        /*
         * if mac_entry_type is not passed then assume it is 1Q for backward compatibility
         */
        sai_object_id_t vlan_oid = 0;
        if(!ndi_mac_get_vlan_oid(entry->vlan_id,vlan_oid)){
            NDI_MAC_LOG(ERR,"Failed to get vlan object id for vlan id %d",entry->vlan_id);
            return STD_ERR(MAC,FAIL,0);
        }
        sai_mac_entry.bv_id = vlan_oid;
    }


    sai_attr[attr_idx].id = SAI_FDB_ENTRY_ATTR_TYPE;
    sai_attr[attr_idx++].value.s32 = (entry->is_static) ? SAI_FDB_ENTRY_TYPE_STATIC : SAI_FDB_ENTRY_TYPE_DYNAMIC;

    sai_attr[attr_idx].id = SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID;
    sai_attr[attr_idx++].value.oid = sai_brport;

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

     if(!_get_brport_from_entry(entry,brport)){
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

bool ndi_mac_handle_bridge_delete(ndi_mac_entry_t *entry, sai_attribute_t * sai_attrs,size_t & ix){
    sai_attrs[ix].id = SAI_FDB_FLUSH_ATTR_BV_ID;
    sai_attrs[ix++].value.oid = entry->bridge_id;
    return true;
}

bool ndi_mac_handle_port_vlan_subport_delete(ndi_mac_entry_t *entry, sai_attribute_t * sai_attrs, size_t &ix){
    sai_object_id_t brport;
    if(_ndi_mac_get_bridge_port_from_pv(entry,brport)){
        sai_attrs[ix].id = SAI_FDB_FLUSH_ATTR_BRIDGE_PORT_ID;
        sai_attrs[ix++].value.oid = brport;
        return true;
    }
    return false;
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
    {NDI_MAC_DEL_BY_BRIDGE, ndi_mac_handle_bridge_delete},
    {NDI_MAC_DEL_BY_BRIDGE_ENDPOINT_IP, ndi_mac_handle_port_delete},
    {NDI_MAC_DEL_BY_PORT_VLAN_SUBPORT, ndi_mac_handle_port_vlan_subport_delete}
};

static auto _flush_type_str = new  std::map<int,const char *>
{
    {NDI_MAC_DEL_BY_PORT, "Port " },
    {NDI_MAC_DEL_BY_VLAN, "Vlan "},
    {NDI_MAC_DEL_BY_PORT_VLAN, "Port-Vlan"},
    {NDI_MAC_DEL_BY_BRIDGE, " 1D Bridge" },
    {NDI_MAC_DEL_BY_BRIDGE_ENDPOINT_IP, "Remote Endport IP" },
    {NDI_MAC_DEL_BY_PORT_VLAN_SUBPORT, "1D bridge subport" }
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

        if(entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1D_LOCAL ||
            entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1D_REMOTE){
            sai_mac_entry.bv_id = entry->bridge_id;
        }else{
            sai_object_id_t vlan_oid;
            if(!ndi_mac_get_vlan_oid(entry->vlan_id,vlan_oid)){
                return STD_ERR(MAC,FAIL,0);
            }
            sai_mac_entry.bv_id = vlan_oid;
        }
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
        auto flush_str_it  = _flush_type_str->find(delete_type);
        if(flush_str_it != _flush_type_str->end()){
            NDI_MAC_LOG(DEBUG,"%s based Flush request ",flush_str_it->second);
        }

        if(!it->second(entry,fdb_flush_attr,attr_idx)){
            NDI_MAC_LOG(ERR,"Failed to flush MAC entries for flush type %d " , delete_type);
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

static bool _fill_mac_entry_from_brport(ndi_mac_entry_t * mac_entry, sai_object_id_t oid){
    if(mac_entry->mac_entry_type ==NDI_MAC_ENTRY_TYPE_1D_REMOTE ){
        mac_entry->endpoint_ip_port = oid;
        return true;
    }

    if(_get_port_vlan_from_bridge_port(oid,mac_entry)){
        return true;
    }
    return false;
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

    if(mac_entry->mac_entry_type == NDI_MAC_ENTRY_TYPE_1Q){
        sai_object_id_t vlan_oid = 0;
        if(!ndi_mac_get_vlan_oid(mac_entry->vlan_id,vlan_oid)){
            return STD_ERR(MAC,PARAM,0);
        }
        sai_mac_entry.bv_id = vlan_oid;
    }else{
        sai_mac_entry.bv_id = mac_entry->bridge_id;
    }

    if ((sai_ret = ndi_mac_api_get(ndi_db_ptr)->get_fdb_entry_attribute
                    (&sai_mac_entry,sai_attr_ix,sai_attrs))!= SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, DEBUG, "NDI-MAC","Failed to fetch mac entry from NDI");
        return sai_to_ndi_err_translate(sai_ret);
    }

    sai_attr_ix = 0;


    if(!_fill_mac_entry_from_brport(mac_entry,sai_attrs[sai_attr_ix++].value.oid)){
        return STD_ERR(MAC,FAIL,0);
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
        return false;
    }

    size_t attr_idx = 0;
    sai_attribute_t fdb_flush_attr[2];

    memset(fdb_flush_attr, 0, sizeof(fdb_flush_attr));
    fdb_flush_attr[attr_idx].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
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
        return false;
    }

    return true;

}


void ndi_fdb_event_cb (uint32_t count,sai_fdb_event_notification_data_t *data)
{
    ndi_mac_entry_t ndi_mac_entry_temp;
    ndi_mac_event_type_t ndi_mac_event_type_temp;
    unsigned int attr_idx;
    unsigned int entry_idx;
    BASE_MAC_PACKET_ACTION_t action;
    bool is_lag_index = false;

    npu_id_t npu_id = ndi_npu_id_get();
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_MAC_LOG(ERR,"invalid npu_id 0x%" PRIx64 " ", npu_id);
        return;
    }

    if (data == NULL) {
        NDI_MAC_LOG(ERR,"Invalid parameters passed : notification data is NULL");
        return;
    }


    for (entry_idx = 0 ; entry_idx < count; entry_idx++) {
        memset(&ndi_mac_entry_temp,0,sizeof(ndi_mac_entry_temp));
        is_lag_index = false;
        if(data[entry_idx].attr == NULL) {
            NDI_MAC_LOG(ERR,"Invalid parameters passed : entry index: %d \
                    fdb_entry=%s, attr=%s, attr_count=%d.",entry_idx,
                    data[entry_idx].fdb_entry, data[entry_idx].attr,
                    data[entry_idx].attr_count);
            /*Ignore the entry. Continue with next entry*/
            continue;
        }
        /* Setting the default values */
        ndi_mac_entry_temp.is_static = false;
        ndi_mac_entry_temp.action =  BASE_MAC_PACKET_ACTION_FORWARD;
        sai_object_id_t brport_id = 0;
        bool _is_remote = false;

        for (attr_idx = 0; attr_idx < data[entry_idx].attr_count; attr_idx++) {
            switch (data[entry_idx].attr[attr_idx].id) {

                case SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID:
                    brport_id= data[entry_idx].attr[attr_idx].value.oid;
                    break;

                case SAI_FDB_ENTRY_ATTR_TYPE :
                    if ((data[entry_idx].attr[attr_idx].value.s32) ==  SAI_FDB_ENTRY_TYPE_STATIC)
                        ndi_mac_entry_temp.is_static = true;
                    else
                        ndi_mac_entry_temp.is_static = false;
                    break;

                case SAI_FDB_ENTRY_ATTR_PACKET_ACTION :
                    action = ndi_mac_packet_action_get((sai_packet_action_t)data[entry_idx].attr[attr_idx].value.s32);
                    ndi_mac_entry_temp.action = action;
                    break;

                case SAI_FDB_ENTRY_ATTR_ENDPOINT_IP:
                    if(data[entry_idx].attr[attr_idx].value.ipaddr.addr_family == SAI_IP_ADDR_FAMILY_IPV4){
                        ndi_mac_entry_temp.endpoint_ip.u.v4_addr = data[entry_idx].attr[attr_idx].
                                                                                  value.ipaddr.addr.ip4;
                        ndi_mac_entry_temp.endpoint_ip.af_index = HAL_INET4_FAMILY;
                    }else{
                        memcpy(ndi_mac_entry_temp.endpoint_ip.u.v6_addr,data[entry_idx].attr[attr_idx].
                                  value.ipaddr.addr.ip6,sizeof(ndi_mac_entry_temp.endpoint_ip.u.v6_addr));
                        ndi_mac_entry_temp.endpoint_ip.af_index = HAL_INET6_FAMILY;
                    }
                    _is_remote = true;
                    break;

                default:
                    NDI_MAC_LOG(ERR,"Invalid attr id : %d.", data[entry_idx].attr[attr_idx].id);
                    break;
            }
        }

        ndi_mac_event_type_temp = ndi_mac_event_type_get(data[entry_idx].event_type);
        ndi_virtual_obj_t obj;
        obj.oid = data[entry_idx].fdb_entry.bv_id;

        if(nas_ndi_get_virtual_obj(&obj,ndi_virtual_obj_query_type_FROM_OBJ)){
            ndi_mac_entry_temp.vlan_id = obj.vid;
            ndi_mac_entry_temp.mac_entry_type = NDI_MAC_ENTRY_TYPE_1Q;
        }else {
            ndi_mac_entry_temp.bridge_id = obj.oid;
            if(_is_remote){
                ndi_mac_entry_temp.mac_entry_type = NDI_MAC_ENTRY_TYPE_1D_REMOTE;
            }else{
                ndi_mac_entry_temp.mac_entry_type = NDI_MAC_ENTRY_TYPE_1D_LOCAL;
            }
        }

        if(!_fill_mac_entry_from_brport(&ndi_mac_entry_temp,brport_id)){
            NDI_MAC_LOG(ERR,"Failed to fill mac entry information from bridge port id");
            return;
        }
        memcpy(ndi_mac_entry_temp.mac_addr, data[entry_idx].fdb_entry.mac_address, HAL_MAC_ADDR_LEN);
        is_lag_index = ndi_mac_entry_temp.ndi_lag_id ? true : false;
        if (ndi_db_ptr->switch_notification->mac_event_notify_cb != NULL) {
            ndi_db_ptr->switch_notification->mac_event_notify_cb(npu_id, ndi_mac_event_type_temp,
                    &ndi_mac_entry_temp, is_lag_index);
        } else {
            return;
        }
    }
}
