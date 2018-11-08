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
 * filename: nas_ndi_route.c
 */

#include <stdio.h>
#include <string.h>
#include "std_error_codes.h"
#include "std_assert.h"
#include "std_ip_utils.h"
#include "ds_common_types.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_int.h"
#include "nas_ndi_route.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_map.h"
#include "sai.h"
#include "saistatus.h"
#include "saitypes.h"

/* TODO: To be removed once the SAI Switch id changes are merged */
sai_object_id_t g_nas_ndi_switch_id = 0;

/*  NDI Route/Neighbor specific APIs  */

static inline  sai_route_api_t *ndi_route_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_route_api_tbl);
}

static inline  sai_neighbor_api_t *ndi_neighbor_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_neighbor_api_tbl);
}

static inline  sai_next_hop_api_t *ndi_next_hop_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_next_hop_api_tbl);
}

static inline void ndi_sai_ip_address_copy(sai_ip_address_t *sai_ip_addr,const hal_ip_addr_t *ip_addr)
{
    if (STD_IP_IS_AFINDEX_V4(ip_addr->af_index)) {
        sai_ip_addr->addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        sai_ip_addr->addr.ip4 = ip_addr->u.v4_addr;
    } else {
        sai_ip_addr->addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        memcpy (sai_ip_addr->addr.ip6, ip_addr->u.v6_addr, sizeof (sai_ip6_t));
    }
}

static void ndi_route_params_copy(sai_route_entry_t *p_sai_route,
                                  ndi_route_t *p_route_entry)
{
    sai_ip_prefix_t *p_ip_prefix = NULL;
    hal_ip_addr_t    ip_mask;
    uint32_t         af_index;

    p_sai_route->vr_id = p_route_entry->vrf_id;

    p_ip_prefix = &p_sai_route->destination;

    af_index = p_route_entry->prefix.af_index;
    p_ip_prefix->addr_family = (STD_IP_IS_AFINDEX_V4(af_index))?
                                SAI_IP_ADDR_FAMILY_IPV4:SAI_IP_ADDR_FAMILY_IPV6;

    if (STD_IP_IS_AFINDEX_V4(af_index)) {
        p_ip_prefix->addr.ip4 = p_route_entry->prefix.u.v4_addr;
    } else {
        memcpy (p_ip_prefix->addr.ip6, p_route_entry->prefix.u.v6_addr, sizeof (sai_ip6_t));
    }

    std_ip_get_mask_from_prefix_len (af_index, p_route_entry->mask_len, &ip_mask);

    if (STD_IP_IS_AFINDEX_V4(af_index)) {
        //SAI expects mask in network-byte order.
        p_ip_prefix->mask.ip4 = htonl(ip_mask.u.v4_addr);
    } else {
        memcpy (p_ip_prefix->mask.ip6, ip_mask.u.v6_addr, sizeof (sai_ip6_t));
    }
    p_sai_route->switch_id = ndi_switch_id_get();
    return;
}

static sai_packet_action_t ndi_route_sai_action_get(ndi_route_action action)
{
    sai_packet_action_t sai_action;

    switch(action) {
        case NDI_ROUTE_PACKET_ACTION_FORWARD:
            sai_action = SAI_PACKET_ACTION_FORWARD;
            break;
        case NDI_ROUTE_PACKET_ACTION_TRAPCPU:
            sai_action = SAI_PACKET_ACTION_TRAP;
            break;
        case NDI_ROUTE_PACKET_ACTION_DROP:
            sai_action = SAI_PACKET_ACTION_DROP;
            break;
        case NDI_ROUTE_PACKET_ACTION_TRAPFORWARD:
            sai_action = SAI_PACKET_ACTION_LOG;
            break;
        default:
            sai_action = SAI_PACKET_ACTION_FORWARD;
            break;
    }
    return sai_action;
}

