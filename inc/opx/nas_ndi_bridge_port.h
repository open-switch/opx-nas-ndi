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
 * nas_ndi_bridge_port.h
 */

#ifndef _NAS_NDI_BRIDGE_PORT_H_
#define _NAS_NDI_BRIDGE_PORT_H_

#include "nas_ndi_common.h"
#include "saitypes.h"
#include "nas_ndi_obj_cache.h"

#ifdef __cplusplus
extern "C"{
#endif

t_std_error ndi_init_brport_for_1Q(void);

t_std_error ndi_set_bridge_port_attribute(npu_id_t npu_id, sai_object_id_t brport_oid, sai_attribute_t *sai_attr);
t_std_error ndi_get_bridge_port_attribute(npu_id_t npu_id, sai_object_id_t brport_oid, sai_attribute_t *sai_attr, int count);


t_std_error nas_ndi_delete_bridge_port_1Q(npu_id_t npu_id, sai_object_id_t sai_port);
t_std_error nas_ndi_create_bridge_port_1Q(npu_id_t npu_id, sai_object_id_t sai_port, bool lag);
t_std_error ndi_brport_attr_set_or_get_1Q(npu_id_t npu_id, sai_object_id_t port_id, bool set, sai_attribute_t *sai_attr);

bool ndi_get_1q_bridge_port(sai_object_id_t *brport_oid, sai_object_id_t saiport_oid);

/* For subports type 1d i.e  port and lag */
bool ndi_get_1d_bridge_port(sai_object_id_t *brport_oid, sai_object_id_t saiport_oid,  hal_vlan_id_t vlan_id );
bool ndi_get_1q_sai_port(sai_object_id_t brport_oid, sai_object_id_t *saiport_oid);

t_std_error ndi_1d_bridge_tunnel_port_add(npu_id_t npu_id, sai_object_id_t br_oid, sai_object_id_t tunnel_oid, sai_object_id_t *bridge_port_id);

t_std_error ndi_1d_bridge_tunnel_delete(npu_id_t npu_id, sai_object_id_t tun_brport_oid);

t_std_error ndi_bridge_port_tunnel_stats_get(npu_id_t npu_id, sai_object_id_t tun_bridge_port_oid, ndi_stat_id_t *ndi_stat_ids, uint64_t* stats_val, size_t len);

#ifdef __cplusplus
}
#endif

#endif  /*  _NAS_NDI_BRIDGE_PORT_H_ */
