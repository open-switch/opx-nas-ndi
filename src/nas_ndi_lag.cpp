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
 * filename: nas_ndi_lag.c
 */


#include "std_error_codes.h"
#include "std_assert.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_lag.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_bridge_port.h"
#include "sai.h"
#include "sailag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <inttypes.h>
#include <unordered_map>

//@TODO Get this max_lag_port from platform Jira AR-710
#define MAX_LAG_PORTS 32
/*  LAG Member OID to SAI Port OID mapping  */
static auto lag_member_to_port_map = new std::unordered_map<sai_object_id_t,sai_object_id_t>;

extern "C" {

static inline  sai_lag_api_t *ndi_sai_lag_api(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_lag_api_tbl);
}

t_std_error ndi_create_lag(npu_id_t npu_id,ndi_obj_id_t *ndi_lag_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_local_lag_id ;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    if((sai_ret = ndi_sai_lag_api(ndi_db_ptr)->create_lag(&sai_local_lag_id,
                                                          ndi_switch_id_get(),
                                                          0,NULL))
            != SAI_STATUS_SUCCESS) {
        NDI_LAG_LOG_ERROR("SAI_LAG_CREATE Failure");
        return STD_ERR(INTERFACE, CFG, sai_ret);
    }

    NDI_LAG_LOG_INFO("Create LAG Group Id %lu",sai_local_lag_id);
    *ndi_lag_id = sai_local_lag_id;

    if (nas_ndi_create_bridge_port_1Q(npu_id,sai_local_lag_id,true) != STD_ERR_OK) {
        return STD_ERR(INTERFACE, CFG,0);
    }
    return STD_ERR_OK;
}

t_std_error ndi_delete_lag(npu_id_t npu_id, ndi_obj_id_t ndi_lag_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    if (nas_ndi_delete_bridge_port_1Q(npu_id, ndi_lag_id) != STD_ERR_OK) {
        return STD_ERR(INTERFACE, CFG,0);
    }
    NDI_LAG_LOG_INFO("Delete LAG Group  %lu",ndi_lag_id);
    if ((sai_ret = ndi_sai_lag_api(ndi_db_ptr)->remove_lag((sai_object_id_t) ndi_lag_id))
            != SAI_STATUS_SUCCESS) {
        NDI_LAG_LOG_ERROR("SAI_LAG_Delete Failure");
        return STD_ERR(INTERFACE, CFG, sai_ret);
    }

    return STD_ERR_OK;
}



t_std_error ndi_add_ports_to_lag(npu_id_t npu_id, ndi_obj_id_t ndi_lag_id,
        ndi_port_list_t *lag_port_list,ndi_obj_id_t *ndi_lag_member_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t sai_lag_attr_list[4];
    sai_object_id_t  sai_port;
    ndi_port_t *ndi_port = NULL;
    unsigned int count = 0;

    NDI_LAG_LOG_INFO("Add ports to Lag ID  %lu",ndi_lag_id);

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    memset(sai_lag_attr_list,0, sizeof(sai_lag_attr_list));
    sai_lag_attr_list [count].id = SAI_LAG_MEMBER_ATTR_LAG_ID;
    sai_lag_attr_list [count].value.oid = ndi_lag_id;
    count++;


    ndi_port = &(lag_port_list->port_list[0]);
    if(ndi_sai_port_id_get(ndi_port->npu_id,ndi_port->npu_port,&sai_port) != STD_ERR_OK) {
        NDI_LAG_LOG_ERROR("Failed to convert  npu %d and port %d to sai port",
                ndi_port->npu_id, ndi_port->npu_port);
        return STD_ERR(INTERFACE, CFG, 0);
    }


    sai_lag_attr_list [count].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    sai_lag_attr_list [count].value.oid = sai_port;
    count++;


    /* Delete the member from  1Q */
    if (nas_ndi_delete_bridge_port_1Q(npu_id, sai_port) != STD_ERR_OK) {
        return STD_ERR(INTERFACE, CFG, 0);
    }

    if((sai_ret = ndi_sai_lag_api(ndi_db_ptr)->create_lag_member(ndi_lag_member_id,
                    ndi_switch_id_get(), count,
                    sai_lag_attr_list)) != SAI_STATUS_SUCCESS) {
        NDI_LAG_LOG_ERROR("Add ports to LAG Group Failure");
        return STD_ERR(INTERFACE, CFG, sai_ret);
    }
    lag_member_to_port_map->insert({*ndi_lag_member_id,  sai_port});

    return STD_ERR_OK;
}



