/*
 * Copyright (c) 2019 Dell Inc.
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
 * filename: nas_ndi_fc_stat.cpp
 */


#include "sai.h"
#include "std_error_codes.h"
#include "dell-interface.h"
#include "saifcport.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_fc_init.h"

#include <map>

static bool ndi_to_sai_fc_if_stats(ndi_stat_id_t ndi_id, sai_fc_port_counter_t * sai_id)
{
    static const auto ndi_to_sai_fc_if_stat_ids = new std::map<ndi_stat_id_t ,sai_fc_port_counter_t>
    {
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_BYTES ,SAI_FC_PORT_RX_BYTES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_FRAMES ,SAI_FC_PORT_TOTAL_RX_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_UCAST_PKTS ,SAI_FC_PORT_RX_UNICAST_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_BCAST_PKTS ,SAI_FC_PORT_RX_BROADCAST_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_INVALID_CRC ,SAI_FC_PORT_RX_INVALID_CRC },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_FRAME_TOO_LONG ,SAI_FC_PORT_RX_FRAME_TOO_LONG },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_FRAME_TRUNCATED ,SAI_FC_PORT_RX_RUNT_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_LINK_FAIL ,SAI_FC_PORT_RX_LINK_FAIL },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_LOSS_SYNC ,SAI_FC_PORT_RX_LOSS_SYNC },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_CLASS2_RX_GOOD_FRAMES ,SAI_FC_PORT_CLASS2_RX_GOOD_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_CLASS3_RX_GOOD_FRAMES ,SAI_FC_PORT_CLASS3_RX_GOOD_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_BB_CREDIT0 ,SAI_FC_PORT_RX_BB_CREDIT0 },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_BB_CREDIT0_DROP ,SAI_FC_PORT_RX_BBC0_DROP },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_PRIM_SEQ_ERR ,SAI_FC_PORT_RX_PRIM_SEQ_ERR },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_RX_LIP_COUNT ,SAI_FC_PORT_RX_LIP_COUNT },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TX_BYTES ,SAI_FC_PORT_TX_BYTES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TX_FRAMES ,SAI_FC_PORT_TOTAL_TX_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TX_UCAST_PKTS ,SAI_FC_PORT_TX_UNICAST_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TX_BCAST_PKTS ,SAI_FC_PORT_TX_BCAST_PKTS },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_CLASS2_TX_FRAMES ,SAI_FC_PORT_CLASS2_TX_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_CLASS3_TX_FRAMES ,SAI_FC_PORT_CLASS3_TX_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TX_BB_CREDIT0 ,SAI_FC_PORT_TX_BB_CREDIT0 },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TX_OVERSIZE_FRAMES ,SAI_FC_PORT_TX_OVERSIZED_FRAMES },
        { DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TOTAL_ERRORS ,SAI_FC_PORT_TOTAL_ERRORS },
    };

    auto it = ndi_to_sai_fc_if_stat_ids->find(ndi_id);
    if(it == ndi_to_sai_fc_if_stat_ids->end() || (sai_id == NULL)){
        EV_LOGGING(NDI,DEBUG,"NAS-NDI-FC-STAT","Failed to get mapping stat id for ndi FC stat id %lu ",ndi_id);
        return false;
    }
    *sai_id = it->second;
    return true;
}

extern "C" t_std_error ndi_port_fc_stats_get(npu_id_t npu_id, npu_port_t port_id,
                               ndi_stat_id_t *ndi_fc_stat_ids,
                               uint64_t* stats_val, size_t len)
{
    sai_object_id_t sai_port;
    const unsigned int list_len = len;
    sai_fc_port_counter_t sai_fc_port_stats_ids[list_len];

    t_std_error ret_code = STD_ERR_OK;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI,DEBUG,"FC-PORT-STAT","Invalid NPU Id %d", npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    if ((ret_code = ndi_sai_fcport_id_get(npu_id, port_id, &sai_port)) != STD_ERR_OK) {
        return ret_code;
    }
    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_fc_if_stats(ndi_fc_stat_ids[ix],&sai_fc_port_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_get_fc_port_api()->get_fc_port_stats(sai_port,
                   sai_fc_port_stats_ids, len, stats_val))
                   != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI,ERR,"FC-PORT-STAT","Port stats get failed for npu %d, port %d, sai_port %lu ret %d \n",
                            npu_id, port_id, sai_port, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return ret_code;
}


extern "C" t_std_error ndi_port_fc_stats_clear(npu_id_t npu_id, npu_port_t port_id,
                               ndi_stat_id_t *ndi_fc_stat_ids, size_t len)
{
    sai_object_id_t sai_port;
    t_std_error ret_code = STD_ERR_OK;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    const unsigned int list_len = len;
    sai_fc_port_counter_t sai_fc_port_stats_ids[list_len];

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI,DEBUG,"FC-PORT-STAT","Invalid NPU Id %d", npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    if ((ret_code = ndi_sai_fcport_id_get(npu_id, port_id, &sai_port)) != STD_ERR_OK) {
        return ret_code;
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_fc_if_stats(ndi_fc_stat_ids[ix],&sai_fc_port_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_get_fc_port_api()->clear_fc_port_stats(sai_port,
                   sai_fc_port_stats_ids, len))
                   != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI,ERR,"FC-PORT-STAT","Port stats clear failed for npu %d, port %d, sai_port %lu, ret %d \n",
                            npu_id, port_id, sai_port, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return ret_code;

}
