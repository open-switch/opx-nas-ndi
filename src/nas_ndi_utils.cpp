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
 * filename: nas_ndi_utils.c
 */

#include "std_error_codes.h"
#include "std_assert.h"
#include "ds_common_types.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_int.h"
#include "nas_ndi_utils.h"
#include "dell-interface.h"
#include "bridge-model.h"
#include "ietf-interfaces.h"
#include "tunnel.h"

#include "sai.h"
#include "saitypes.h"
#include "saiport.h"
#include "saivlan.h"
#include "saibridge.h"
#include "saitunnel.h"

#include <map>
#include <stdlib.h>
#include <stdio.h>

#include <set>
#include <sstream>
#include <vector>
#include <iostream>
#include <fstream>

#define NDI_SAI_PORT_OBJECT_TYPE_BITPOS     48
#define NDI_SAI_PORT_OBJECT_ID_BITPOS     0

#define NDI_SAI_PORT_OBJECT_ID_BITMASK       0x0000ffffffffffff
#define NDI_SAI_PORT_OBJECT_TYPE_BITMASK     0x0fff000000000000

static std::map<sai_switch_profile_id_t, size_t> profile_id_map;
static std::vector<std::map<std::string, std::string>> profile_map;
static std::vector<std::map<std::string, std::string>::iterator> profile_iter;

static nas_ndi_db_t  *g_nas_ndi_db_tbl = NULL;
/*  @todo as mentioned in the Current SAI spec supports only one NPU. Use npu_id  of 0 always */
static npu_id_t g_nas_ndi_npu_id = 0;

/*  Alloc memory for NDI DB for all NPUs  */
t_std_error ndi_db_global_tbl_alloc(size_t max_npu)
{
    g_nas_ndi_db_tbl = (nas_ndi_db_t *)calloc(max_npu, sizeof(nas_ndi_db_t));
    if (g_nas_ndi_db_tbl == NULL) {
        return (STD_ERR(NPU, NOMEM, 0));
    }
    return(STD_ERR_OK);
}

npu_id_t ndi_npu_id_get(void)
{
    return g_nas_ndi_npu_id;
}

nas_ndi_db_t *ndi_db_ptr_get(npu_id_t npu_id)
{
    return (g_nas_ndi_db_tbl + npu_id);
}

ndi_switch_oper_status_t ndi_oper_status_translate(sai_switch_oper_status_t oper_status)
{
    switch(oper_status) {
        case SAI_SWITCH_OPER_STATUS_UNKNOWN:
            return(NDI_SWITCH_OPER_UNKNOWN);

        case SAI_SWITCH_OPER_STATUS_UP:
            return(NDI_SWITCH_OPER_UP);

        case SAI_SWITCH_OPER_STATUS_DOWN:
            return(NDI_SWITCH_OPER_DOWN);

        case SAI_SWITCH_OPER_STATUS_FAILED:
            return(NDI_SWITCH_OPER_FAILED);

        default:
            break;
    }
    return(NDI_SWITCH_OPER_UNKNOWN);
}

t_std_error ndi_sai_vlan_id_get(npu_id_t npu_id, hal_vlan_id_t vlan_id, sai_vlan_id_t *sai_vlan)
{
    /*  @todo TODO At present it is same */
    *sai_vlan = vlan_id;
    return(STD_ERR_OK);
}


bool ndi_port_to_sai_oid(ndi_port_t * ndi_port, sai_object_id_t *oid){
    if(ndi_sai_port_id_get( ndi_port->npu_id,ndi_port->npu_port,oid)!= STD_ERR_OK){
        NDI_LOG_ERROR("NAS-NDI-UTILS","Failed to get oid for npu %d and port %d",
                              ndi_port->npu_id,ndi_port->npu_port);
        return false;
    }
    return true;
}


