/*
 * Copyright (c) 2017 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/*
 * filename: nas_ndi_l2mc.cpp
 */

#include "std_ip_utils.h"
#include "sail2mcgroup.h"
#include "saistatus.h"
#include "nas_ndi_int.h"
#include "nas_ndi_l2mc.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_bridge_port.h"
#include "std_mutex_lock.h"
#include <unordered_map>

#include <inttypes.h>


typedef ndi_obj_id_t l2mc_grp_id;
static std_mutex_lock_create_static_init_rec(_l2mc_map_lock);

using member_map_t = std::unordered_map <ndi_obj_id_t , sai_object_id_t>; /*First member bridge_port_id */
using l2mc_grp_mem_map_t = std::unordered_map <l2mc_grp_id , member_map_t>; /*First member is l2mc group id */

l2mc_grp_mem_map_t l2mc_grp_mem_map;


std_mutex_type_t *ndi_l2mc_mutex_lock()
{
    return &_l2mc_map_lock;
}

static inline sai_l2mc_group_api_t *ndi_l2mc_group_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return ndi_db_ptr->ndi_sai_api_tbl.n_sai_l2mc_grp_api_tbl;
}

static bool _del_mem_from_map(ndi_obj_id_t group_id, ndi_obj_id_t brport_id) {

    std_mutex_simple_lock_guard lock_t(ndi_l2mc_mutex_lock());

    auto it = l2mc_grp_mem_map.find(group_id);
    if (it == l2mc_grp_mem_map.end()) {
        NDI_MCAST_LOG_ERROR("Failed to find multicast group %llu in l2mc map",
         group_id);
        return false;
    }
    auto embedded_map = &it->second;
    auto it_embedded = embedded_map->find(brport_id);
    if (it_embedded == embedded_map->end()) {
        NDI_MCAST_LOG_ERROR("Map failed to find tun brport %llu in multicast group %llu ",
                  brport_id, group_id);
        return false;
    }
    embedded_map->erase(brport_id);
    if (embedded_map->empty()) {
        /* TODO: remove after testing */
        NDI_MCAST_LOG_ERROR(" Erasing main map entry for multicast group %llu ",
                    group_id);
        l2mc_grp_mem_map.erase(it);
    }
    return true;
}

bool _add_member_to_map(ndi_obj_id_t group_id, ndi_obj_id_t brport_id, sai_object_id_t member_id ) {


    std_mutex_simple_lock_guard lock_t(ndi_l2mc_mutex_lock());
    auto it = l2mc_grp_mem_map.find(group_id);
    if (it != l2mc_grp_mem_map.end()) {
        it->second.insert(std::make_pair(brport_id, (member_id)));
    } else {
        l2mc_grp_mem_map[group_id][brport_id] =  (member_id);
    }
    return true;
}

bool _get_member_from_map(ndi_obj_id_t group_id, ndi_obj_id_t tun_brport_id, sai_object_id_t *member_id) {

     std_mutex_simple_lock_guard lock_t(ndi_l2mc_mutex_lock());
     auto it = l2mc_grp_mem_map.find(group_id);
     if (it == l2mc_grp_mem_map.end()) {
         NDI_MCAST_LOG_ERROR("Failed to find multicast group %llu in l2mc map",
          group_id);
         return false;
     }
     auto embedded_map = &it->second;
     auto it_embedded = embedded_map->find(tun_brport_id);
     if (it_embedded == embedded_map->end()) {
         NDI_MCAST_LOG_ERROR("Map failed to find tun brport %llu in multicast group %llu ",
                     tun_brport_id, group_id);
         return false;
     }
     *(member_id) = it_embedded->second;
     return true;
}

