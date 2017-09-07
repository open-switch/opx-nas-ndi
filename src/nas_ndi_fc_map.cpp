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
 * filename: nas_ndi_fc_map.c
 */

#include "std_error_codes.h"
#include "std_assert.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_fc.h"
#include "sai.h"
#include "saifcport.h"
#include "dell-base-if-fc.h"
#include "dell-interface.h"
#include "dell-base-platform-common.h"
#include "nas_ndi_port_utils.h"
#include "nas_ndi_fc_init.h"
#include "std_mac_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unordered_map>


typedef struct ndi_saiport_map_t {
    npu_id_t npu_id;
    port_t port;
} ndi_saiport_map_t;

struct _npu_port_t {
    uint_t npu_id;
    uint_t npu_port;

    bool operator== (const _npu_port_t& p) const {
        return npu_id == p.npu_id && npu_port == p.npu_port;
    }
};

struct _npu_port_hash_t {
    std::size_t operator() (const _npu_port_t& p) const {
        return std::hash<int>()(p.npu_id) ^ (std::hash<int>()(p.npu_port) << 1);
    }
};

typedef std::unordered_map<sai_object_id_t, ndi_saiport_map_t> sai_fcport_map_t;
using NasFcPortMap = std::unordered_map<_npu_port_t, sai_object_id_t, _npu_port_hash_t>;


static NasFcPortMap port_to_saifc_tbl;
static sai_fcport_map_t saifc_to_port_map;
static std_rw_lock_t _fc_port_lock;



enum nas_ndi_fc_attr_type {
    SW_ATTR_U16,
    SW_ATTR_U32,
    SW_ATTR_S32,
    SW_ATTR_U64,
    SW_ATTR_LST, /* uint 32 list */
    SW_ATTR_MAC,
    SW_ATTR_BOOL,
};

typedef std::unordered_map<uint32_t,uint32_t> _sai_attr_map;

static _sai_attr_map  mac_mode_map = {
    {SAI_FC_MAP_MAC_FPMA_MODE, BASE_IF_FC_FCMAC_MODE_FPMA_MODE } ,
    {SAI_FC_MAP_MAC_NULL_MODE, BASE_IF_FC_FCMAC_MODE_NULL_MODE },
    {SAI_FC_MAP_MAC_USR_MODE, BASE_IF_FC_FCMAC_MODE_USER_MODE },
};

static _sai_attr_map oper_state_map = {
    {SAI_FC_PORT_OPER_STATUS_UNKNOWN, IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_UNKNOWN },
    {SAI_FC_PORT_OPER_STATUS_UP, IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_UP },
    {SAI_FC_PORT_OPER_STATUS_DOWN, IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_DOWN },
};