bool ndi_to_sai_if_stats(ndi_stat_id_t ndi_id, sai_port_stat_t * sai_id){
    static const auto ndi_to_sai_if_stat_ids = new std::map<ndi_stat_id_t ,sai_port_stat_t>
    {
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IF_OUT_QLEN  ,SAI_PORT_STAT_IF_OUT_QLEN },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_DROP_EVENTS  ,SAI_PORT_STAT_ETHER_STATS_DROP_EVENTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_MULTICAST_PKTS  ,SAI_PORT_STAT_ETHER_STATS_MULTICAST_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_BROADCAST_PKTS  ,SAI_PORT_STAT_ETHER_STATS_BROADCAST_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_UNDERSIZE_PKTS  ,SAI_PORT_STAT_ETHER_STATS_UNDERSIZE_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_FRAGMENTS  ,SAI_PORT_STAT_ETHER_STATS_FRAGMENTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OVERSIZE_PKTS  ,SAI_PORT_STAT_ETHER_STATS_OVERSIZE_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_RX_OVERSIZE_PKTS  ,SAI_PORT_STAT_ETHER_RX_OVERSIZE_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_TX_OVERSIZE_PKTS  ,SAI_PORT_STAT_ETHER_TX_OVERSIZE_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_JABBERS  ,SAI_PORT_STAT_ETHER_STATS_JABBERS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OCTETS  ,SAI_PORT_STAT_ETHER_STATS_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_PKTS  ,SAI_PORT_STAT_ETHER_STATS_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_COLLISIONS  ,SAI_PORT_STAT_ETHER_STATS_COLLISIONS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_CRC_ALIGN_ERRORS  ,SAI_PORT_STAT_ETHER_STATS_CRC_ALIGN_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_TX_NO_ERRORS  ,SAI_PORT_STAT_ETHER_STATS_TX_NO_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_RX_NO_ERRORS  ,SAI_PORT_STAT_ETHER_STATS_RX_NO_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_GREEN_DISCARD_DROPPED_PACKETS  ,SAI_PORT_STAT_GREEN_WRED_DROPPED_PACKETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_GREEN_DISCARD_DROPPED_BYTES  ,SAI_PORT_STAT_GREEN_WRED_DROPPED_BYTES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_YELLOW_DISCARD_DROPPED_PACKETS  ,SAI_PORT_STAT_YELLOW_WRED_DROPPED_PACKETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_YELLOW_DISCARD_DROPPED_BYTES  ,SAI_PORT_STAT_YELLOW_WRED_DROPPED_BYTES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RED_DISCARD_DROPPED_PACKETS  ,SAI_PORT_STAT_RED_WRED_DROPPED_PACKETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RED_DISCARD_DROPPED_BYTES  ,SAI_PORT_STAT_RED_WRED_DROPPED_BYTES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_DISCARD_DROPPED_PACKETS  ,SAI_PORT_STAT_WRED_DROPPED_PACKETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_DISCARD_DROPPED_BYTES  ,SAI_PORT_STAT_WRED_DROPPED_BYTES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ECN_MARKED_PACKETS ,SAI_PORT_STAT_ECN_MARKED_PACKETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_64_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_64_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_65_TO_127_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_65_TO_127_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_128_TO_255_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_128_TO_255_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_256_TO_511_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_256_TO_511_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_512_TO_1023_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_512_TO_1023_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_1024_TO_1518_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_1024_TO_1518_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_1519_TO_2047_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_1519_TO_2047_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_2048_TO_4095_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_2048_TO_4095_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_4096_TO_9216_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_4096_TO_9216_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_IN_PKTS_9217_TO_16383_OCTETS  ,SAI_PORT_STAT_ETHER_IN_PKTS_9217_TO_16383_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_64_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_64_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_65_TO_127_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_65_TO_127_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_128_TO_255_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_128_TO_255_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_256_TO_511_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_256_TO_511_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_512_TO_1023_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_512_TO_1023_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_1024_TO_1518_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_1024_TO_1518_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_1519_TO_2047_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_1519_TO_2047_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_2048_TO_4095_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_2048_TO_4095_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_4096_TO_9216_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_4096_TO_9216_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ETHER_OUT_PKTS_9217_TO_16383_OCTETS  ,SAI_PORT_STAT_ETHER_OUT_PKTS_9217_TO_16383_OCTETS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_CURRENT_OCCUPANCY_BYTES ,SAI_PORT_STAT_IN_CURR_OCCUPANCY_BYTES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_WATERMARK_BYTES , SAI_PORT_STAT_IN_WATERMARK_BYTES},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_SHARED_CURRENT_OCCUPANCY_BYTES ,SAI_PORT_STAT_IN_SHARED_CURR_OCCUPANCY_BYTES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_SHARED_WATERMARK_BYTES , SAI_PORT_STAT_IN_SHARED_WATERMARK_BYTES},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_XOFF_ROOM_CURRENT_OCCUPANCY_BYTES , SAI_PORT_STAT_IN_XOFF_ROOM_CURR_OCCUPANCY_BYTES},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PAUSE_RX_PKTS , SAI_PORT_STAT_PAUSE_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PAUSE_TX_PKTS , SAI_PORT_STAT_PAUSE_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_0_RX_PKTS , SAI_PORT_STAT_PFC_0_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_0_TX_PKTS , SAI_PORT_STAT_PFC_0_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_1_RX_PKTS , SAI_PORT_STAT_PFC_1_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_1_TX_PKTS , SAI_PORT_STAT_PFC_1_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_2_RX_PKTS , SAI_PORT_STAT_PFC_2_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_2_TX_PKTS , SAI_PORT_STAT_PFC_2_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_3_RX_PKTS , SAI_PORT_STAT_PFC_3_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_3_TX_PKTS , SAI_PORT_STAT_PFC_3_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_4_RX_PKTS , SAI_PORT_STAT_PFC_4_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_4_TX_PKTS , SAI_PORT_STAT_PFC_4_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_5_RX_PKTS , SAI_PORT_STAT_PFC_5_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_5_TX_PKTS , SAI_PORT_STAT_PFC_5_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_6_RX_PKTS , SAI_PORT_STAT_PFC_6_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_6_TX_PKTS , SAI_PORT_STAT_PFC_6_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_7_RX_PKTS , SAI_PORT_STAT_PFC_7_RX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_PFC_7_TX_PKTS , SAI_PORT_STAT_PFC_7_TX_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_LPI_COUNT , SAI_PORT_STAT_EEE_RX_EVENT_COUNT},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_LPI_DURATION , SAI_PORT_STAT_EEE_RX_DURATION},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TX_LPI_COUNT, SAI_PORT_STAT_EEE_TX_EVENT_COUNT},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TX_LPI_DURATION, SAI_PORT_STAT_EEE_TX_DURATION},
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_OCTETS  ,SAI_PORT_STAT_IF_IN_OCTETS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_UNICAST_PKTS  ,SAI_PORT_STAT_IF_IN_UCAST_PKTS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_BROADCAST_PKTS  ,SAI_PORT_STAT_IF_IN_BROADCAST_PKTS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_MULTICAST_PKTS  ,SAI_PORT_STAT_IF_IN_MULTICAST_PKTS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_DISCARDS  ,SAI_PORT_STAT_IF_IN_DISCARDS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_ERRORS  ,SAI_PORT_STAT_IF_IN_ERRORS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_UNKNOWN_PROTOS  ,SAI_PORT_STAT_IF_IN_UNKNOWN_PROTOS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_OCTETS  ,SAI_PORT_STAT_IF_OUT_OCTETS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_UNICAST_PKTS  ,SAI_PORT_STAT_IF_OUT_UCAST_PKTS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_BROADCAST_PKTS  ,SAI_PORT_STAT_IF_OUT_BROADCAST_PKTS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_MULTICAST_PKTS  ,SAI_PORT_STAT_IF_OUT_MULTICAST_PKTS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_DISCARDS  ,SAI_PORT_STAT_IF_OUT_DISCARDS },
        { IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_ERRORS  ,SAI_PORT_STAT_IF_OUT_ERRORS },

        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ALIGNMENT_ERRORS, SAI_PORT_STAT_DOT3_STATS_ALIGNMENT_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_FCS_ERRORS, SAI_PORT_STAT_DOT3_STATS_FCS_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_SINGLE_COLLISION_FRAMES, SAI_PORT_STAT_DOT3_STATS_SINGLE_COLLISION_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_MULTIPLE_COLISION_FRAMES, SAI_PORT_STAT_DOT3_STATS_MULTIPLE_COLLISION_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_SQE_TEST_ERRORS, SAI_PORT_STAT_DOT3_STATS_SQE_TEST_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_DEFERRED_TRANSMISSIONS, SAI_PORT_STAT_DOT3_STATS_DEFERRED_TRANSMISSIONS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_LATE_COLLISIONS, SAI_PORT_STAT_DOT3_STATS_LATE_COLLISIONS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_EXCESSIVE_COLLISIONS, SAI_PORT_STAT_DOT3_STATS_EXCESSIVE_COLLISIONS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_INTERNAL_MAC_TRANSMIT_ERRORS, SAI_PORT_STAT_DOT3_STATS_INTERNAL_MAC_TRANSMIT_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_CARRIER_SENSE_ERRORS, SAI_PORT_STAT_DOT3_STATS_CARRIER_SENSE_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_FRAME_TOO_LONG, SAI_PORT_STAT_DOT3_STATS_FRAME_TOO_LONGS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_INTERNAL_MAC_RECEIVE_ERRORS, SAI_PORT_STAT_DOT3_STATS_INTERNAL_MAC_RECEIVE_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_SYMBOL_ERRORS, SAI_PORT_STAT_DOT3_STATS_SYMBOL_ERRORS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_UNKNOWN_OPCODES, SAI_PORT_STAT_DOT3_CONTROL_IN_UNKNOWN_OPCODES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IP_IN_RECEIVES, SAI_PORT_STAT_IP_IN_RECEIVES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IP_IN_HDR_ERRORS, SAI_PORT_STAT_IP_IN_HDR_ERRORS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IP_IN_DISCARDS, SAI_PORT_STAT_IP_IN_DISCARDS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IP_IN_FORW_DATAGRAMS, SAI_PORT_STAT_IP_FORW_DATAGRAMS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_RECEIVES, SAI_PORT_STAT_IPV6_IN_RECEIVES},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_HDR_ERRORS, SAI_PORT_STAT_IPV6_IN_HDR_ERRORS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_ADDR_ERRORS, SAI_PORT_STAT_IPV6_IN_ADDR_ERRORS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_DISCARDS, SAI_PORT_STAT_IPV6_IN_DISCARDS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_OUT_FORW_DATAGRAMS, SAI_PORT_STAT_IPV6_OUT_FORW_DATAGRAMS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_OUT_DISCARDS, SAI_PORT_STAT_IPV6_OUT_DISCARDS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_MCAST_PKTS, SAI_PORT_STAT_IPV6_IN_MCAST_PKTS},
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_OUT_MCAST_PKTS, SAI_PORT_STAT_IPV6_OUT_MCAST_PKTS},
    };
    auto it = ndi_to_sai_if_stat_ids->find(ndi_id);
    if(it == ndi_to_sai_if_stat_ids->end() || (sai_id == NULL)){
        NDI_LOG_ERROR("NAS-NDI-UTILS","Failed to get the sai stat id for ndi id %lu ",ndi_id);
        return false;
    }
    *sai_id = it->second;
    return true;
}