t_std_error ndi_l2mc_group_create(npu_id_t npu_id, ndi_obj_id_t *mc_grp_id_p)
{
    if (mc_grp_id_p == nullptr) {
        NDI_MCAST_LOG_ERROR("NULL pointer is not accepted for returning group ID");
        return STD_ERR(MCAST, PARAM, 0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_object_id_t sai_grp_id;
    sai_status_t sai_ret = ndi_l2mc_group_api_get(ndi_db_ptr)->create_l2mc_group(&sai_grp_id,
                                    ndi_switch_id_get(), 0, nullptr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to create multicast group");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    *mc_grp_id_p = (ndi_obj_id_t)sai_grp_id;

    return STD_ERR_OK;
}

t_std_error ndi_l2mc_group_delete(npu_id_t npu_id, ndi_obj_id_t mc_grp_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_status_t sai_ret = ndi_l2mc_group_api_get(ndi_db_ptr)->remove_l2mc_group(
                                    (sai_object_id_t)mc_grp_id);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to delete multicast group");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

static t_std_error ndi_l2mc_group_add_member_int(npu_id_t npu_id, sai_object_id_t group_id,
                                                  sai_object_id_t output_id,
                                                  sai_object_id_t *member_id_p, hal_ip_addr_t *ip_addr)
{
    if (member_id_p == nullptr) {
        NDI_MCAST_LOG_ERROR("NULL pointer is not accepted for returning member ID");
        return STD_ERR(MCAST, PARAM, 0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_object_id_t sai_grp_member_id;
    sai_attribute_t sai_attr[3];
    int cnt = 0;
    sai_attr[cnt].id = SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_GROUP_ID;
    sai_attr[cnt].value.oid = group_id;
    cnt++;
    sai_attr[cnt].id = SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_OUTPUT_ID;
    sai_attr[cnt].value.oid = output_id;
    cnt++;
    if (ip_addr != NULL) {
        sai_attr[cnt].id = SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_ENDPOINT_IP;

        if (STD_IP_IS_AFINDEX_V4(ip_addr->af_index)) {
            sai_attr[cnt].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
            sai_attr[cnt].value.ipaddr.addr.ip4 = ip_addr->u.v4_addr;
        } else {
            sai_attr[cnt].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
            memcpy (sai_attr[cnt].value.ipaddr.addr.ip6,
                            ip_addr->u.v6_addr, sizeof (sai_ip6_t));
        }
        cnt++;
    }


    sai_status_t sai_ret = ndi_l2mc_group_api_get(ndi_db_ptr)->create_l2mc_group_member(
                                    &sai_grp_member_id,
                                    ndi_switch_id_get(),
                                    cnt, sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to add multicast group member");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    *member_id_p = sai_grp_member_id;

    return STD_ERR_OK;
}

/* TODO: handle rem_ip and send it */

t_std_error ndi_l2mc_handle_tunnel_member(npu_id_t npu_id,
                ndi_obj_id_t group_id, ndi_obj_id_t tun_brport_id, hal_ip_addr_t *rem_ip, bool add)
{
    t_std_error rc = STD_ERR_OK;
    sai_object_id_t member_id_p;

    if (add) {
        rc =  ndi_l2mc_group_add_member_int(npu_id, group_id, tun_brport_id, &member_id_p, rem_ip);
        if (rc == STD_ERR_OK) {
           _add_member_to_map(group_id, tun_brport_id, member_id_p);
        } else {
           NDI_MCAST_LOG_ERROR("Add tunnel port to l2mc  failed to for tun_br_port %" PRIx64 " " "and l2mc id %" PRIx64 " ",
                                                                                            tun_brport_id, group_id);
        }
    } else {
        if (!_get_member_from_map(group_id, tun_brport_id, &member_id_p)) {
            return STD_ERR(MCAST, FAIL, 0);
        }
        rc = ndi_l2mc_group_delete_member(npu_id, member_id_p);
        if (rc == STD_ERR_OK) {
            _del_mem_from_map(group_id, tun_brport_id);
        } else {
            NDI_MCAST_LOG_ERROR("Del tunnelport frm l2mc  failed to for sai_port %" PRIx64 " " "and l2mc id %" PRIx64 " ",
                                                                                            tun_brport_id, group_id);
        }
    }
    return rc;
}

/* These 2 API are handling .1d bridge members : lag or ports */


t_std_error ndi_l2mc_handle_lagport_add (npu_id_t npu_id,
           ndi_obj_id_t group_id, ndi_obj_id_t lag_id, hal_vlan_id_t vid, bool add) {

    sai_object_id_t brport_oid;
    sai_object_id_t member_id_p;
    t_std_error ret_code;

    if (!ndi_get_1d_bridge_port(&brport_oid, lag_id , vid)) {
        NDI_MCAST_LOG_ERROR("Add lag to l2mc  failed to get bridge port for sai_port %" PRIx64 " " "and vlan id %d"
                                      , lag_id, vid);
        return STD_ERR(MCAST, FAIL, 0);
    }

    if (add) {
        ret_code =  ndi_l2mc_group_add_member_int(npu_id, group_id, brport_oid, &member_id_p, NULL);
        if (ret_code == STD_ERR_OK) {
            _add_member_to_map(group_id, brport_oid, member_id_p);
        } else {
             NDI_MCAST_LOG_ERROR("Add lag to l2mc  failed to for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            lag_id, vid);
        }
    } else {
        if (!_get_member_from_map(group_id, brport_oid, &member_id_p)) {
            return STD_ERR(MCAST, FAIL, 0);
        }
        ret_code = ndi_l2mc_group_delete_member(npu_id, member_id_p);
        if  (ret_code == STD_ERR_OK) {
            _del_mem_from_map(group_id, brport_oid);
        } else {
            NDI_MCAST_LOG_ERROR("Del lag frm l2mc  failed to for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            lag_id, vid);
       }
    }
    return ret_code;
}

t_std_error ndi_l2mc_handle_subport_add (npu_id_t npu_id,
           ndi_obj_id_t group_id, port_t port_id, hal_vlan_id_t vid, bool add) {

    sai_object_id_t sai_port;
    sai_object_id_t brport_oid;
    sai_object_id_t member_id_p;

    t_std_error ret_code = ndi_sai_port_id_get(npu_id, port_id, &sai_port);
    if (ret_code != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Could not find SAI port ID for npu %d port %d",
                            npu_id, port_id);
        return ret_code;
    }
    if (!ndi_get_1d_bridge_port(&brport_oid, sai_port , vid)) {
        NDI_MCAST_LOG_ERROR("Add subport to l2mc  failed to get bridge port for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            sai_port, vid);
        return STD_ERR(MCAST, FAIL, 0);
    }
    if (add) {
        ret_code =  ndi_l2mc_group_add_member_int(npu_id, group_id, brport_oid, &member_id_p, NULL);
        if (ret_code == STD_ERR_OK) {
            _add_member_to_map(group_id, brport_oid, member_id_p);
        } else {
             NDI_MCAST_LOG_ERROR("Add subport to l2mc  failed to for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            sai_port, vid);
        }
    } else {
        if (!_get_member_from_map(group_id, brport_oid, &member_id_p)) {
            return STD_ERR(MCAST, FAIL, 0);
        }
        ret_code = ndi_l2mc_group_delete_member(npu_id, member_id_p);
        if  (ret_code == STD_ERR_OK) {
            _del_mem_from_map(group_id, brport_oid);
        } else {
            NDI_MCAST_LOG_ERROR("Del subport frm l2mc  failed to for sai_port %" PRIx64 " " "and vlan id %d",
                                                                                            sai_port, vid);
       }
    }
    return ret_code;
}



/* For tunnel bridge port delete use ndi_l2mc_group_delete_member */


t_std_error ndi_l2mc_group_add_port_member(npu_id_t npu_id,
                                            ndi_obj_id_t group_id, port_t port_id,
                                            ndi_obj_id_t *member_id_p)
{
    sai_object_id_t sai_port;
    t_std_error ret_code = ndi_sai_port_id_get(npu_id, port_id, &sai_port);
    if (ret_code != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Could not find SAI port ID for npu %d port %d",
                            npu_id, port_id);
        return ret_code;
    }
    sai_object_id_t bridge_port;
    if (!ndi_get_1q_bridge_port(&bridge_port, sai_port)) {
        NDI_MCAST_LOG_ERROR("Could not find bridge port ID for SAI port 0x%" PRIx64,
                            sai_port);
        return STD_ERR(MCAST, FAIL, 0);
    }

    return ndi_l2mc_group_add_member_int(npu_id, (sai_object_id_t)group_id, bridge_port,
                                          (sai_object_id_t *)member_id_p, NULL);
}

t_std_error ndi_l2mc_group_add_lag_member(npu_id_t npu_id,
                                           ndi_obj_id_t group_id, ndi_obj_id_t lag_id,
                                           ndi_obj_id_t *member_id_p)
{
    sai_object_id_t bridge_port;
    if (!ndi_get_1q_bridge_port(&bridge_port, static_cast<sai_object_id_t>(lag_id))) {
        NDI_MCAST_LOG_ERROR("Could not find bridge port ID for LAG ID 0x%" PRIx64,
                            lag_id);
        return STD_ERR(MCAST, FAIL, 0);
    }
    return ndi_l2mc_group_add_member_int(npu_id, (sai_object_id_t)group_id, bridge_port,
                                          (sai_object_id_t *)member_id_p, NULL);
}

t_std_error ndi_l2mc_group_delete_member(npu_id_t npu_id, ndi_obj_id_t member_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_status_t sai_ret = ndi_l2mc_group_api_get(ndi_db_ptr)->remove_l2mc_group_member(
                                    (sai_object_id_t)member_id);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to delete multicast group member");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}
