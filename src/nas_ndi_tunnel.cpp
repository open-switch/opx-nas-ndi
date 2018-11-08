/*
 * Copyright (c) 2018 Dell Inc.
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
 * filename: nas_ndi_tunnel.cpp
 */



#include "nas_ndi_tunnel_map.h"
#include "std_error_codes.h"
#include "std_assert.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_bridge_port.h"
#include "nas_ndi_common.h"
#include "dell-interface.h"
#include "sai.h"
#include "saitunnel.h"
#include "std_ip_utils.h"
#include "std_mutex_lock.h"
#include "tunnel.h"
#include "nas_ndi_1d_bridge.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


static std_mutex_lock_create_static_init_rec(_tun_lock); /* Syncronize remote_end point create or delete which accesses nas_ndi_tunnel_obj_map */
//sai_object_id_t underlay_rif = 0;

typedef struct tunnel_create_info {
   hal_ip_addr_t remote_src_ip;
   hal_ip_addr_t local_src_ip;
   uint32_t vni;
   ndi_obj_id_t vrf_oid;
   ndi_obj_id_t ulay_rif_oid;
   ndi_obj_id_t bridge_oid;
} tun_info_t;

std_mutex_type_t *ndi_tun_mutex_lock()
{
    return &_tun_lock;
}

/*  Router Interface APIs  */
static inline  sai_router_interface_api_t *ndi_rif_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_route_interface_api_tbl);
}

static inline  sai_tunnel_api_t *ndi_sai_tunnel_api(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_tunnel_api_tbl);
}


/*  Virtual Router specific APIs  */
static inline  sai_virtual_router_api_t *ndi_route_vr_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_virtual_router_api_tbl);
}