bool ndi_to_sai_vlan_stats(ndi_stat_id_t ndi_id, sai_vlan_stat_t * sai_id){
    static const auto ndi_to_sai_vlan_stat_ids = new std::map<ndi_stat_id_t,sai_vlan_stat_t>
    {
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_OCTETS ,SAI_VLAN_STAT_IN_OCTETS },
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_UNICAST_PKTS ,SAI_VLAN_STAT_IN_UCAST_PKTS },
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_DISCARDS ,SAI_VLAN_STAT_IN_DISCARDS},
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_ERRORS ,SAI_VLAN_STAT_IN_ERRORS},
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_UNKNOWN_PROTOS ,SAI_VLAN_STAT_IN_UNKNOWN_PROTOS},
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_OCTETS ,SAI_VLAN_STAT_OUT_OCTETS},
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_UNICAST_PKTS ,SAI_VLAN_STAT_OUT_UCAST_PKTS },
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_DISCARDS ,SAI_VLAN_STAT_OUT_DISCARDS },
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_ERRORS ,SAI_VLAN_STAT_OUT_ERRORS },
        {  DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IF_OUT_QLEN ,SAI_VLAN_STAT_OUT_QLEN },
        {  DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_PKTS ,SAI_VLAN_STAT_IN_PACKETS },
        {  DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_PKTS ,SAI_VLAN_STAT_OUT_PACKETS },
    };

    auto it = ndi_to_sai_vlan_stat_ids->find(ndi_id);
    if(it == ndi_to_sai_vlan_stat_ids->end() || (sai_id == NULL)){
        NDI_LOG_ERROR("NAS-NDI-UTILS","Failed to get the sai stat id for ndi id %lu",ndi_id);
        return false;
    }
    *sai_id = it->second;
    return true;
}