t_std_error ndi_del_ports_from_lag(npu_id_t npu_id,ndi_obj_id_t ndi_lag_member_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    NDI_LAG_LOG_INFO("Deletting lag member id %lu",ndi_lag_member_id);

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    if((sai_ret = ndi_sai_lag_api(ndi_db_ptr)->remove_lag_member(ndi_lag_member_id))
            != SAI_STATUS_SUCCESS) {
        NDI_LAG_LOG_ERROR("Delete ports from LAG Group Failure");
        return STD_ERR(INTERFACE, CFG, sai_ret);
    }

    auto it = lag_member_to_port_map->find(ndi_lag_member_id);
    if (it == lag_member_to_port_map->end()) {
        NDI_LAG_LOG_ERROR(" Bridgeport creation failure: Can't find the corresponding SAI port");
        return STD_ERR(INTERFACE, CFG, sai_ret);

    }
    sai_object_id_t sai_port = it->second;
    lag_member_to_port_map->erase(ndi_lag_member_id);
    /* Add the deleted lag member as a normal PORT type to .1Q */
    if (nas_ndi_create_bridge_port_1Q(npu_id, sai_port, false) != STD_ERR_OK) {
        return STD_ERR(INTERFACE, CFG,0);
    }
    return STD_ERR_OK;
}