/* Create encap tunnel map object for holding tunnel map entries */
static t_std_error ndi_create_encap_tunnel_map(npu_id_t npu,
                                               sai_object_id_t *encap_tunnel_map_oid)
{
    sai_status_t    sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t encap_tunnel_map_attr;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    encap_tunnel_map_attr.id=SAI_TUNNEL_MAP_ATTR_TYPE;
    encap_tunnel_map_attr.value.s32=SAI_TUNNEL_MAP_TYPE_BRIDGE_IF_TO_VNI;

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->create_tunnel_map(encap_tunnel_map_oid,
                                                                    ndi_switch_id_get(),
                                                                    1,
                                                                    &encap_tunnel_map_attr)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: create_encap_tunnel_map");

        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Remove encap tunnel map object for holding tunnel map entries */
static t_std_error ndi_remove_encap_tunnel_map(npu_id_t npu,
                                               sai_object_id_t encap_tunnel_map_oid)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->remove_tunnel_map(encap_tunnel_map_oid)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: remove_encap_tunnel_map");
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Create decap tunnel map object for holding tunnel map entries */
static t_std_error ndi_create_decap_tunnel_map(npu_id_t npu, sai_object_id_t *decap_tunnel_map_oid)
{
    sai_status_t    sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t decap_tunnel_map_attr;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    decap_tunnel_map_attr.id=SAI_TUNNEL_MAP_ATTR_TYPE;
    decap_tunnel_map_attr.value.s32=SAI_TUNNEL_MAP_TYPE_VNI_TO_BRIDGE_IF;

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->create_tunnel_map(decap_tunnel_map_oid,
                                                                   ndi_switch_id_get(),
                                                                   1, &decap_tunnel_map_attr)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: create_decap_tunnel_map");
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Remove decap tunnel map object for holding tunnel map entries */
static t_std_error ndi_remove_decap_tunnel_map(npu_id_t npu,
                                               sai_object_id_t decap_tunnel_map_oid)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->remove_tunnel_map(decap_tunnel_map_oid)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: remove_decap_tunnel_map");
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Create encap tunnel map entry for mapping bridge to vnid. Also associate the encap map entry to encap tunnel map */
static t_std_error ndi_create_encap_tunnel_map_entry(npu_id_t npu,
                                                     sai_object_id_t *encap_tunnel_map_entry_oid,
                                                     ndi_obj_id_t bridge_oid,
                                                     sai_object_id_t encap_tunnel_map_oid,
                                                     uint32_t vnid)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_attribute_t encap_tunnel_map_entry_attrs[4];
    encap_tunnel_map_entry_attrs[0].id=SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE;
    encap_tunnel_map_entry_attrs[0].value.s32=SAI_TUNNEL_MAP_TYPE_BRIDGE_IF_TO_VNI;

    encap_tunnel_map_entry_attrs[1].id=SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP;
    encap_tunnel_map_entry_attrs[1].value.oid=encap_tunnel_map_oid;

    encap_tunnel_map_entry_attrs[2].id=SAI_TUNNEL_MAP_ENTRY_ATTR_BRIDGE_ID_KEY;
    encap_tunnel_map_entry_attrs[2].value.oid=bridge_oid;

    encap_tunnel_map_entry_attrs[3].id=SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_VALUE;
    encap_tunnel_map_entry_attrs[3].value.u32=vnid;

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->create_tunnel_map_entry(encap_tunnel_map_entry_oid,
                                                                          ndi_switch_id_get(),
                                                                          4,
                                                                          encap_tunnel_map_entry_attrs)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: create_encp_tunnel_map_entry bridge_oid %llu, vni %u", bridge_oid, vnid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Remove encap tunnel map entry for mapping bridge to vnid. Also associate the encap map entry to encap tunnel map */
static t_std_error ndi_remove_encap_tunnel_map_entry(npu_id_t npu, sai_object_id_t encap_tunnel_map_entry_oid)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->remove_tunnel_map_entry(encap_tunnel_map_entry_oid)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: remove_tunnel_map_entry bridge_oid %llu", encap_tunnel_map_entry_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Create decap tunnel map entry for mapping vnid to bridge. Also associate the decap map entry to decap tunnel map */
static t_std_error ndi_create_decap_tunnel_map_entry(npu_id_t npu,
                                                     sai_object_id_t *decap_tunnel_map_entry_oid,
                                                     ndi_obj_id_t bridge_oid,
                                                     sai_object_id_t decap_tunnel_map_oid,
                                                     uint32_t vnid)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_attribute_t decap_tunnel_map_entry_attrs[4];
    decap_tunnel_map_entry_attrs[0].id=SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE;
    decap_tunnel_map_entry_attrs[0].value.s32=SAI_TUNNEL_MAP_TYPE_VNI_TO_BRIDGE_IF;

    decap_tunnel_map_entry_attrs[1].id=SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP;
    decap_tunnel_map_entry_attrs[1].value.oid=decap_tunnel_map_oid;

    decap_tunnel_map_entry_attrs[2].id=SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_KEY;
    decap_tunnel_map_entry_attrs[2].value.u32=vnid;

    decap_tunnel_map_entry_attrs[3].id=SAI_TUNNEL_MAP_ENTRY_ATTR_BRIDGE_ID_VALUE;
    decap_tunnel_map_entry_attrs[3].value.oid=bridge_oid;

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->create_tunnel_map_entry(decap_tunnel_map_entry_oid,
                                                                          ndi_switch_id_get(),
                                                                          4,
                                                                          decap_tunnel_map_entry_attrs)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: create_decp_tunnel_map_entry bridge_oid %llu, vni %u", bridge_oid, vnid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Remove decap tunnel map entry for mapping bridge to vnid. Also associate the encap map entry to encap tunnel map */
static t_std_error ndi_remove_decap_tunnel_map_entry(npu_id_t npu,
                                                     sai_object_id_t decap_tunnel_map_entry_oid)
{

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->remove_tunnel_map_entry(decap_tunnel_map_entry_oid)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: remove_decp_tunnel_map_entry  %llu", decap_tunnel_map_entry_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Create vxlan tunnel */
static t_std_error ndi_create_tunnel(npu_id_t npu,
                                     sai_object_id_t *tunnel_oid,
                                     sai_object_id_t underlay_rif_oid,
                                     sai_object_id_t encap_tunnel_map_oid,
                                     sai_object_id_t decap_tunnel_map_oid,
                                     const hal_ip_addr_t *src_ip)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }
    char buff[HAL_INET6_TEXT_LEN + 1];

    std_ip_to_string(src_ip, buff, HAL_INET6_TEXT_LEN);
    NDI_IDBR_LOG_TRACE("create_tunnel for IP %s", buff);

    sai_attribute_t tunnel_attrs[8];
    tunnel_attrs[0].id=SAI_TUNNEL_ATTR_TYPE;
    tunnel_attrs[0].value.s32=SAI_TUNNEL_TYPE_VXLAN;

    tunnel_attrs[1].id=SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE;
    tunnel_attrs[1].value.oid=underlay_rif_oid;

    tunnel_attrs[2].id=SAI_TUNNEL_ATTR_OVERLAY_INTERFACE;
    tunnel_attrs[2].value.oid=underlay_rif_oid;

    tunnel_attrs[3].id=SAI_TUNNEL_ATTR_ENCAP_MAPPERS;
    tunnel_attrs[3].value.objlist.count=1;
    tunnel_attrs[3].value.objlist.list=&encap_tunnel_map_oid;

    tunnel_attrs[4].id=SAI_TUNNEL_ATTR_DECAP_MAPPERS;
    tunnel_attrs[4].value.objlist.count=1;
    tunnel_attrs[4].value.objlist.list=&decap_tunnel_map_oid;

    tunnel_attrs[5].id=SAI_TUNNEL_ATTR_ENCAP_SRC_IP;
    if (STD_IP_IS_AFINDEX_V4(src_ip->af_index)) {
         tunnel_attrs[5].value.ipaddr.addr_family=SAI_IP_ADDR_FAMILY_IPV4;
         tunnel_attrs[5].value.ipaddr.addr.ip4 = src_ip->u.v4_addr;
    } else {
         tunnel_attrs[5].value.ipaddr.addr_family=SAI_IP_ADDR_FAMILY_IPV6;
         memcpy (tunnel_attrs[5].value.ipaddr.addr.ip6,  src_ip->u.v6_addr, sizeof (sai_ip6_t));
    }

    tunnel_attrs[6].id = SAI_TUNNEL_ATTR_ENCAP_TTL_MODE;
    tunnel_attrs[6].value.s32 = SAI_TUNNEL_TTL_MODE_PIPE_MODEL;

    /* TTL in the outer IP header needs to be set to 255 during encap.
     * When inner packet in the vxlan tunnel is non-ip and the ttl mode
     * is set to the default uniform model, NPUs don't have a common
     * behavior, ex: td3 sets outer ttl as 0 whereas td2 etc sets as 255.
     * To get the same behavior across NPUs, set the ttl explicitly.*/
    tunnel_attrs[7].id = SAI_TUNNEL_ATTR_ENCAP_TTL_VAL;
    tunnel_attrs[7].value.u8 = NDI_TTL;

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->create_tunnel(tunnel_oid,
                                                                ndi_switch_id_get(),
                                                                8, tunnel_attrs)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: create_tunnel for IP %s", buff);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Remove vxlan tunnel */
static t_std_error ndi_remove_tunnel(npu_id_t npu,
                                     sai_object_id_t tunnel_oid)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->remove_tunnel(tunnel_oid)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: remove_tunnel for oid %llu", tunnel_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


t_std_error ndi_tunnel_stats_get(npu_id_t npu_id,
                                 nas_com_id_value_t tunnel_params[], size_t tp_len,
                                 ndi_stat_id_t *ndi_stat_ids, uint64_t* stats_val, size_t len)

{
    const unsigned int list_len = len;
    sai_tunnel_stat_t sai_tunnel_stats_ids[list_len];
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    hal_ip_addr_t local_src_ip, remote_src_ip;
    local_src_ip = remote_src_ip ={0};

    if (tp_len != 2) {
       NDI_IDBR_LOG_ERROR("Remove remote endpt recived invalid size recieved 2 vs %d", tp_len);
       return STD_ERR(NPU, FAIL, 0);
    }

    for (uint32_t i = 0 ;i < tp_len ; i++) {
       switch (tunnel_params[i].attr_id) {
          case (TUNNEL_TUNNEL_STATE_TUNNELS_REMOTE_IP_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(hal_ip_addr_t)){
                  NDI_IDBR_LOG_ERROR("Invalid Remote IP- size mismatch addr %d vs %d.",
                    sizeof(hal_ip_addr_t), tunnel_params[i].vlen);
                  return STD_ERR(NPU, FAIL, 0);

              }
              memcpy(&remote_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
          case (TUNNEL_TUNNEL_STATE_TUNNELS_LOCAL_IP_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(hal_ip_addr_t)) {
                  NDI_IDBR_LOG_ERROR("Invalid SRC IP- size mismatch addr %d vs %d.",
                    sizeof(hal_ip_addr_t), tunnel_params[i].vlen);
                   return STD_ERR(NPU, FAIL, 0);
              }
              memcpy(&local_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
       }
    }
    char buff[HAL_INET6_TEXT_LEN + 1];
    std_ip_to_string((const hal_ip_addr_t*) &remote_src_ip, buff, HAL_INET6_TEXT_LEN);
    NDI_IDBR_LOG_TRACE("ndi_tunnel_stats_get: Remote ip %s \n", buff);
    /* NAS to check if a Tunnel Object with correct
     * Termination entry exists for this Remote end-point */
    std_mutex_simple_lock_guard lock_t(ndi_tun_mutex_lock());
    TunnelObj *obj = get_tunnel_obj((const hal_ip_addr_t*)&remote_src_ip, (const hal_ip_addr_t*) &local_src_ip);
    if (obj == NULL) {
        NDI_IDBR_LOG_ERROR("No tunnel obj for remote ip %s\n", buff);
        return STD_ERR(NPU, CFG, 0);
    }

    sai_object_id_t tunnel_oid = obj->get_tun_oid();
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_VLAN_LOG_ERROR("Invalid NPU Id %d passed",npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_tunnel_stats(ndi_stat_ids[ix],&sai_tunnel_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->get_tunnel_stats(tunnel_oid,
                    len, sai_tunnel_stats_ids, stats_val)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Tunnel stats Get failed for npu %d, Tunnel %d, ret %d \n",
                            npu_id, tunnel_oid, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}


t_std_error ndi_tunnel_stats_clear(npu_id_t npu_id,
                                   nas_com_id_value_t tunnel_params[], size_t tp_len,
                                   ndi_stat_id_t *ndi_stat_ids, size_t len)

{
    const unsigned int list_len = len;
    sai_tunnel_stat_t sai_tunnel_stats_ids[list_len];
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    hal_ip_addr_t local_src_ip, remote_src_ip;
    local_src_ip = remote_src_ip ={0};

    if (tp_len != 2) {
       NDI_IDBR_LOG_ERROR("Remove remote endpt recived invalid size recieved 2 vs %d", tp_len);
       return STD_ERR(NPU, FAIL, 0);
    }

    for (unsigned int i = 0 ;i < tp_len ; i++) {
       switch (tunnel_params[i].attr_id) {
          case (TUNNEL_CLEAR_TUNNEL_STATS_INPUT_REMOTE_IP_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(hal_ip_addr_t)){
                  NDI_IDBR_LOG_ERROR("Invalid Remote IP- size mismatch addr %d vs %d.",
                    sizeof(hal_ip_addr_t), tunnel_params[i].vlen);
                  return STD_ERR(NPU, FAIL, 0);

              }
              memcpy(&remote_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
          case (TUNNEL_CLEAR_TUNNEL_STATS_INPUT_LOCAL_IP_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(hal_ip_addr_t)) {
                  NDI_IDBR_LOG_ERROR("Invalid SRC IP- size mismatch addr %d vs %d.",
                    sizeof(hal_ip_addr_t), tunnel_params[i].vlen);
                   return STD_ERR(NPU, FAIL, 0);
              }
              memcpy(&local_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
       }
    }
    char buff[HAL_INET6_TEXT_LEN + 1];
    std_ip_to_string((const hal_ip_addr_t*) &remote_src_ip, buff, HAL_INET6_TEXT_LEN);
    NDI_IDBR_LOG_TRACE("ndi_tunnel_stats_get: Remote ip %s \n", buff);
    /* NAS to check if a Tunnel Object with correct
     * Termination entry exists for this Remote end-point */
    std_mutex_simple_lock_guard lock_t(ndi_tun_mutex_lock());
    TunnelObj *obj = get_tunnel_obj((const hal_ip_addr_t*)&remote_src_ip, (const hal_ip_addr_t*) &local_src_ip);
    if (obj == NULL) {
        NDI_IDBR_LOG_ERROR("No tunnel obj for remote ip %s\n", buff);
        return STD_ERR(NPU, CFG, 0);
    }

    sai_object_id_t tunnel_oid = obj->get_tun_oid();
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_VLAN_LOG_ERROR("Invalid NPU Id %d passed",npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_tunnel_stats(ndi_stat_ids[ix],&sai_tunnel_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->clear_tunnel_stats(tunnel_oid,
                    len, sai_tunnel_stats_ids)) != SAI_STATUS_SUCCESS) {
        NDI_VLAN_LOG_ERROR("Tunnel stats clear failed for npu %d, Tunnel %llu, ret %d \n",
                            npu_id, tunnel_oid, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

/* create vxlan tunnel termination entry */
static t_std_error ndi_create_tunnel_term_entry(npu_id_t npu,
                                                sai_object_id_t *tunnel_term_oid,
                                                sai_object_id_t vr_oid,
                                                sai_object_id_t tunnel_oid,
                                               const hal_ip_addr_t* remote_src_ip,
                                               const hal_ip_addr_t* local_as_dst_ip)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }
    char buff[HAL_INET6_TEXT_LEN + 1], buff2[HAL_INET6_TEXT_LEN + 1];
    std_ip_to_string(remote_src_ip, buff, HAL_INET6_TEXT_LEN);
    std_ip_to_string(local_as_dst_ip, buff2, HAL_INET6_TEXT_LEN);
    NDI_IDBR_LOG_TRACE("create_tunnel term_enry for remote_src IP %s, local_as_dest_ip %s", buff, buff2);

    sai_attribute_t tunnel_term_attrs[6];
    tunnel_term_attrs[0].id=SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_VR_ID;
    tunnel_term_attrs[0].value.oid=vr_oid;

    tunnel_term_attrs[1].id=SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TYPE;
    tunnel_term_attrs[1].value.s32=SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2P;

    tunnel_term_attrs[2].id=SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_DST_IP;

    if (STD_IP_IS_AFINDEX_V4(local_as_dst_ip->af_index)) {
        tunnel_term_attrs[2].value.ipaddr.addr_family=SAI_IP_ADDR_FAMILY_IPV4;
        tunnel_term_attrs[2].value.ipaddr.addr.ip4 = local_as_dst_ip->u.v4_addr;
    } else {
        tunnel_term_attrs[2].value.ipaddr.addr_family=SAI_IP_ADDR_FAMILY_IPV6;
        memcpy (tunnel_term_attrs[2].value.ipaddr.addr.ip6, local_as_dst_ip->u.v6_addr, sizeof (sai_ip6_t));
    }

    tunnel_term_attrs[3].id=SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_SRC_IP;

    if (STD_IP_IS_AFINDEX_V4(remote_src_ip->af_index)) {
        tunnel_term_attrs[3].value.ipaddr.addr_family=SAI_IP_ADDR_FAMILY_IPV4;
        tunnel_term_attrs[3].value.ipaddr.addr.ip4 = remote_src_ip->u.v4_addr;
    } else {
        tunnel_term_attrs[3].value.ipaddr.addr_family=SAI_IP_ADDR_FAMILY_IPV6;
        memcpy(tunnel_term_attrs[3].value.ipaddr.addr.ip6, remote_src_ip->u.v6_addr, sizeof (sai_ip6_t));
    }

    tunnel_term_attrs[4].id=SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TUNNEL_TYPE;
    tunnel_term_attrs[4].value.s32=SAI_TUNNEL_TYPE_VXLAN;

    tunnel_term_attrs[5].id=SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_ACTION_TUNNEL_ID;
    tunnel_term_attrs[5].value.oid=tunnel_oid;

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->create_tunnel_term_table_entry(tunnel_term_oid,
                                                                                 ndi_switch_id_get(),
                                                                                 6, tunnel_term_attrs)) != SAI_STATUS_SUCCESS) {

        NDI_IDBR_LOG_ERROR("SAI FAILURE: create_tunnel_term_tbl for oid %llu", tunnel_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}


/* Remove vxlan tunnel termination entry */
static t_std_error ndi_remove_tunnel_term_entry(npu_id_t npu, sai_object_id_t tunnel_term_oid)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    if((sai_ret = ndi_sai_tunnel_api(ndi_db_ptr)->remove_tunnel_term_table_entry(tunnel_term_oid)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: ndi_remove_tunnel_term_entry for oid %llu", tunnel_term_oid);
        return STD_ERR(NPU, CFG, sai_ret);
    }

    return STD_ERR_OK;
}

static t_std_error
ndi_create_tunnel_basic_entries(npu_id_t npu_id, ndi_obj_id_t vrf_oid, ndi_obj_id_t ulay_oid,
const hal_ip_addr_t *local_src_ip, const hal_ip_addr_t *remote_src_ip) {

    sai_object_id_t tunnel_term_oid            = SAI_NULL_OBJECT_ID;
    sai_object_id_t tunnel_oid                 = SAI_NULL_OBJECT_ID;
    sai_object_id_t encap_tunnel_map_oid       = SAI_NULL_OBJECT_ID;
    sai_object_id_t decap_tunnel_map_oid       = SAI_NULL_OBJECT_ID;
    hal_ip_addr_t loc_ip, rem_ip;

    do {
        if (STD_ERR_OK != ndi_create_encap_tunnel_map(npu_id, &encap_tunnel_map_oid)) {
            break;
        }

        if (STD_ERR_OK != ndi_create_decap_tunnel_map(npu_id, &decap_tunnel_map_oid)){
            break;
        }

        memcpy(&loc_ip , local_src_ip, sizeof (hal_ip_addr_t));
        memcpy(&rem_ip, remote_src_ip, sizeof (hal_ip_addr_t));

        if (STD_ERR_OK != ndi_create_tunnel(npu_id, &tunnel_oid, ulay_oid, encap_tunnel_map_oid,
                 decap_tunnel_map_oid, local_src_ip)) {
            break;
        }
        if (STD_ERR_OK != ndi_create_tunnel_term_entry(npu_id, &tunnel_term_oid, vrf_oid, tunnel_oid,
              remote_src_ip, local_src_ip)) {
            break;
        }
        TunnelObj *obj = new TunnelObj(tunnel_oid, encap_tunnel_map_oid, decap_tunnel_map_oid,
                                tunnel_term_oid, loc_ip, rem_ip);

        if (false == insert_tunnel_obj(remote_src_ip, local_src_ip, (TunnelObj *)obj)) {
            delete(obj);
            break;
        }
        return STD_ERR_OK;
    } while (0);

    if(encap_tunnel_map_oid != SAI_NULL_OBJECT_ID) {
        ndi_remove_encap_tunnel_map(npu_id, encap_tunnel_map_oid);
    }

    if(decap_tunnel_map_oid != SAI_NULL_OBJECT_ID) {
        ndi_remove_decap_tunnel_map(npu_id, decap_tunnel_map_oid);
    }

    if(tunnel_term_oid != SAI_NULL_OBJECT_ID) {
        ndi_remove_tunnel_term_entry(npu_id, tunnel_term_oid);
    }

    if(tunnel_oid != SAI_NULL_OBJECT_ID) {
        ndi_remove_tunnel(npu_id, tunnel_oid);
        remove_tunnel_obj(remote_src_ip, local_src_ip);
    }
    return STD_ERR(NPU, PARAM, 0);
}


static bool ndi_handle_tunnel_creation(npu_id_t npu_id, const tun_info_t *info,  ndi_obj_id_t *tun_brport)

{

    sai_object_id_t encap_tunnel_map_oid       = SAI_NULL_OBJECT_ID;
    sai_object_id_t decap_tunnel_map_oid       = SAI_NULL_OBJECT_ID;
    sai_object_id_t tunnel_oid                 = SAI_NULL_OBJECT_ID;
    sai_object_id_t encap_tunnel_map_entry_oid = SAI_NULL_OBJECT_ID;
    sai_object_id_t decap_tunnel_map_entry_oid = SAI_NULL_OBJECT_ID;
    char buff2[HAL_INET6_TEXT_LEN+1];
    char buff[HAL_INET6_TEXT_LEN + 1];

    std_mutex_simple_lock_guard lock_t(ndi_tun_mutex_lock());

    std_ip_to_string(&info->remote_src_ip, buff2, HAL_INET6_TEXT_LEN);
    std_ip_to_string(&info->local_src_ip, buff, HAL_INET6_TEXT_LEN);

    NDI_IDBR_LOG_TRACE("Handle tunnel ccreate for Src IP %s Dest IP %s vrf id %llu" , buff, buff2, info->vrf_oid);

    if (has_tunnel_obj(&info->remote_src_ip, &info->local_src_ip) == false) {
        NDI_IDBR_LOG_TRACE("create basic tunnel for local src IP %s, dest_ip %s", buff, buff2);
        if (ndi_create_tunnel_basic_entries(npu_id, info->vrf_oid, info->ulay_rif_oid,  &info->local_src_ip, &info->remote_src_ip)
                   != STD_ERR_OK) {
            NDI_IDBR_LOG_ERROR("Basic tunnel entry failure for Src IP %s Dest IP %s vrf id %llu" , buff, buff2, info->vrf_oid);
            return false;
        }

    }

    /* Get tunnel object from the map */
    auto obj = get_tunnel_obj((const hal_ip_addr_t *) &info->remote_src_ip,(const hal_ip_addr_t *) &info->local_src_ip);
    if (obj == NULL) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: tunnel_create : no obj for ip %s", buff);
        return false;
    }

    /* Create tunnel bridge port type, encap and decap map entry everytime */
    /* VNI AND BRIDGE WILL BE UNIDUE WITHIN A CLASS TIUNNEL OBJECT AND ONLY ONE EVTRY POSSIBBLE FOR THE 2 */
    /* 2 VNI ON SAME BRIDGE FOR 2 UNIQUE SRC AND DEST IP ARE NOT POSSIBLE */
    /* ONE BRIDGE WILL HAVE ONLY ONE  UNIQUE VNI /VTEP ASSOCIATED WITH 2 UNIQUE SRC AND DEST */
    /* So for unique  SRC AND DEST ip in a bridge do we need to create a new tunnel port for each new VNIi for that bridge */

    encap_tunnel_map_oid = obj->get_encap_map_oid();
    decap_tunnel_map_oid = obj->get_decap_map_oid();
    do {
        /* Create decap tunnel entry for Bridge to VNI mapping & associate with the encap map above. */
        if (STD_ERR_OK != ndi_create_decap_tunnel_map_entry(npu_id, &decap_tunnel_map_entry_oid, info->bridge_oid, decap_tunnel_map_oid, info->vni)) {
            break;
        }
        if(false == obj->insert_decap_map_entry(info->bridge_oid, info->vni, decap_tunnel_map_entry_oid)) {
            NDI_IDBR_LOG_ERROR(" insert_decap_map_entry bridge %llu , vni %llu", info->bridge_oid,info-> vni);
            break;
        }

        /* Create encap tunnel entry for Bridge to VNI mapping & associate with the encap map above. */
        if (STD_ERR_OK != ndi_create_encap_tunnel_map_entry(npu_id, &encap_tunnel_map_entry_oid, info->bridge_oid, encap_tunnel_map_oid, info->vni)) {
            break;
        }
        if(false == obj->insert_encap_map_entry(info->bridge_oid, info->vni, encap_tunnel_map_entry_oid)) {
            NDI_IDBR_LOG_ERROR(" insert_encap_map_entry bridge %llu , vni %llu", info->bridge_oid, info->vni);
            obj->remove_decap_map_entry(info->vni);
            break;
        }


        /* NAS-MAC will update the remote MAC address in to SAI FDB for
         * this particular Dot1dBridge with the Tunnel BridgePort and
         * Remote MAC as Dest - sai_create_fdb_entry_fn() */


        vni_s_oid_map_t key_val;
        tunnel_oid  =  obj->get_tun_oid();
        if (!obj->get_bridge_port(info->bridge_oid, &key_val)) {
        /* If not create TunnelBridgePort and add to object */
           sai_object_id_t tun_bridgeport_oid;
           if ((ndi_1d_bridge_tunnel_port_add(npu_id, info->bridge_oid, tunnel_oid, &tun_bridgeport_oid) == STD_ERR_OK)) {
                if  (false == obj->insert_tunnel_bridge_port(info->bridge_oid, info->vni, tun_bridgeport_oid)) {
                    NDI_IDBR_LOG_ERROR(" Tunnel bridge port insert failure");
                    break;
                }
            }else {
                NDI_IDBR_LOG_ERROR(" Tunnel bridge port create  failure");
                break;
            }
            *(tun_brport) = (ndi_obj_id_t)tun_bridgeport_oid;
        } else {
            NDI_IDBR_LOG_ERROR(" ERROR :Tunnel bridge port exist for tunnel brport %llu, tun oid %llu",key_val.oid , tunnel_oid);
            break;
        }
        NDI_IDBR_LOG_TRACE("Handle  tunnel create  entry PASSED for Src IP %s Dest IP %s vrf id %llu" , buff, buff2, info->vrf_oid);
        return true;
    } while(0);

    if(encap_tunnel_map_entry_oid != SAI_NULL_OBJECT_ID) {
        ndi_remove_encap_tunnel_map_entry(npu_id, encap_tunnel_map_entry_oid);
    }

    if(decap_tunnel_map_entry_oid != SAI_NULL_OBJECT_ID) {
        ndi_remove_decap_tunnel_map_entry(npu_id, decap_tunnel_map_entry_oid);
    }
    return false;
}

extern "C" {

t_std_error
nas_ndi_add_remote_endpoint(npu_id_t npu_id, nas_com_id_value_t tunnel_params[], size_t len, nas_custom_id_value_t cus_param[],
  size_t cus_len, ndi_obj_id_t *tun_brport_oid)
{
    tun_info_t info;

    memset(&info, 0, sizeof(tun_info_t));
    union {
        bool bol;
        uint32_t u32;
        uint64_t u64;
        hal_ip_addr_t  addr;
    } un;

    if (len != 3) {
       NDI_IDBR_LOG_ERROR("Add remote endpoint :invalid size recieved 3 vs %d", len);
       return STD_ERR(NPU, FAIL, 0);
    }

    for (uint32_t i = 0 ;i < len ; i++) {
       switch (tunnel_params[i].attr_id) {
          case (DELL_IF_IF_INTERFACES_INTERFACE_REMOTE_ENDPOINT_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(un.addr)){
                  NDI_IDBR_LOG_ERROR("Invalid Remote IP- size mismatch addr %d vs %d.",
                    sizeof(un.addr), tunnel_params[i].vlen);
                  return STD_ERR(NPU, FAIL, 0);

              }
              memcpy(&info.remote_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
          case (DELL_IF_IF_INTERFACES_INTERFACE_SOURCE_IP_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(un.addr)) {
                  NDI_IDBR_LOG_ERROR("Invalid SRC IP- size mismatch addr %d vs %d.",
                    sizeof(un.addr), tunnel_params[i].vlen);
                   return STD_ERR(NPU, FAIL, 0);
              }
              memcpy(&info.local_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
          case (DELL_IF_IF_INTERFACES_INTERFACE_VNI):
          {
              if (tunnel_params[i].vlen != sizeof(un.u32)) {
                  NDI_IDBR_LOG_ERROR("Invalid vni: size mismatch vni_sz %d vs %d.",
                    sizeof(un.u32), tunnel_params[i].vlen);
                   return STD_ERR(NPU, FAIL, 0);
              }
              info.vni  = *((uint32_t*)tunnel_params[i].val);
              break;
          }
          default:
          {
              NDI_IDBR_LOG_ERROR("Create endpt Unsupported attr %d", tunnel_params[i].attr_id);
              break;
          }

       }

    }
    for (uint32_t i = 0 ;i < cus_len ; i++) {
       switch (cus_param[i].attr_id) {
          case (VRF_OID):
          {
              if (cus_param[i].vlen != sizeof(un.u64)){
                  NDI_IDBR_LOG_ERROR("Invalid VRF ID - size mismatch %d vs %d.",
                    sizeof(un.u64), cus_param[i].vlen);
                  return STD_ERR(NPU, FAIL, 0);

              }
              info.vrf_oid = *((uint64_t*)cus_param[i].val);
              break;
          }
          case (UNDERLAY_OID):
          {
              if (cus_param[i].vlen != sizeof(un.u64)){
                  NDI_IDBR_LOG_ERROR("Invalid underlay oid size mismatch  %d vs %d.",
                    sizeof(un.u64), cus_param[i].vlen);
                  return STD_ERR(NPU, FAIL, 0);

              }
              info.ulay_rif_oid = *((uint64_t*)cus_param[i].val);
              break;
          }
          case (BRIDGE_OID):
          {
              if (cus_param[i].vlen != sizeof(un.u64)){
                  NDI_IDBR_LOG_ERROR("Invalid underlay oid size mismatch  %d vs %d.",
                    sizeof(un.u64), cus_param[i].vlen);
                  return STD_ERR(NPU, FAIL, 0);

              }
              info.bridge_oid = *((uint64_t*)cus_param[i].val);;
              break;
          }
          default:
          {
              NDI_IDBR_LOG_ERROR("Create endpt cust Unsupported attr %d", tunnel_params[i].attr_id);
              break;
          }
       }
    }
#if 0
    info.bridge_oid = bridge_oid;
    info.vrf_oid = vrf_info->vrf_oid;
    info.ulay_rif_oid = vrf_info->underlay_rif_id;
#endif
    NDI_IDBR_LOG_TRACE("ADD remote endpoint to br %" PRIx64 " " "vrf_id %" PRIx64 " " "ulay_rif %" PRIx64 " " ,
                info.bridge_oid, info.vrf_oid, info.ulay_rif_oid);

    NDI_IDBR_LOG_TRACE("Received vni %d " ,info.vni);
    if (ndi_handle_tunnel_creation(npu_id, &info, tun_brport_oid)  != true) {
        NDI_IDBR_LOG_ERROR("Failed ndi_handle_tunnel_creation FOR BRIDE_OID %llu, vni %d \n",
            info.bridge_oid,info.vni);
        return STD_ERR(NPU, CFG, 0);

    }
    return STD_ERR_OK;
}

t_std_error
ndi_delete_basic_tunnel_entry(npu_id_t npu_id, TunnelObj *obj, nas_bridge_id_t bridge_oid)
{
    sai_object_id_t encap_map_oid, decap_map_oid, tunnel_term_oid,tunnel_oid;

    encap_map_oid = obj->get_encap_map_oid();
    decap_map_oid = obj->get_decap_map_oid();
    tunnel_term_oid = obj->get_term_oid();
    tunnel_oid = obj->get_tun_oid();

    if (ndi_remove_tunnel_term_entry(npu_id, tunnel_term_oid) != STD_ERR_OK) {
        return STD_ERR(NPU, CFG, 0);
    }
    if (ndi_remove_tunnel(npu_id, tunnel_oid) != STD_ERR_OK) {
        return STD_ERR(NPU, CFG, 0);
    }

    if ((ndi_remove_decap_tunnel_map(npu_id, decap_map_oid) != STD_ERR_OK) ||
             (ndi_remove_encap_tunnel_map(npu_id, encap_map_oid) != STD_ERR_OK)) {
        return STD_ERR(NPU, CFG, 0);
    }
    return STD_ERR_OK;
}

t_std_error
nas_ndi_remove_remote_endpoint(npu_id_t npu_id, nas_bridge_id_t bridge_oid, nas_com_id_value_t tunnel_params[], size_t len)
{
    hal_ip_addr_t local_src_ip, remote_src_ip;
    local_src_ip = remote_src_ip ={0};

    sai_object_id_t encap_map_entry_oid = SAI_NULL_OBJECT_ID;
    sai_object_id_t decap_map_entry_oid = SAI_NULL_OBJECT_ID;

    union {
        bool bol;
        uint32_t u32;
        hal_ip_addr_t  addr;
    } un;

    if (len != 2) {
       NDI_IDBR_LOG_ERROR("Remove remote endpt recived invalid size recieved 2 vs %d", len);
       return STD_ERR(NPU, FAIL, 0);
    }

    for (uint32_t i = 0 ;i < len ; i++) {
       switch (tunnel_params[i].attr_id) {
          case (DELL_IF_IF_INTERFACES_INTERFACE_REMOTE_ENDPOINT_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(un.addr)){
                  NDI_IDBR_LOG_ERROR("Invalid Remote IP- size mismatch addr %d vs %d.",
                    sizeof(un.addr), tunnel_params[i].vlen);
                  return STD_ERR(NPU, FAIL, 0);

              }
              memcpy(&remote_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
          case (DELL_IF_IF_INTERFACES_INTERFACE_SOURCE_IP_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(un.addr)) {
                  NDI_IDBR_LOG_ERROR("Invalid SRC IP- size mismatch addr %d vs %d.",
                    sizeof(un.addr), tunnel_params[i].vlen);
                   return STD_ERR(NPU, FAIL, 0);
              }
              memcpy(&local_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
          default:
          {
              NDI_IDBR_LOG_ERROR("Remove endpt Unsupported attr %d", tunnel_params[i].attr_id);
              break;
          }
       }
    }
    char buff[HAL_INET6_TEXT_LEN + 1];
    std_ip_to_string((const hal_ip_addr_t*) &remote_src_ip, buff, HAL_INET6_TEXT_LEN);
    NDI_IDBR_LOG_TRACE("nas_ndi_remove_remote_endpoint: Remote ip %s with br  %llu \n", buff, bridge_oid);
    /* NAS to check if a Tunnel Object with correct
     * Termination entry exists for this Remote end-point */
    std_mutex_simple_lock_guard lock_t(ndi_tun_mutex_lock());
    if (has_tunnel_obj((const hal_ip_addr_t *)&remote_src_ip, (const hal_ip_addr_t *)&local_src_ip) == false) {
        NDI_IDBR_LOG_ERROR("No tunnel entry for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG , 0);
    }
    TunnelObj *obj = get_tunnel_obj((const hal_ip_addr_t*)&remote_src_ip, (const hal_ip_addr_t*) &local_src_ip);
    if (obj == NULL) {
        NDI_IDBR_LOG_ERROR("No tunnel obj for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG, 0);
    }
    vni_s_oid_map_t vni_tun_oid;
    sai_object_id_t tun_bridge_port_oid;
    if (obj->get_bridge_port(bridge_oid, &vni_tun_oid ) != true) {
        tun_bridge_port_oid = vni_tun_oid.oid;
    }
    tun_bridge_port_oid = vni_tun_oid.oid;
    if (ndi_1d_bridge_tunnel_delete(npu_id, tun_bridge_port_oid) != STD_ERR_OK) {
        NDI_IDBR_LOG_ERROR("failed to remove tunnel bridge port for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG, 0);
    }
    if (obj->remove_tunnel_bridge_port(bridge_oid) != true) {
        NDI_IDBR_LOG_ERROR("failed remove_bridge_port for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG, 0);
    }

    vni_s_oid_map_t map_val;
    memset(&map_val,0, sizeof(vni_s_oid_map_t));
    if (obj->get_encap_map_entry(bridge_oid, &map_val) != true) {
        NDI_IDBR_LOG_ERROR("failed remove_bridge_port for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG, 0);
    }

    encap_map_entry_oid = map_val.oid;

    memset(&map_val,0, sizeof(vni_s_oid_map_t));
    if (obj->get_decap_map_entry(bridge_oid, &map_val) != true) {
        NDI_IDBR_LOG_ERROR("failed remove_bridge_port for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG, 0);
    }
    decap_map_entry_oid = map_val.oid;
    /* TODO: Underlay needs to be updated , remove entry from ndi*/
    if ((ndi_remove_encap_tunnel_map_entry(npu_id, encap_map_entry_oid) != STD_ERR_OK) ||
                   (ndi_remove_encap_tunnel_map_entry(npu_id, decap_map_entry_oid) != STD_ERR_OK)) {
         NDI_IDBR_LOG_ERROR("Failed to remove encap or decap map entry for bridge %llu \n", bridge_oid);
         return STD_ERR(NPU, CFG, 0);
    }

    if ((obj->remove_decap_map_entry(bridge_oid) != true) || (obj->remove_encap_map_entry(bridge_oid) != true)) {
        NDI_IDBR_LOG_ERROR("failed remove_decap_map_entry for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG, 0);
    }

    /* NAS-MAC will update the remote MAC address in to SAI FDB for
     * this particular Dot1dBridge with the Tunnel BridgePort and
     * Remote MAC as Dest - sai_create_fdb_entry_fn() */
    if (obj->get_tunnel_bridge_ports_size() == 0) {
        NDI_IDBR_LOG_TRACE("Removing OBJ for remote ip %s with br  %llu \n", buff, bridge_oid);
        if (ndi_delete_basic_tunnel_entry(npu_id, obj, bridge_oid) != STD_ERR_OK) {
            NDI_IDBR_LOG_ERROR("Failed to delete basic tunnel entries for remote ip %s with br  %llu \n", buff, bridge_oid);
            return STD_ERR(NPU, CFG, 0);
        }
        remove_tunnel_obj(&remote_src_ip, &local_src_ip);
    }

    return  STD_ERR_OK;
}


t_std_error ndi_tunnel_bridge_port_stats_get(npu_id_t npu_id, nas_bridge_id_t bridge_oid, nas_com_id_value_t tunnel_params[], size_t tp_len, ndi_stat_id_t *ndi_stat_ids, uint64_t* stats_val, size_t len)
{
    hal_ip_addr_t local_src_ip, remote_src_ip;
    local_src_ip = remote_src_ip ={0};

    union {
        bool bol;
        uint32_t u32;
        hal_ip_addr_t  addr;
    } un;

    if (tp_len != 2) {
       NDI_IDBR_LOG_ERROR("Remove remote endpt recived invalid size recieved 2 vs %d", tp_len);
       return STD_ERR(NPU, FAIL, 0);
    }

    for (uint32_t i = 0 ;i < tp_len ; i++) {
       switch (tunnel_params[i].attr_id) {
          case (DELL_IF_IF_INTERFACES_INTERFACE_REMOTE_ENDPOINT_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(un.addr)){
                  NDI_IDBR_LOG_ERROR("Invalid Remote IP- size mismatch addr %d vs %d.",
                    sizeof(un.addr), tunnel_params[i].vlen);
                  return STD_ERR(NPU, FAIL, 0);

              }
              memcpy(&remote_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
          case (DELL_IF_IF_INTERFACES_INTERFACE_SOURCE_IP_ADDR):
          {
              if (tunnel_params[i].vlen != sizeof(un.addr)) {
                  NDI_IDBR_LOG_ERROR("Invalid SRC IP- size mismatch addr %d vs %d.",
                    sizeof(un.addr), tunnel_params[i].vlen);
                   return STD_ERR(NPU, FAIL, 0);
              }
              memcpy(&local_src_ip, tunnel_params[i].val, sizeof(hal_ip_addr_t));
              break;
          }
          default:
          {
              NDI_IDBR_LOG_ERROR("delete endpt Unsupported attr %d", tunnel_params[i].attr_id);
              break;
          }
       }
    }
    char buff[HAL_INET6_TEXT_LEN + 1];
    std_ip_to_string((const hal_ip_addr_t*) &remote_src_ip, buff, HAL_INET6_TEXT_LEN);
    NDI_IDBR_LOG_TRACE("ndi_tunnel_bridge_port_stats_get: Remote ip %s with br  %llu \n", buff, bridge_oid);
    /* NAS to check if a Tunnel Object with correct
     * Termination entry exists for this Remote end-point */
    std_mutex_simple_lock_guard lock_t(ndi_tun_mutex_lock());
    if (has_tunnel_obj((const hal_ip_addr_t *)&remote_src_ip, (const hal_ip_addr_t *)&local_src_ip) == false) {
        NDI_IDBR_LOG_ERROR("No tunnel entry for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG, 0);
    }
    TunnelObj *obj = get_tunnel_obj((const hal_ip_addr_t*)&remote_src_ip, (const hal_ip_addr_t*) &local_src_ip);
    if (obj == NULL) {
        NDI_IDBR_LOG_ERROR("No tunnel obj for bridge %llu \n", bridge_oid);
        return STD_ERR(NPU, CFG, 0);
    }
    vni_s_oid_map_t vni_tun_oid;
    sai_object_id_t tun_bridge_port_oid;
    if (obj->get_bridge_port(bridge_oid, &vni_tun_oid ) != true) {
        tun_bridge_port_oid = vni_tun_oid.oid;
    }
    tun_bridge_port_oid = vni_tun_oid.oid;
    return ndi_bridge_port_tunnel_stats_get(npu_id, tun_bridge_port_oid, ndi_stat_ids, stats_val, len);
}


t_std_error ndi_create_underlay_rif(npu_id_t npu, ndi_obj_id_t *underlay_rif_oid, ndi_obj_id_t vr_oid)
{
    sai_attribute_t rif_attr_list[2];
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    rif_attr_list[0].id = SAI_ROUTER_INTERFACE_ATTR_TYPE;
    rif_attr_list[0].value.s32 = SAI_ROUTER_INTERFACE_TYPE_LOOPBACK;

    rif_attr_list[1].id = SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID;
    rif_attr_list[1].value.oid = vr_oid;

    if((sai_ret = ndi_rif_api_get(ndi_db_ptr)->create_router_interface(underlay_rif_oid,
                                                                       ndi_switch_id_get(),
                                                                       2, rif_attr_list)) != SAI_STATUS_SUCCESS) {
        NDI_IDBR_LOG_ERROR("SAI FAILURE: create_router_interface ");
        return STD_ERR(NPU, CFG, sai_ret);
    }
    return STD_ERR_OK;
}

}