bool ndi_to_sai_bridge_port_stats(ndi_stat_id_t ndi_id, sai_bridge_port_stat_t *sai_id){
    static const auto ndi_to_sai_bridge_port_stat_ids = new std::map<ndi_stat_id_t, sai_bridge_port_stat_t>
    {
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_OCTETS ,SAI_BRIDGE_PORT_STAT_IN_OCTETS },
        {  DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_PKTS ,SAI_BRIDGE_PORT_STAT_IN_PACKETS },
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_OCTETS ,SAI_BRIDGE_PORT_STAT_OUT_OCTETS},
        {  DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_PKTS ,SAI_BRIDGE_PORT_STAT_OUT_PACKETS },
    };

    auto it = ndi_to_sai_bridge_port_stat_ids->find(ndi_id);
    if(it == ndi_to_sai_bridge_port_stat_ids->end() || (sai_id == NULL)){
        NDI_LOG_ERROR("NAS-NDI-UTILS","Failed to get the sai stat id for ndi id %d", ndi_id);
        return false;
    }
    *sai_id = it->second;
    return true;
}

bool ndi_to_sai_bridge_1d_stats(ndi_stat_id_t ndi_id, sai_bridge_stat_t *sai_id){
    static const auto ndi_to_sai_bridge_1d_stat_ids = new std::map<ndi_stat_id_t, sai_bridge_stat_t>
    {
        {  BRIDGE_DOMAIN_BRIDGE_STATS_IN_OCTETS ,SAI_BRIDGE_STAT_IN_OCTETS },
        {  BRIDGE_DOMAIN_BRIDGE_STATS_IN_PKTS ,SAI_BRIDGE_STAT_IN_PACKETS },
        {  BRIDGE_DOMAIN_BRIDGE_STATS_OUT_OCTETS ,SAI_BRIDGE_STAT_OUT_OCTETS},
        {  BRIDGE_DOMAIN_BRIDGE_STATS_OUT_PKTS ,SAI_BRIDGE_STAT_OUT_PACKETS },
    };

    auto it = ndi_to_sai_bridge_1d_stat_ids->find(ndi_id);
    if(it == ndi_to_sai_bridge_1d_stat_ids->end() || (sai_id == NULL)){
        NDI_LOG_ERROR("NAS-NDI-UTILS","Failed to get the sai stat id for ndi id %d", ndi_id);
        return false;
    }
    *sai_id = it->second;
    return true;
}