static _sai_attr_map media_map = {
    { PLATFORM_MEDIA_TYPE_AR_POPTICS_NOTPRESENT, SAI_FC_PORT_MEDIA_TYPE_NOT_PRESENT},
    { PLATFORM_MEDIA_TYPE_AR_POPTICS_UNKNOWN, SAI_FC_PORT_MEDIA_TYPE_UNKNONWN},
    { PLATFORM_MEDIA_TYPE_SFPPLUS_8GBASE_FC_SW, SAI_FC_PORT_MEDIA_TYPE_SFP_FIBER},
    { PLATFORM_MEDIA_TYPE_SFPPLUS_8GBASE_FC_LW, SAI_FC_PORT_MEDIA_TYPE_SFP_FIBER},
    { PLATFORM_MEDIA_TYPE_AR_SFPPLUS_FC_8GBASE_SR, SAI_FC_PORT_MEDIA_TYPE_SFP_FIBER},
    { PLATFORM_MEDIA_TYPE_AR_SFPPLUS_FC_8GBASE_IR, SAI_FC_PORT_MEDIA_TYPE_SFP_FIBER},
    { PLATFORM_MEDIA_TYPE_AR_SFPPLUS_FC_8GBASE_MR, SAI_FC_PORT_MEDIA_TYPE_SFP_FIBER},
    { PLATFORM_MEDIA_TYPE_AR_SFPPLUS_FC_8GBASE_LR, SAI_FC_PORT_MEDIA_TYPE_SFP_FIBER},
    { PLATFORM_MEDIA_TYPE_SFPPLUS_16GBASE_FC_SW, SAI_FC_PORT_MEDIA_TYPE_SFP_FIBER},
    { PLATFORM_MEDIA_TYPE_SFPPLUS_16GBASE_FC_LW, SAI_FC_PORT_MEDIA_TYPE_SFP_FIBER},
    { PLATFORM_MEDIA_TYPE_QSFPPLUS_64GBASE_FC_SW4, SAI_FC_PORT_MEDIA_TYPE_QSFP_FIBER },
    { PLATFORM_MEDIA_TYPE_QSFPPLUS_4X16_16GBASE_FC_SW, SAI_FC_PORT_MEDIA_TYPE_QSFP_FIBER},
    { PLATFORM_MEDIA_TYPE_QSFPPLUS_64GBASE_FC_LW4, SAI_FC_PORT_MEDIA_TYPE_QSFP_FIBER},
    { PLATFORM_MEDIA_TYPE_QSFPPLUS_4X16_16GBASE_FC_LW, SAI_FC_PORT_MEDIA_TYPE_QSFP_FIBER},
    { PLATFORM_MEDIA_TYPE_QSFP28_128GBASE_FC_SW4, SAI_FC_PORT_MEDIA_TYPE_QSFP28_FIBER},
    { PLATFORM_MEDIA_TYPE_QSFP28_4X32_32GBASE_FC_SW, SAI_FC_PORT_MEDIA_TYPE_QSFP28_FIBER},
    { PLATFORM_MEDIA_TYPE_QSFP28_128GBASE_FC_LW4, SAI_FC_PORT_MEDIA_TYPE_QSFP28_FIBER},
    { PLATFORM_MEDIA_TYPE_QSFP28_4X32_32GBASE_FC_LW, SAI_FC_PORT_MEDIA_TYPE_QSFP28_FIBER},
    { PLATFORM_MEDIA_TYPE_SFP28_32GBASE_FC_SW, SAI_FC_PORT_MEDIA_TYPE_QSFP28_FIBER},
    { PLATFORM_MEDIA_TYPE_SFP28_32GBASE_FC_LW, SAI_FC_PORT_MEDIA_TYPE_QSFP28_FIBER}
};


static bool frm_sai_enum(_sai_attr_map &map, sai_attribute_t *param) {
    auto it = map.find(param->value.u32);
    if (it==map.end()) return false;
    NDI_PORT_LOG_TRACE("sai value %u, base value %u\n", param->value.u32, it->second);
    param->value.u32 = it->second;
    return true;
}

static bool to_sai_enum(_sai_attr_map &map, sai_attribute_t *param) {
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it->second == param->value.u32) {
            param->value.u32 = it->first;
            NDI_PORT_LOG_TRACE("sai value %u, base value %u\n", param->value.u32, it->second);
            return true;
        }
   }
   return false;

}

static bool frm_sai_oper_mode(sai_attribute_t *param) {

    NDI_PORT_LOG_TRACE("frm_sai_oper_mode \n");
    return frm_sai_enum(oper_state_map, param);
}

static bool to_sai_oper_mode(sai_attribute_t *param) {

    NDI_PORT_LOG_TRACE("to_sai_oper_mode \n ");
    return to_sai_enum(oper_state_map, param);
}

static bool frm_sai_mac_mode(sai_attribute_t *param) {
    NDI_PORT_LOG_TRACE("frm_sai_mac_mode \n ");
    return frm_sai_enum(mac_mode_map, param);
}

static bool to_sai_mac_mode(sai_attribute_t *param) {
    NDI_PORT_LOG_TRACE("to_sai_mac_mode \n ");
    return to_sai_enum(mac_mode_map, param);
}