t_std_error ndi_route_add (ndi_route_t *p_route_entry)
{
    uint32_t                  attr_idx = 0;
    sai_status_t              sai_ret = SAI_STATUS_FAILURE;
    sai_route_entry_t sai_route;
    sai_attribute_t           sai_attr[NDI_MAX_ROUTE_ATTR];

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_route_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    ndi_route_params_copy(&sai_route, p_route_entry);

    sai_attr[attr_idx].value.s32 = ndi_route_sai_action_get(p_route_entry->action);
    sai_attr[attr_idx].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    attr_idx++;

    if(p_route_entry->nh_handle){
        sai_attr[attr_idx].value.oid = p_route_entry->nh_handle;
        /*
         * Attribute type is same for both ECMP or non-ECMP case
         */
        sai_attr[attr_idx].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
        attr_idx++;
    }
    if ((sai_ret = ndi_route_api_get(ndi_db_ptr)->create_route_entry(&sai_route, attr_idx, sai_attr))
                          != SAI_STATUS_SUCCESS) {
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_route_delete (ndi_route_t *p_route_entry)
{
    sai_status_t              sai_ret = SAI_STATUS_FAILURE;
    sai_route_entry_t sai_route;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_route_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    ndi_route_params_copy(&sai_route, p_route_entry);

    if ((sai_ret = ndi_route_api_get(ndi_db_ptr)->remove_route_entry(&sai_route))!= SAI_STATUS_SUCCESS){
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }
    return STD_ERR_OK;
}

t_std_error ndi_route_set_attribute (ndi_route_t *p_route_entry)
{
    sai_status_t              sai_ret = SAI_STATUS_FAILURE;
    sai_route_entry_t sai_route;
    sai_attribute_t           sai_attr;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_route_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    ndi_route_params_copy(&sai_route, p_route_entry);

    switch(p_route_entry->flags) {
        case NDI_ROUTE_L3_PACKET_ACTION:
            sai_attr.value.s32 = ndi_route_sai_action_get(p_route_entry->action);
            sai_attr.id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
            break;
        case NDI_ROUTE_L3_TRAP_PRIORITY:
            sai_attr.value.u8 = p_route_entry->priority;
            sai_attr.id = SAI_ROUTE_ENTRY_ATTR_TRAP_PRIORITY;
            break;
        case NDI_ROUTE_L3_NEXT_HOP_ID:
            sai_attr.value.oid = p_route_entry->nh_handle;
            sai_attr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
            break;
        case NDI_ROUTE_L3_ECMP:
            sai_attr.value.oid = p_route_entry->nh_handle;
            sai_attr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
            break;
        default:
            NDI_LOG_TRACE("NDI-ROUTE", "Invalid attribute");
            return STD_ERR(ROUTE, FAIL, 0);
    }

    if ((sai_ret = ndi_route_api_get(ndi_db_ptr)->set_route_entry_attribute(&sai_route, &sai_attr))
                          != SAI_STATUS_SUCCESS) {
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_route_next_hop_add (ndi_neighbor_t *p_nbr_entry, next_hop_id_t *nh_handle)
{
    uint32_t          attr_idx = 0;
    sai_status_t      sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t   sai_nh_id;
    sai_attribute_t   sai_attr[NDI_MAX_NEXT_HOP_ATTR];

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_nbr_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    sai_attr[attr_idx].value.s32 = SAI_NEXT_HOP_TYPE_IP;
    sai_attr[attr_idx].id = SAI_NEXT_HOP_ATTR_TYPE;
    attr_idx++;

    ndi_sai_ip_address_copy(&sai_attr[attr_idx].value.ipaddr, &p_nbr_entry->ip_addr);
    sai_attr[attr_idx].id = SAI_NEXT_HOP_ATTR_IP;
    attr_idx++;

    sai_attr[attr_idx].value.oid = p_nbr_entry->rif_id;
    sai_attr[attr_idx].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    attr_idx++;

    if ((sai_ret = ndi_next_hop_api_get(ndi_db_ptr)->create_next_hop(&sai_nh_id, g_nas_ndi_switch_id, attr_idx, sai_attr))
                          != SAI_STATUS_SUCCESS) {
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }

    *nh_handle = sai_nh_id;

    return STD_ERR_OK;
}

t_std_error ndi_route_next_hop_delete (npu_id_t npu_id, next_hop_id_t nh_handle)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    if ((sai_ret = ndi_next_hop_api_get(ndi_db_ptr)->remove_next_hop(nh_handle))
                          != SAI_STATUS_SUCCESS) {
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }
    return STD_ERR_OK;
}

t_std_error ndi_route_neighbor_add (ndi_neighbor_t *p_nbr_entry)
{
    uint32_t             attr_idx = 0;
    sai_status_t         sai_ret = SAI_STATUS_FAILURE;
    sai_neighbor_entry_t sai_nbr_entry;
    sai_attribute_t      sai_attr[NDI_MAX_NEIGHBOR_ATTR];

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_nbr_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    sai_nbr_entry.vr_id = p_nbr_entry->vrf_id;
    sai_nbr_entry.rif_id = p_nbr_entry->rif_id;
    ndi_sai_ip_address_copy(&sai_nbr_entry.ip_address, &p_nbr_entry->ip_addr);

    memcpy (sai_attr[attr_idx].value.mac, p_nbr_entry->egress_data.neighbor_mac,
            HAL_MAC_ADDR_LEN);
    sai_attr[attr_idx].id = SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS;
    attr_idx++;

    sai_attr[attr_idx].value.s32 = ndi_route_sai_action_get(p_nbr_entry->action);
    sai_attr[attr_idx].id = SAI_NEIGHBOR_ENTRY_ATTR_PACKET_ACTION;
    attr_idx++;

    /* If state NDI_NEIGHBOR_ENTRY_NO_HOST_ROUTE, dont program the neighbor in the host table */
    if (p_nbr_entry->state == NDI_NEIGHBOR_ENTRY_NO_HOST_ROUTE) {
        sai_attr[attr_idx].value.s32 = true;
        sai_attr[attr_idx].id = SAI_NEIGHBOR_ENTRY_ATTR_NO_HOST_ROUTE;
        attr_idx++;
    }

    if ((sai_ret = ndi_neighbor_api_get(ndi_db_ptr)->create_neighbor_entry(&sai_nbr_entry,
                                                     attr_idx, sai_attr))!= SAI_STATUS_SUCCESS) {
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_route_neighbor_delete (ndi_neighbor_t *p_nbr_entry)
{
    sai_status_t         sai_ret = SAI_STATUS_FAILURE;
    sai_neighbor_entry_t sai_nbr_entry;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_nbr_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    sai_nbr_entry.vr_id = p_nbr_entry->vrf_id;
    sai_nbr_entry.rif_id = p_nbr_entry->rif_id;
    ndi_sai_ip_address_copy(&sai_nbr_entry.ip_address, &p_nbr_entry->ip_addr);

    if ((sai_ret = ndi_neighbor_api_get(ndi_db_ptr)->remove_neighbor_entry(&sai_nbr_entry))
                                        != SAI_STATUS_SUCCESS) {
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}


/*
 * NAS NDI Nexthop Group APIS for ECMP Functionality
 */
static inline  sai_next_hop_group_api_t *ndi_next_hop_group_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_next_hop_group_api_tbl);

}

static t_std_error ndi_route_nh_grp_members_create (nas_ndi_db_t    *ndi_db_ptr,
                                                    sai_object_id_t  nh_grp_oid,
                                                    uint32_t         nh_count,
                                                    sai_object_id_t *nh_list)
{
    uint32_t           attr_idx;
    uint32_t           i;
    sai_status_t       sai_rc = SAI_STATUS_SUCCESS;
    t_std_error        ndi_rc = STD_ERR_OK;
    sai_attribute_t    sai_attr [NDI_MAX_GROUP_NEXT_HOP_MEMBER_ATTR];
    nas_ndi_map_data_t data [NDI_MAX_NH_ENTRIES_PER_GROUP];
    sai_object_id_t    member_oid;
    nas_ndi_map_key_t  key;
    nas_ndi_map_val_t  value;

    memset (&data, 0, sizeof (data));

    for (i = 0; i < nh_count; i++) {
        attr_idx = 0;
        sai_attr[attr_idx].value.oid = nh_grp_oid;
        sai_attr[attr_idx].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID;
        attr_idx++;

        sai_attr[attr_idx].value.oid = nh_list[i];
        sai_attr[attr_idx].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID;
        attr_idx++;

        sai_rc = ndi_next_hop_group_api_get(ndi_db_ptr)->
            create_next_hop_group_member (&member_oid,
                    ndi_switch_id_get(),
                    attr_idx, sai_attr);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }

        data [i].val1 = nh_list[i];
        data [i].val2 = member_oid;
    }

    if (sai_rc != SAI_STATUS_SUCCESS) {
        /* on failure, remove the members from the NH group */
        while (i) {
            sai_rc = ndi_next_hop_group_api_get(ndi_db_ptr)->
                remove_next_hop_group_member (data [i-1].val2);
            --i;
        }

        return STD_ERR (ROUTE, FAIL, sai_rc);
    }

    memset (&key, 0, sizeof (key));
    key.type = NAS_NDI_MAP_TYPE_NH_GRP_MEMBER;
    key.id1  = nh_grp_oid;

    memset (&value, 0, sizeof (value));
    value.count = nh_count;
    value.data  = &data [0];

    ndi_rc = nas_ndi_map_insert (&key, &value);

    if (ndi_rc != STD_ERR_OK) {
        return ndi_rc;
    }

    return STD_ERR_OK;
}

static t_std_error
ndi_route_get_nh_member_oid (sai_object_id_t           nh_grp_oid,
                             uint32_t                  count,
                             nas_ndi_map_data_t       *in_out_data,
                             nas_ndi_map_val_filter_type_t  filter)
{
    t_std_error       ndi_ret = STD_ERR_OK;
    nas_ndi_map_key_t key;
    nas_ndi_map_val_t value;
    size_t            t_count = 0;
    unsigned int      iter=0,t_iter=0;

    memset (&key, 0, sizeof (key));
    key.type = NAS_NDI_MAP_TYPE_NH_GRP_MEMBER;
    key.id1  = nh_grp_oid;

    ndi_ret = nas_ndi_map_get_val_count (&key, &t_count);
    if (ndi_ret != STD_ERR_OK) {
        return (ndi_ret);
    }

    if(t_count > 0) {
        nas_ndi_map_data_t t_data[t_count];

        memset (&value, 0, sizeof (value));
        value.count = t_count;
        value.data  = t_data;

        ndi_ret = nas_ndi_map_get (&key, &value);

        if (ndi_ret != STD_ERR_OK) {
            return (ndi_ret);
        }

        for(iter = 0; iter < count; ++iter) {
            for(t_iter = 0; t_iter < t_count; ++t_iter) {
                if(filter == NAS_NDI_MAP_VAL_FILTER_VAL1) {
                    if(in_out_data[iter].val1 == t_data[t_iter].val1) {
                        in_out_data[iter].val2 = t_data[t_iter].val2;
                        t_data[t_iter].val1 = -1;
                        break;
                    }
                } else if(filter == NAS_NDI_MAP_VAL_FILTER_VAL2) {
                    if(in_out_data[iter].val2 == t_data[t_iter].val2) {
                        in_out_data[iter].val1 = t_data[t_iter].val1;
                        t_data[t_iter].val2 = -1;
                        break;
                    }
                }
            }
        }
    }

    return ndi_ret;
}

static t_std_error
nas_ndi_get_nh_id_from_member_oid (sai_object_id_t    nh_grp_id,
                                   uint32_t           nhop_count,
                                   sai_object_list_t *member_id_list,
                                   sai_object_id_t   *out_nhid_list)
{
    nas_ndi_map_data_t data [NDI_MAX_NH_ENTRIES_PER_GROUP];
    t_std_error        ndi_ret;
    uint32_t           i;

    memset (data, 0, sizeof (data));

    if (nhop_count > NDI_MAX_NH_ENTRIES_PER_GROUP) {
        return STD_ERR (ROUTE, TOOBIG, 0);
    }

    for (i = 0; i < nhop_count; i++) {
        data[i].val2 = member_id_list->list [i];
    }

    /*
     * data[i].val2 contains SAI NH member id. Retrieve NAS nhId, based on SAI
     * NH member id. So set the filter argument to NAS_NDI_MAP_VAL_FILTER_VAL2.
     */
    ndi_ret = ndi_route_get_nh_member_oid (nh_grp_id, nhop_count,
                                           data, NAS_NDI_MAP_VAL_FILTER_VAL2);

    if (ndi_ret != STD_ERR_OK) {
        return ndi_ret;
    }

    /* Copy the NAS nhId from 'val1' field. */
    for (i = 0; i < nhop_count; i++) {
        out_nhid_list [i] = data[i].val1;
    }

    return STD_ERR_OK;
}

static t_std_error
ndi_route_nh_grp_all_members_remove (nas_ndi_db_t    *ndi_db_ptr,
                                     sai_object_id_t  nh_grp_oid)
{
    uint32_t           i;
    nas_ndi_map_key_t  key;
    nas_ndi_map_val_t  value;
    sai_status_t       sai_rc = SAI_STATUS_FAILURE;
    t_std_error        ndi_rc = STD_ERR_OK;
    nas_ndi_map_data_t data[NDI_MAX_NH_ENTRIES_PER_GROUP];

    memset (&key, 0, sizeof (key));
    key.type = NAS_NDI_MAP_TYPE_NH_GRP_MEMBER;
    key.id1  = nh_grp_oid;

    memset (&data, 0, sizeof (data));
    memset (&value, 0, sizeof (value));
    value.count = NDI_MAX_NH_ENTRIES_PER_GROUP;
    value.data  = &data [0];

    ndi_rc = nas_ndi_map_get (&key, &value);

    if (ndi_rc != STD_ERR_OK) {
        return ndi_rc;
    }

    for (i = 0; i < value.count; i++) {
        sai_rc = ndi_next_hop_group_api_get(ndi_db_ptr)->
            remove_next_hop_group_member (value.data [i].val2);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            return STD_ERR (ROUTE, FAIL, sai_rc);
        }
    }

    ndi_rc = nas_ndi_map_delete (&key);

    if (ndi_rc != STD_ERR_OK) {
        return ndi_rc;
    }

    return STD_ERR_OK;
}

static t_std_error ndi_route_nh_grp_members_remove (nas_ndi_db_t    *ndi_db_ptr,
                                                    sai_object_id_t  nh_grp_oid,
                                                    uint32_t         nh_count,
                                                    nas_ndi_map_data_t *data)
{
    uint32_t          i;
    nas_ndi_map_key_t key;
    nas_ndi_map_val_filter_t filter;
    sai_status_t      sai_rc = SAI_STATUS_FAILURE;
    t_std_error       ndi_rc = STD_ERR_OK;

    if (nh_count > NDI_MAX_NH_ENTRIES_PER_GROUP) {
        return STD_ERR (ROUTE, TOOBIG, sai_rc);
    }

    memset (&key, 0, sizeof (key));
    key.type = NAS_NDI_MAP_TYPE_NH_GRP_MEMBER;
    key.id1  = nh_grp_oid;

    for (i = 0; i < nh_count; i++) {
        sai_rc = ndi_next_hop_group_api_get(ndi_db_ptr)->
            remove_next_hop_group_member (data[i].val2);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            return STD_ERR (ROUTE, FAIL, sai_rc);
        }

        memset (&filter, 0, sizeof (filter));
        filter.value.val1 = SAI_NULL_OBJECT_ID;
        filter.value.val2 = data[i].val2;
        filter.type = NAS_NDI_MAP_VAL_FILTER_VAL2;

        /*
         * nas_ndi_map_data_t.val1 contains NAS nhId.
         * nas_ndi_map_data_t.val2 contains SAI NH member Id.
         * 'value.data' contains both NAS nhId and SAI NH member Id.
         * So deleting the mapping using NAS_NDI_MAP_VAL_FILTER_VAL2
         * since val2 is unique.
         */
        ndi_rc = nas_ndi_map_delete_elements (&key, &filter);

        if (ndi_rc != STD_ERR_OK) {
            break;
        }
    }

    return ndi_rc;
}

t_std_error ndi_route_next_hop_group_create (ndi_nh_group_t *p_nh_group_entry,
                        next_hop_id_t *nh_group_handle)
{
    uint32_t          attr_idx = 0;
    sai_status_t      sai_ret = SAI_STATUS_FAILURE;
    t_std_error       ndi_ret = STD_ERR_OK;
    sai_object_id_t   sai_nh_group_id;
    sai_object_id_t   nexthops[NDI_MAX_NH_ENTRIES_PER_GROUP];
    sai_attribute_t   sai_attr[NDI_MAX_GROUP_NEXT_HOP_ATTR];
    uint32_t          nhop_count;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_nh_group_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    sai_attr[attr_idx].value.s32 = SAI_NEXT_HOP_GROUP_TYPE_ECMP;
    sai_attr[attr_idx].id = SAI_NEXT_HOP_GROUP_ATTR_TYPE;
    attr_idx++;

    if ((sai_ret = ndi_next_hop_group_api_get(ndi_db_ptr)->
            create_next_hop_group(&sai_nh_group_id, g_nas_ndi_switch_id, attr_idx, sai_attr))
            != SAI_STATUS_SUCCESS) {
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }

    nhop_count = p_nh_group_entry->nhop_count;
    /*
     * Add the nexthop id list to sai_next_hop_list_t
     */
    if (nhop_count == 0) {
        return STD_ERR_OK;
    }

    int i;
    for (i = 0; i <nhop_count; i++) {
        nexthops[i] = p_nh_group_entry->nh_list[i].id;
    }

    ndi_ret = ndi_route_nh_grp_members_create (ndi_db_ptr, sai_nh_group_id,
                                               nhop_count, nexthops);
    if (ndi_ret != STD_ERR_OK) {
        sai_ret = ndi_next_hop_group_api_get(ndi_db_ptr)->
                       remove_next_hop_group(sai_nh_group_id);
        return ndi_ret;
    }

    *nh_group_handle = sai_nh_group_id;

    return STD_ERR_OK;
}

t_std_error ndi_route_next_hop_group_delete (npu_id_t npu_id,
                                    next_hop_id_t nh_handle)
{
    sai_status_t    sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t next_hop_group_id;
    t_std_error     ndi_ret = STD_ERR_OK;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    next_hop_group_id = nh_handle;

    ndi_ret = ndi_route_nh_grp_all_members_remove (ndi_db_ptr,
                                                   next_hop_group_id);
    if (ndi_ret != STD_ERR_OK) {
        return ndi_ret;
    }

    if ((sai_ret = ndi_next_hop_group_api_get(ndi_db_ptr)->
        remove_next_hop_group(next_hop_group_id)) != SAI_STATUS_SUCCESS) {
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }
    return STD_ERR_OK;
}

t_std_error ndi_route_set_next_hop_group_attribute (ndi_nh_group_t *p_nh_group_entry,
                                        next_hop_id_t nh_group_handle)
{
    /* Currently all Next Group Attributes are not settable */
    return (STD_ERR (ROUTE, FAIL, SAI_STATUS_FAILURE));
}

t_std_error ndi_route_get_next_hop_group_attribute (ndi_nh_group_t *p_nh_group_entry,
                                                    next_hop_id_t nh_group_handle)
{
    sai_status_t      sai_ret = SAI_STATUS_FAILURE;
    uint32_t          attr_count= 1;
    sai_attribute_t   sai_attr[NDI_MAX_GROUP_NEXT_HOP_ATTR];
    sai_object_id_t   sai_nh_group_id;
    unsigned int      attr_idx = 0;
    sai_object_id_t   nhid_list [NDI_MAX_NH_ENTRIES_PER_GROUP];
    sai_object_id_t  *next_hops;
    uint32_t          nhop_count;
    t_std_error       ndi_ret = STD_ERR_OK;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_nh_group_entry->npu_id);

    sai_nh_group_id  = nh_group_handle;
    if ((sai_ret = ndi_next_hop_group_api_get(ndi_db_ptr)->
            get_next_hop_group_attribute(sai_nh_group_id,attr_count, sai_attr))
            != SAI_STATUS_SUCCESS) {
        NDI_LOG_TRACE("NDI-ROUTE-NHGROUP",
                      "get attribute failed");
        return STD_ERR(ROUTE, FAIL, sai_ret);
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(sai_attr[attr_idx].id) {
            case SAI_NEXT_HOP_GROUP_ATTR_NEXT_HOP_COUNT:
                p_nh_group_entry->nhop_count = sai_attr[attr_idx].value.u32;
                break;
            case SAI_NEXT_HOP_GROUP_ATTR_TYPE:
                if (sai_attr[attr_idx].value.u32 == SAI_NEXT_HOP_GROUP_TYPE_ECMP) {
                    p_nh_group_entry->group_type = NDI_ROUTE_NH_GROUP_TYPE_ECMP;
                }
                /* @TODO WECMP case */
                break;
            case SAI_NEXT_HOP_GROUP_ATTR_NEXT_HOP_MEMBER_LIST:
                /*
                 * Get the nexthop id list from sai_next_hop_list_t
                 */
                nhop_count = sai_attr[attr_idx].value.objlist.count;
                if (nhop_count > NDI_MAX_NH_ENTRIES_PER_GROUP) {
                    NDI_LOG_TRACE("NDI-ROUTE-NHGROUP",
                                 "Invalid get nhlist attribute");
                    return STD_ERR(ROUTE, FAIL, sai_ret);
                }

                memset (nhid_list, 0, sizeof (nhid_list));
                ndi_ret = nas_ndi_get_nh_id_from_member_oid (sai_nh_group_id,
                                                             nhop_count,
                                                             &sai_attr[attr_idx]
                                                             .value.objlist,
                                                             nhid_list);

                if (ndi_ret != STD_ERR_OK) {
                    NDI_LOG_TRACE("NDI-ROUTE-NHGROUP",
                                  "Failed to convert Next Hop Member Oid to NH Id");
                    return ndi_ret;
                }

                /* nexthops*/
                next_hops = nhid_list;
                /*
                 * Copy nexthop-id to list
                 */
                int i;
                for (i = 0; i <nhop_count; i++) {
                    p_nh_group_entry->nh_list[i].id = (*(next_hops+i));
                }

                break;
            default:
                NDI_LOG_TRACE("NDI-ROUTE-NHGROUP",
                             "Invalid get attribute");
                return STD_ERR(ROUTE, FAIL, 0);
        }
    }

    return STD_ERR_OK;
}