t_std_error ndi_set_lag_port_mode (npu_id_t npu_id,ndi_obj_id_t ndi_lag_member_id,
                                  bool egr_disable)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    NDI_LAG_LOG_INFO("Set port mode in NPU %d lag member id %lu egr_disable %d",
            npu_id,ndi_lag_member_id,egr_disable);

    sai_attribute_t sai_lag_member_attr;
    memset (&sai_lag_member_attr, 0, sizeof (sai_lag_member_attr));

    sai_lag_member_attr.id = SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE;
    sai_lag_member_attr.value.booldata = egr_disable;


    if((sai_ret = ndi_sai_lag_api(ndi_db_ptr)->set_lag_member_attribute(ndi_lag_member_id,
                    &sai_lag_member_attr)) != SAI_STATUS_SUCCESS) {
        NDI_LAG_LOG_ERROR("Lag port mode set Failure");
        return STD_ERR(INTERFACE, CFG, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_set_lag_member_attr(npu_id_t npu_id, ndi_obj_id_t ndi_lag_member_id,
        bool egress_disable)
{

    if(ndi_set_lag_port_mode (npu_id,ndi_lag_member_id,egress_disable) != STD_ERR_OK){
        NDI_LAG_LOG_ERROR("Lag port block/unblock mode set Failure");
        return (STD_ERR(INTERFACE,CFG,0));
    }
    return STD_ERR_OK;
}

t_std_error ndi_get_lag_port_mode (npu_id_t npu_id,ndi_obj_id_t ndi_lag_member_id,
                                   bool *egr_disable)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    NDI_LAG_LOG_INFO("Get port mode in NPU %d lag member id %lu",
            npu_id,ndi_lag_member_id);

    sai_attribute_t sai_lag_member_attr;
    memset (&sai_lag_member_attr, 0, sizeof (sai_lag_member_attr));

    sai_lag_member_attr.id = SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE;

    if((sai_ret = ndi_sai_lag_api(ndi_db_ptr)->get_lag_member_attribute(ndi_lag_member_id,
                    1, &sai_lag_member_attr)) != SAI_STATUS_SUCCESS) {
        NDI_LAG_LOG_ERROR("Lag port mode get Failure");
        return STD_ERR(INTERFACE, CFG, sai_ret);
    }

    if (egr_disable) {
        *egr_disable = sai_lag_member_attr.value.booldata;
    }

    return STD_ERR_OK;
}

t_std_error ndi_get_lag_member_attr(npu_id_t npu_id, ndi_obj_id_t ndi_lag_member_id,
        bool* egress_disable)
{

    if(ndi_get_lag_port_mode (npu_id, ndi_lag_member_id, egress_disable) != STD_ERR_OK){
        NDI_LAG_LOG_ERROR("Lag port block/unblock mode get Failure");
        return (STD_ERR(INTERFACE,CFG,0));
    }
    return STD_ERR_OK;
}

t_std_error ndi_set_lag_pvid(npu_id_t npu_id, ndi_obj_id_t ndi_lag_id,
        hal_vlan_id_t vlan_id)
{

    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    NDI_LAG_LOG_INFO("Set lag pvid in NPU %d lag id %lu",
            npu_id,ndi_lag_id);

    sai_attribute_t sai_lag_attr;
    memset (&sai_lag_attr, 0, sizeof (sai_attribute_t));

    sai_lag_attr.id = SAI_LAG_ATTR_PORT_VLAN_ID;
    sai_lag_attr.value.u16 = (sai_vlan_id_t)vlan_id;

    if((sai_ret = ndi_sai_lag_api(ndi_db_ptr)->set_lag_attribute(ndi_lag_id,
                    &sai_lag_attr)) != SAI_STATUS_SUCCESS) {
        NDI_LAG_LOG_ERROR("Lag PVID set Failure %d, lag id %lu ", vlan_id, ndi_lag_id);
        return STD_ERR(INTERFACE, CFG, sai_ret);
    }

    return STD_ERR_OK;
}


static sai_bridge_port_fdb_learning_mode_t ndi_lag_get_sai_mac_learn_mode
                             (BASE_IF_MAC_LEARN_MODE_t ndi_fdb_learn_mode){

    static const auto ndi_to_sai_fdb_learn_mode = new std::unordered_map<BASE_IF_MAC_LEARN_MODE_t,
                                                            sai_bridge_port_fdb_learning_mode_t,std::hash<int>>
    {
        {BASE_IF_MAC_LEARN_MODE_DROP, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DROP},
        {BASE_IF_MAC_LEARN_MODE_DISABLE, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DISABLE},
        {BASE_IF_MAC_LEARN_MODE_HW, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW},
        {BASE_IF_MAC_LEARN_MODE_CPU_TRAP, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_TRAP},
        {BASE_IF_MAC_LEARN_MODE_CPU_LOG, SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_LOG},
    };

    sai_bridge_port_fdb_learning_mode_t mode;

    auto it = ndi_to_sai_fdb_learn_mode->find(ndi_fdb_learn_mode);

    if(it != ndi_to_sai_fdb_learn_mode->end()){
        mode = it->second;
    } else {
        NDI_LAG_LOG_ERROR("Invalid ndi learn mode %d , setting to HW ", ndi_fdb_learn_mode);
        mode = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW;
    }
    return mode;
}

t_std_error ndi_set_lag_learn_mode(npu_id_t npu_id, ndi_obj_id_t ndi_lag_id,
          BASE_IF_MAC_LEARN_MODE_t mode)
{
    sai_attribute_t sai_attr;

    NDI_LAG_LOG_INFO("Set lag learn mode to %d lag id %lu",
            mode, ndi_lag_id);

    sai_attr.value.u32 = (sai_bridge_port_fdb_learning_mode_t )ndi_lag_get_sai_mac_learn_mode(mode);
    sai_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;

    return ndi_brport_attr_set_or_get_1Q(npu_id, ndi_lag_id, true, &sai_attr);

}

t_std_error ndi_lag_set_packet_drop(npu_id_t npu_id, ndi_obj_id_t ndi_lag_id,
                                     ndi_port_drop_mode_t mode, bool enable)
{
    sai_attribute_t drop_mode_attr;
    if (mode == NDI_PORT_DROP_UNTAGGED) {
        drop_mode_attr.id = SAI_LAG_ATTR_DROP_UNTAGGED;
    } else if (mode == NDI_PORT_DROP_TAGGED) {
        drop_mode_attr.id = SAI_LAG_ATTR_DROP_TAGGED;
    } else {
        NDI_LAG_LOG_ERROR("Unknown tag untag packet drop mode %d", mode);
        return STD_ERR(INTERFACE, PARAM, 0);
    }

    drop_mode_attr.value.booldata = enable;

    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(INTERFACE, CFG,0);
    }

    if((sai_ret = ndi_sai_lag_api(ndi_db_ptr)->set_lag_attribute(ndi_lag_id,&drop_mode_attr))
            != SAI_STATUS_SUCCESS) {
        NDI_LAG_LOG_ERROR("Sai failure to set tag-untag drop mode %d to %d  on lagid %lu",
                                        mode, enable, ndi_lag_id);
        return STD_ERR(INTERFACE, CFG, sai_ret);
    }

    NDI_LAG_LOG_INFO("Set Tag /untag drop: mode %d , enable %d,  NDI lag id %lu",
                                        mode, enable, ndi_lag_id);
    return STD_ERR_OK;
}
}