struct _sai_fc_op_table {
    nas_ndi_fc_attr_type type;
    bool (*to_sai_type)( sai_attribute_t *param );
    bool (*from_sai_type)(sai_attribute_t *param );
    sai_attr_id_t id;
};

static bool to_sai_speed(sai_attribute_t *param) {
    uint32_t sai_speed;

    if (ndi_port_get_sai_speed((BASE_IF_SPEED_t)param->value.u32, &sai_speed)) {
        NDI_PORT_LOG_TRACE("(BASE_IF_SPEED_t) %u, sai_speed %u \n", param->value.u32, sai_speed);
        param->value.u32 = sai_speed;
        return true;
    }
    return false;
}

static bool frm_sai_supp_speed(sai_attribute_t *param) {
    BASE_IF_SPEED_t ndi_speed;
    size_t cnt = 0;
    NDI_PORT_LOG_TRACE("frm_sai_supp_speed count is %d \n", param->value.u32list.count);
    while (cnt < param->value.u32list.count) {
       if (ndi_port_get_ndi_speed(param->value.u32list.list[cnt], &ndi_speed)) {
           NDI_PORT_LOG_TRACE("(BASE_IF_SPEED_t) %u, sai_speed %u \n", ndi_speed, param->value.u32list.list[cnt]);
           param->value.u32list.list[cnt] = ndi_speed;
       } else {
           NDI_PORT_LOG_ERROR(" Could get ndi_speed for sai speed %u \n", param->value.u32list.list[cnt]);
           return false;
       }
       cnt++;
    }
    return true;

}
static bool frm_sai_speed(sai_attribute_t *param) {
    BASE_IF_SPEED_t ndi_speed;

    if (ndi_port_get_ndi_speed(param->value.u32, &ndi_speed)) {
        NDI_PORT_LOG_TRACE("(BASE_IF_SPEED_t) %u, sai_speed %u \n", ndi_speed, param->value.u32);
        param->value.u32 = ndi_speed;
        return true;
    }
    return false;

}

#define SPEED_1MBPS ((uint64_t)(1000*1000))
static bool frm_sai_ietf_speed(sai_attribute_t *param) {

    /*  SAI speed is in 1MBPS unit. SPEED defined in the ietf yang model is bps unit */
    uint64_t ietf_speed = (uint64_t) param->value.u32*SPEED_1MBPS;
    param->value.u64 = ietf_speed;
    return true;

}
static bool to_sai_media(sai_attribute_t *param) {
    NDI_PORT_LOG_TRACE("to_sai_media \n ");
    return frm_sai_enum(media_map, param);

}

static bool frm_sai_media(sai_attribute_t *param) {
    NDI_PORT_LOG_TRACE("frm_sai_media \n");
    return to_sai_enum(media_map, param);

}

