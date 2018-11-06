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

#ifndef NAS_NDI_INC_NAS_NDI_OBJ_CACHE_H_
#define NAS_NDI_INC_NAS_NDI_OBJ_CACHE_H_

#include "saitypes.h"
#include "nas_ndi_common.h"


/* Bridge port type */
typedef enum{
    ndi_brport_type_PORT=1,
    ndi_brport_type_SUBPORT_TAG,
    ndi_brport_type_SUBPORT_UNTAG,
    ndi_brport_type_TUNNEL,
    ndi_brport_type_1D_ROUTER,
    ndi_brport_type_1Q_ROUTER,
} ndi_brport_type_t;


/* Bridge port slave type when creating vlan/stp member port */
typedef enum{
    ndi_brport_slave_type_VLAN=1,
    ndi_brport_slave_type_STP
} ndi_brport_slave_type_t;


/* virtual obj query type when querying virtual obj information */
typedef enum{
    ndi_virtual_obj_query_type_FROM_OBJ=1,
    ndi_virtual_obj_query_type_FROM_VLAN
}ndi_virtual_obj_query_type_t;


/* bridge port query type when querying the bridge port information */
typedef enum{
    ndi_brport_query_type_FROM_PORT=1,
    ndi_brport_query_type_FROM_BRPORT,
    ndi_brport_query_type_FROM_PORT_VLAN,
}ndi_brport_query_type_t;


/* to maintain vlan_object id id to vlan id */
typedef struct{
    sai_object_id_t oid;
    hal_vlan_id_t vid;
} ndi_virtual_obj_t;


/* bridge port struct which maintains
 * port id, bridge port id, port type, bridge port type
 * and vlan id
 */
typedef struct {
    sai_object_id_t brport_obj_id;
    sai_object_id_t port_obj_id;
    ndi_port_type_t port_type;
    ndi_brport_type_t brport_type;
    hal_vlan_id_t vlan_id;

} ndi_brport_obj_t;


/* struct to maintain bridge tunnel port, vni and ip address */
typedef struct {
    sai_object_id_t bridge_port_id;
    vxlan_id_t vni;
    sai_ip_address_t ip;

} ndi_bridge_tunnel_port_t;


/* struct to maintain bridge port slave id and its type */
typedef struct {
    sai_object_id_t slave_oid;
    ndi_brport_slave_type_t slave_type;
}ndi_brport_slave_t;


/* struct to maintain bridge port id to its slave ports */
typedef struct {
    sai_object_id_t master_oid;
    ndi_brport_slave_t slave;
}ndi_brport_slave_obj_t;


#ifdef __cplusplus
extern "C"{
#endif

bool nas_ndi_add_virtual_obj(ndi_virtual_obj_t * obj);

bool nas_ndi_remove_virtual_obj(ndi_virtual_obj_t * obj);

bool nas_ndi_get_virtual_obj(ndi_virtual_obj_t * obj,ndi_virtual_obj_query_type_t qtype);

bool nas_ndi_add_bridge_port_obj(ndi_brport_obj_t * obj);

bool nas_ndi_remove_bridge_port_obj(ndi_brport_obj_t * obj);

bool nas_ndi_get_bridge_port_obj(ndi_brport_obj_t * obj,ndi_brport_query_type_t qtype);

bool nas_ndi_add_brport_slave(ndi_brport_slave_obj_t * obj);

bool nas_ndi_remove_brport_slave(ndi_brport_slave_obj_t * obj);

#ifdef __cplusplus
}
#endif


#endif /* NAS_NDI_INC_NAS_NDI_OBJ_CACHE_H_ */