bool ndi_to_sai_tunnel_stats(ndi_stat_id_t ndi_id, sai_tunnel_stat_t *sai_id){
    static const auto ndi_to_sai_tunnel_stat_ids = new std::map<ndi_stat_id_t, sai_tunnel_stat_t>
    {
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_OCTETS ,SAI_TUNNEL_STAT_IN_OCTETS },
        {  DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_PKTS ,SAI_TUNNEL_STAT_IN_PACKETS },
        {  IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_OCTETS ,SAI_TUNNEL_STAT_OUT_OCTETS},
        {  DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_OUT_PKTS ,SAI_TUNNEL_STAT_OUT_PACKETS },
        {  TUNNEL_TUNNEL_STATS_TUNNELS_IN_OCTETS ,SAI_TUNNEL_STAT_IN_OCTETS },
        {  TUNNEL_TUNNEL_STATS_TUNNELS_IN_PKTS ,SAI_TUNNEL_STAT_IN_PACKETS },
        {  TUNNEL_TUNNEL_STATS_TUNNELS_OUT_OCTETS ,SAI_TUNNEL_STAT_OUT_OCTETS},
        {  TUNNEL_TUNNEL_STATS_TUNNELS_OUT_PKTS,SAI_TUNNEL_STAT_OUT_PACKETS },

    };

    auto it = ndi_to_sai_tunnel_stat_ids->find(ndi_id);
    if(it == ndi_to_sai_tunnel_stat_ids->end() || (sai_id == NULL)){
        NDI_LOG_ERROR("NAS-NDI-UTILS","Failed to get the sai stat id for ndi id %d", ndi_id);
        return false;
    }
    *sai_id = it->second;
    return true;
}

t_std_error handle_profile_map(sai_switch_profile_id_t profile_id,
                               const char *profile_file_name) {
    std::string profile_map_file = DEFAULT_SAI_PROFILE_FILE;
    if (profile_file_name != NULL) {
        profile_map_file = profile_file_name;
    }

    size_t profile_index = profile_id_map.size();
    profile_id_map[profile_id] = profile_index;

    if (profile_map.size() <= profile_id) {
        profile_map.resize(profile_index + 1);
    }
    if (profile_iter.size() <= profile_index) {
        size_t profile_iter_size = profile_iter.size();
        profile_iter.resize(profile_index + 1);
        for (int i = profile_iter_size; i <= profile_iter.size(); ++i) {
            profile_iter[i] = profile_map[i].begin();
        }
    }
    std::ifstream profile(profile_map_file);
    if (!profile.is_open()) {
        NDI_INIT_LOG_INFO("failed to open profile map file: %s : %s using SAI default profile",
                          profile_map_file.c_str(), strerror(errno));
        return STD_ERR_OK;
    }
    std::string line;
    while(getline(profile, line)) {
        if (line.size() > 0 && (line[0] == '#' || line[0] == ';'))
            continue;

        size_t pos = line.find("=");
        if (pos == std::string::npos) {
            NDI_INIT_LOG_INFO("not found '=' in line %s", line.c_str());
            continue;
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        profile_map[profile_id][key] = value;

        NDI_INIT_LOG_TRACE("handle profile map insert key %s: %s", key.c_str(), value.c_str());
    }
    return STD_ERR_OK;
}