static std::unordered_map<uint64_t,_sai_fc_op_table> _attr_to_op = {
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_SRC_MAC_MODE, {
            SW_ATTR_U32, to_sai_mac_mode, frm_sai_mac_mode, SAI_FC_PORT_ATTR_MAP_SRC_MAC_MODE }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_SRC_MAP_PREFIX, {
            SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_SRC_MAP_PREFIX }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_INGRESS_SRC_MAC, {
            SW_ATTR_MAC, NULL, NULL, SAI_FC_PORT_ATTR_INGRESS_SRC_MAC }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_DEST_MAC_MODE, {
            SW_ATTR_U32, to_sai_mac_mode, frm_sai_mac_mode, SAI_FC_PORT_ATTR_DST_MAC_MODE }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_DEST_MAP_PREFIX, {
            SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_DST_MAP_PREFIX }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_INGRESS_DEST_MAC, {
            SW_ATTR_MAC, NULL, NULL, SAI_FC_PORT_ATTR_INGRESS_DST_MAC }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_VFT_HEADER, {
            SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_DEFAULT_VFT }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_BB_CREDIT, {
            SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_BB_CREDIT }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_BB_CREDIT_RECOVERY, {
            SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_BB_CREDIT_RECOVERY }},
        { DELL_IF_IF_INTERFACES_INTERFACE_AUTO_NEGOTIATION,  {
           SW_ATTR_BOOL, NULL, NULL, SAI_FC_PORT_ATTR_AUTO_NEG_MODE}},
        { IF_INTERFACES_INTERFACE_ENABLED, {
           SW_ATTR_BOOL, NULL, NULL, SAI_FC_PORT_ATTR_ADMIN_STATE}},
        { IF_INTERFACES_STATE_INTERFACE_OPER_STATUS, {
           SW_ATTR_U32, to_sai_oper_mode , frm_sai_oper_mode, SAI_FC_PORT_ATTR_OPER_STATUS }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_FCOE_PKT_VLANID, {
           SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_VLAN_ID }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_PRIORITY, {
           SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_PCP }},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_MTU, {
           SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_MAX_FRAME_SIZE}},
        { DELL_IF_IF_INTERFACES_INTERFACE_SPEED, {
           SW_ATTR_U32, to_sai_speed, frm_sai_speed, SAI_FC_PORT_ATTR_SPEED}},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_SUPPORTED_SPEED , {
           SW_ATTR_LST, NULL, frm_sai_supp_speed, SAI_FC_PORT_ATTR_SUPPORTED_SPEED}},
        { BASE_IF_PHY_PHYSICAL_HARDWARE_PORT_LIST, {
           SW_ATTR_LST, NULL, NULL, SAI_FC_PORT_ATTR_HW_LANE_LIST }},
        { IF_INTERFACES_STATE_INTERFACE_SPEED, {             /* Read only never set */
           SW_ATTR_U64, NULL, frm_sai_ietf_speed, SAI_FC_PORT_ATTR_SPEED }},
        { BASE_IF_PHY_IF_INTERFACES_INTERFACE_PHY_MEDIA, {
           SW_ATTR_U32, to_sai_media, frm_sai_media, SAI_FC_PORT_ATTR_MEDIA_TYPE }},
        { BASE_IF_FC_IF_INTERFACES_STATE_INTERFACE_BB_CREDIT_RECEIVE, {
            SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_BB_CREDIT_RX}},
        { BASE_IF_FC_IF_INTERFACES_INTERFACE_FLOW_CONTROL_ENABLE, {
           SW_ATTR_BOOL, NULL, NULL, SAI_FC_PORT_ATTR_FLOW_CONTROL_ENABLE}},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_FC_MTU, {
           SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_MAX_FRAME_SIZE }},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_BB_CREDIT, {
           SW_ATTR_U32, NULL, NULL, SAI_FC_PORT_ATTR_BB_CREDIT }}

};


void nas_fc_lock_init() {
    std_rw_lock_create_default(&_fc_port_lock);
}


t_std_error ndi_sai_fcport_id_get (npu_id_t npu_id, port_t port, sai_object_id_t *sai_port) {


    _npu_port_t npu_port = {(uint_t)npu_id, (uint_t)port};
    std_rw_lock_read_guard g(&_fc_port_lock);
    auto it = port_to_saifc_tbl.find(npu_port);
    if (it != port_to_saifc_tbl.end()) {
        *(sai_port) = it->second;
        NDI_PORT_LOG_TRACE("SAI FC  to npu port mapping exist npu %d , port %u sai_port 0x%" PRIx64 " ", npu_id, port, *sai_port);
        return STD_ERR_OK;
    }
    NDI_PORT_LOG_ERROR("SAI FC to npu port mapping does't exist npu %u, port %u \n", npu_id, port);
    return STD_ERR(NPU,FAIL,0);

}