/*
 *  ndi_route_add_next_hop_to_group: t all new NH and new NH count
 */
t_std_error ndi_route_add_next_hop_to_group (ndi_nh_group_t *p_nh_group_entry,
                                             next_hop_id_t nh_group_handle)
{
    t_std_error       ndi_ret = STD_ERR_OK;
    sai_object_id_t   next_hop_group_id;
    sai_object_id_t   nexthops[NDI_MAX_NH_ENTRIES_PER_GROUP];
    uint32_t nhop_count;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_nh_group_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);


    /* Set all new NH count */
    nhop_count = p_nh_group_entry->nhop_count;
    /*
     * Add the nexthop id list to SAI nexthps list
     */
    if (nhop_count == 0) {
        return STD_ERR(ROUTE, FAIL, 0);
    }
    /*
     * Copy nexthop-id to list
     */
    int i;
    for (i = 0; i <nhop_count; i++) {
        nexthops[i] = p_nh_group_entry->nh_list[i].id;
    }

    next_hop_group_id  = nh_group_handle;

    ndi_ret = ndi_route_nh_grp_members_create (ndi_db_ptr, next_hop_group_id,
                                               nhop_count, nexthops);

    return ndi_ret;
}

t_std_error ndi_route_delete_next_hop_from_group (ndi_nh_group_t *p_nh_group_entry,
                            next_hop_id_t nh_group_handle)
{
    sai_object_id_t    next_hop_group_id;
    nas_ndi_map_data_t data [NDI_MAX_NH_ENTRIES_PER_GROUP];
    t_std_error        ndi_ret;
    uint32_t           nhop_count;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(p_nh_group_entry->npu_id);
    STD_ASSERT(ndi_db_ptr != NULL);

    memset (data, 0, sizeof (data));
    nhop_count = p_nh_group_entry->nhop_count;
    /*
     * Add the nexthop id list to SAI nexthps list
     */
    if (nhop_count == 0) {
        return STD_ERR(ROUTE, FAIL, 0);
    }

    if (nhop_count > NDI_MAX_NH_ENTRIES_PER_GROUP) {
        return STD_ERR(ROUTE, NOMEM, 0);
    }

    /*
     * Copy nexthop-id to list
     */
    int i;
    for (i = 0; i <nhop_count; i++) {
        data[i].val1 = p_nh_group_entry->nh_list[i].id;
    }

    next_hop_group_id  = nh_group_handle;

    /*
     * data[i].val1 contains NAS nhId. Retrieve the SAI NH member id, based on
     * NAS nhId. So set the filter argument to NAS_NDI_MAP_VAL_FILTER_VAL1.
     */
    ndi_ret = ndi_route_get_nh_member_oid (next_hop_group_id, nhop_count,
                                           data, NAS_NDI_MAP_VAL_FILTER_VAL1);

    if (ndi_ret != STD_ERR_OK) {
        return ndi_ret;
    }

    ndi_ret = ndi_route_nh_grp_members_remove (ndi_db_ptr, next_hop_group_id,
                                               nhop_count, data);

    if (ndi_ret != STD_ERR_OK) {
        return ndi_ret;
    }

    return STD_ERR_OK;
}