t_std_error ndi_sai_fcport_id_add (npu_id_t npu_id, port_t port, sai_object_id_t sai_port) {


    _npu_port_t npu_port = {(uint_t)npu_id, (uint_t)port};

    std_rw_lock_write_guard g(&_fc_port_lock);
    auto it = port_to_saifc_tbl.find(npu_port);
    if (it == port_to_saifc_tbl.end()) {
        port_to_saifc_tbl[npu_port] = sai_port;;
        NDI_PORT_LOG_TRACE("Add SAI FC to npu port mapping: npu %u, port %u port_to_saifc_tbl[npu_port] %lu \n", npu_id, port, port_to_saifc_tbl[npu_port]);
        return STD_ERR_OK;
    }
    NDI_PORT_LOG_ERROR("In ADD: SAI FC to npu port mapping exist npu %u, port %u  sai_port 0x%" PRIx64 " ", npu_id, port, sai_port);
    return STD_ERR(NPU,FAIL,0);

}

t_std_error ndi_sai_fcport_id_del(npu_id_t npu_id, port_t port, sai_object_id_t sai_port) {


    _npu_port_t npu_port = {(uint_t)npu_id, (uint_t)port};
    std_rw_lock_write_guard g(&_fc_port_lock);
    auto it = port_to_saifc_tbl.find(npu_port);
    if (it != port_to_saifc_tbl.end()) {
        port_to_saifc_tbl.erase(it);
        NDI_PORT_LOG_TRACE("Delete SAI FC to npu port mapping: npu %u, port %u  it->second 0x%" PRIx64 " ", npu_id, port, it->second);
        return STD_ERR_OK;
    } else {
        NDI_PORT_LOG_ERROR("In delete: SAI FC to npu port mapping doesn't exist: npu %u, port %u  sai_port 0x%" PRIx64 " ",npu_id, port, sai_port);
        return STD_ERR(NPU,FAIL,0);
    }

}

t_std_error ndi_create_fc_port (npu_id_t npu_id, port_t port, BASE_IF_SPEED_t speed, uint32_t *hw_port_list, size_t count )
{

    sai_object_id_t               sai_port = 0;
    t_std_error                   ret_code;
    if ((ret_code = ndi_sai_fcport_id_get(npu_id, port, &sai_port) == STD_ERR_OK)) {
         NDI_PORT_LOG_ERROR("Create FC port elready exist npu %d, port %u \n",
                            npu_id, port);
         return STD_ERR_OK;
    }
    sai_attribute_t               sai_entry_attr = {0}, nil_attr = {0};
    std::vector<sai_attribute_t>  sai_entry_attr_list;
    std::vector <int32_t> hw_lst; /* may hv to copy here */

    sai_entry_attr_list.reserve(3);
    ndi_port_get_sai_speed(speed, &sai_entry_attr.value.u32);
    sai_entry_attr.id = SAI_FC_PORT_ATTR_SPEED;
    sai_entry_attr_list.push_back (sai_entry_attr);

    sai_entry_attr = nil_attr;
    sai_entry_attr.id = SAI_FC_PORT_ATTR_HW_LANE_LIST;
    sai_entry_attr.value.u32list.count = count;
    sai_entry_attr.value.u32list.list  = hw_port_list;
    sai_entry_attr_list.push_back (sai_entry_attr);

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    if ((sai_ret = ndi_get_fc_port_api()->fc_port_create_fn(&sai_port, sai_entry_attr_list.size(),
                         sai_entry_attr_list.data()))  != SAI_STATUS_SUCCESS) {
         NDI_PORT_LOG_ERROR("Create FC port : SAI create failed for port %u speed %u \n", port, speed);
         return STD_ERR(NPU, PARAM, sai_ret);
    }

    ndi_sai_fcport_id_add (npu_id, port, sai_port);
    return STD_ERR_OK;
}

t_std_error ndi_delete_fc_port(npu_id_t npu_id, port_t port)
{
    t_std_error                   ret_code;

    sai_object_id_t               sai_port = 0;
    if ((ret_code = ndi_sai_fcport_id_get(npu_id, port, &sai_port) != STD_ERR_OK)) {
         NDI_PORT_LOG_ERROR("FC port to be delete  doestn't  exist npu %d, port %u \n",
                            npu_id, port);
         return ret_code;
    }
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    if ((sai_ret = ndi_get_fc_port_api()->fc_port_remove_fn(sai_port)) != SAI_STATUS_SUCCESS) {
         NDI_PORT_LOG_ERROR("Delete FC port : SAI delete failed for port %u \n", port);
         return STD_ERR(NPU, PARAM, sai_ret);
    }
    ndi_sai_fcport_id_del(npu_id, port, sai_port);
    return STD_ERR_OK;
}


t_std_error ndi_set_fc_attr(npu_id_t npu_id, port_t port, nas_fc_param_t *param, uint64_t attr)
{

    t_std_error                   ret_code;
    sai_object_id_t               sai_port = 0;
    char mac_str[18] ={0};

    NDI_PORT_LOG_TRACE("Values  set for npu port %u attribute % " PRIx64 " ", port, attr);
    if ((ret_code = ndi_sai_fcport_id_get(npu_id, port, &sai_port) != STD_ERR_OK)) {
         NDI_PORT_LOG_ERROR("FC port to be set  doestn't  exist npu %d, port %u \n",
                            npu_id, port);
         return ret_code;
    }
    auto it = _attr_to_op.find(attr);
    if (it==_attr_to_op.end()) {
        NDI_PORT_LOG_ERROR("Invalid operation type in set FC attr %"  PRIu64 " ",attr);
        return STD_ERR(NPU,FAIL,0);
    }
    sai_attribute_t sai_attr;
    sai_attr.id = it->second.id;
    std::vector<uint32_t> tmp_lst;

    switch(it->second.type) {
    case SW_ATTR_S32:
        sai_attr.value.s32 = param->s32;
        NDI_PORT_LOG_TRACE("FC port to be set S32 val %d \n", sai_attr.value.s32);
        break;
    case SW_ATTR_U32:
        sai_attr.value.u32 = param->u32;
        NDI_PORT_LOG_TRACE("FC port to be set u32 val %u \n", sai_attr.value.u32);
        break;
    case SW_ATTR_U64:
        sai_attr.value.u64 = param->u64;
        NDI_PORT_LOG_TRACE("FC port to be set u64 val %u \n", sai_attr.value.u64);
        break;
    case SW_ATTR_LST:
        /* There is no param in set which is this */
        sai_attr.value.u32list.count = param->list.len;
        tmp_lst.resize(param->list.len);
        memcpy(&tmp_lst[0],param->list.vals,param->list.len*sizeof(tmp_lst[0]));
        sai_attr.value.u32list.list = &tmp_lst[0];
        NDI_PORT_LOG_TRACE("FC port to be set LIST size %d \n", sai_attr.value.u32list.count);
        break;
    case SW_ATTR_U16:
        sai_attr.value.u16 = param->u16;
        NDI_PORT_LOG_TRACE("FC port to be set u16 val %u \n", sai_attr.value.u16);
        break;
    case SW_ATTR_MAC:
        memcpy(sai_attr.value.mac, param->mac, sizeof(sai_attr.value.mac));
        NDI_PORT_LOG_TRACE("FC port to be set mac %s \n", std_mac_to_string(&sai_attr.value.mac, mac_str,sizeof(mac_str)));
        break;
    case SW_ATTR_BOOL:
        sai_attr.value.booldata = param->ty_bool;
        NDI_PORT_LOG_TRACE("FC port to be set BOOL %d \n", sai_attr.value.booldata);
        break;
    }
    if (it->second.to_sai_type!=NULL) {
        if (!(it->second.to_sai_type)(&sai_attr)) {
            NDI_PORT_LOG_ERROR("FC set attr values are invalid - can't be converted to SAI types % " PRIu64 " ",attr);
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((ret_code = ndi_sai_fcport_id_get(npu_id, port, &sai_port) != STD_ERR_OK)) {
         NDI_PORT_LOG_ERROR("FC port to be set  doestn't  exist npu %d, port %u \n",
                            npu_id, port);
         return ret_code;
    }
    NDI_PORT_LOG_TRACE("Values  set for sai_port % " PRIx64 " ",sai_port);

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    if ((sai_ret = ndi_get_fc_port_api()-> set_fc_port_attribute(sai_port, &sai_attr))
            != SAI_STATUS_SUCCESS) {
         NDI_PORT_LOG_ERROR("FC Set attr : SAI failed in set FC attr %"  PRIu64 " ",attr);
         return STD_ERR(NPU, PARAM, sai_ret);
    }

    return STD_ERR_OK;
}

/* Will need a name value pair if multiple attribute can be returned */
t_std_error ndi_get_fc_attr(npu_id_t npu_id, port_t port, nas_fc_id_value_t *array, int count)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);
    sai_object_id_t               sai_port = 0;
    t_std_error                   ret_code;
    char mac_str[18] ={0};

    if ((ret_code = ndi_sai_fcport_id_get(npu_id, port, &sai_port) != STD_ERR_OK)) {
         NDI_PORT_LOG_ERROR("In get FC port attr port doestn't  exist npu %d, port %u \n",
                            npu_id, port);
         return ret_code;
    }

    sai_attribute_t               sai_attr = {0};
    auto it = _attr_to_op.find(array->attr_id);
    if (it==_attr_to_op.end()) {
        NDI_PORT_LOG_ERROR("Invalid operation type for NDI FC attr % " PRIx64  " ",array->attr_id);
        return STD_ERR(NPU,FAIL,0);
    }
    sai_attr.id = it->second.id;
    NDI_PORT_LOG_TRACE("Get FC attr called for sai attr %u sai_port 0x% "  PRIx64 " ",sai_attr.id, sai_port);
    switch(it->second.type) {
    case SW_ATTR_LST:
        sai_attr.value.u32list.count = array->value.list.len;;
        sai_attr.value.u32list.list = array->value.list.vals;
        break;
    default:
        break;
    }

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    if ((sai_ret = ndi_get_fc_port_api()-> get_fc_port_attribute(sai_port, 1, &sai_attr))
        != SAI_STATUS_SUCCESS)    {
        NDI_PORT_LOG_ERROR("FC get attr: SAI get failed for attr %"  PRIu64 " ",array->attr_id);
        return STD_ERR(NPU, PARAM, sai_ret);
    }

    if (it->second.from_sai_type!=NULL) {
        if (!(it->second.from_sai_type)(&sai_attr)) {
            NDI_PORT_LOG_ERROR("FC get attr values are invalid - can't be converted from SAI types (func:%d)",sai_attr.id);
            return STD_ERR(NPU,PARAM,0);
        }
    }

    switch(it->second.type) {
    case SW_ATTR_S32:
        array->value.s32 = sai_attr.value.s32;
        NDI_PORT_LOG_TRACE("FC port get S32 val %d id %" PRIx64 " ", array->value.s32, array->attr_id);
        break;
    case SW_ATTR_U32:
        array->value.u32 = sai_attr.value.u32;
        NDI_PORT_LOG_TRACE("FC port get u32 val %u \n", array->value.u32);
        break;
    case SW_ATTR_U64:
        array->value.u64 = sai_attr.value.u64;
        NDI_PORT_LOG_TRACE("FC port get u64 val %u \n", array->value.u64);
        break;
    case SW_ATTR_LST:
        array->value.list.len = sai_attr.value.u32list.count ;
        NDI_PORT_LOG_TRACE("FC GET port list len  %d \n", array->value.list.len);
        break;
    case SW_ATTR_U16:
        array->value.u16 = sai_attr.value.u16;
        NDI_PORT_LOG_TRACE("FC port get u16 val %u \n", array->value.u16);
        break;
    case SW_ATTR_MAC:
        memcpy(array->value.mac, sai_attr.value.mac,sizeof(array->value.mac));
        NDI_PORT_LOG_TRACE("FC port get mac %s \n", std_mac_to_string(&array->value.mac, mac_str,sizeof(mac_str)));
        break;
    case SW_ATTR_BOOL:
        array->value.ty_bool = sai_attr.value.booldata;
        NDI_PORT_LOG_TRACE("FC port get BOOL %d \n", array->value.ty_bool);
        break;
    }

    return STD_ERR_OK;
}


