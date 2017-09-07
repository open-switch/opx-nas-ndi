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
 * filename: nas_ndi_qos_port_pool.cpp
 */

#include "std_error_codes.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_int.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_qos_utl.h"
#include "sai.h"
#include "dell-base-qos.h" //from yang model
#include "nas_ndi_qos.h"
#include "nas_ndi_switch.h"

#include <stdio.h>
#include <vector>
#include <unordered_map>


static t_std_error ndi_qos_fill_port_pool_attr(nas_attr_id_t attr_id,
                        const ndi_qos_port_pool_struct_t *p,
                        sai_attribute_t &sai_attr)
{
    // Only the following attributes are settable
    if (attr_id == BASE_QOS_PORT_POOL_PORT_ID) {
        sai_attr.id = SAI_PORT_POOL_ATTR_PORT_ID;
        sai_object_id_t sai_port;
        if (ndi_sai_port_id_get(p->ndi_port.npu_id, p->ndi_port.npu_port, &sai_port) != STD_ERR_OK) {
            return STD_ERR(NPU, PARAM, 0);
        }
        sai_attr.value.oid = sai_port;
    }
    else if (attr_id == BASE_QOS_PORT_POOL_BUFFER_POOL_ID) {
        sai_attr.id = SAI_PORT_POOL_ATTR_BUFFER_POOL_ID;
        sai_attr.value.oid = ndi2sai_buffer_pool_id(p->ndi_pool_id);
    }
    else if (attr_id == BASE_QOS_PORT_POOL_WRED_PROFILE_ID) {
        sai_attr.id = SAI_PORT_POOL_ATTR_QOS_WRED_PROFILE_ID;
        sai_attr.value.oid = ndi2sai_wred_profile_id(p->wred_profile_id);
    }
    else {
        // unsupported set/create attributes
        return STD_ERR(NPU, PARAM, 0);
    }

    return STD_ERR_OK;
}


static t_std_error ndi_qos_fill_port_pool_attr_list(const nas_attr_id_t *nas_attr_list,
                                    uint_t num_attr,
                                    const ndi_qos_port_pool_struct_t *p,
                                    std::vector<sai_attribute_t> &attr_list)
{
    sai_attribute_t sai_attr = {0};
    t_std_error     rc = STD_ERR_OK;

    for (uint_t i = 0; i < num_attr; i++) {
        if ((rc = ndi_qos_fill_port_pool_attr(nas_attr_list[i], p, sai_attr)) != STD_ERR_OK)
            return rc;

        attr_list.push_back(sai_attr);
    }

    return STD_ERR_OK;
}


/**
 * This function creates a Scheduler group ID in the NPU.
 * @param npu id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param p scheduler group structure to be modified
 * @param[out] ndi_port_pool_id
 * @return standard error
 */
t_std_error ndi_qos_create_port_pool(npu_id_t npu_id,
                                const nas_attr_id_t *nas_attr_list,
                                uint_t num_attr,
                                const ndi_qos_port_pool_struct_t *p,
                                ndi_obj_id_t *ndi_port_pool_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_attribute_t>  attr_list;

    if (ndi_qos_fill_port_pool_attr_list(nas_attr_list, num_attr, p,
                                attr_list) != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    sai_object_id_t sai_qos_port_pool_id;
    if ((sai_ret = ndi_sai_qos_port_api(ndi_db_ptr)->
            create_port_pool(&sai_qos_port_pool_id,
                                ndi_switch_id_get(),
                                num_attr,
                                &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d port_pool creation failed\n", npu_id);
        return STD_ERR(QOS, CFG, sai_ret);
    }
    *ndi_port_pool_id = sai2ndi_port_pool_id(sai_qos_port_pool_id);
    return STD_ERR_OK;

}

 /**
  * This function sets the port_pool attributes in the NPU.
  * @param npu id
  * @param ndi_port_pool_id
  * @param attr_id based on the CPS API attribute enumeration values
  * @param p port_pool structure to be modified
  * @return standard error
  */
t_std_error ndi_qos_set_port_pool_attr(npu_id_t npu_id,
                                    ndi_obj_id_t ndi_port_pool_id,
                                    BASE_QOS_PORT_POOL_t attr_id,
                                    const ndi_qos_port_pool_struct_t *p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    sai_attribute_t sai_attr;
    if (ndi_qos_fill_port_pool_attr(attr_id, p, sai_attr) != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    sai_ret = ndi_sai_qos_port_api(ndi_db_ptr)->
            set_port_pool_attribute(
                    ndi2sai_port_pool_id(ndi_port_pool_id),
                    &sai_attr);

    if (sai_ret != SAI_STATUS_SUCCESS && sai_ret != SAI_STATUS_ITEM_ALREADY_EXISTS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d port_pool set failed\n", npu_id);
        return STD_ERR(QOS, CFG, sai_ret);
    }
    return STD_ERR_OK;

}

/**
 * This function deletes a port_pool in the NPU.
 * @param npu_id npu id
 * @param ndi_port_pool_id
 * @return standard error
 */
t_std_error ndi_qos_delete_port_pool(npu_id_t npu_id,
                                    ndi_obj_id_t ndi_port_pool_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    if ((sai_ret = ndi_sai_qos_port_api(ndi_db_ptr)->
            remove_port_pool(ndi2sai_port_pool_id(ndi_port_pool_id)))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d port_pool deletion failed\n", npu_id);
        return STD_ERR(QOS, CFG, sai_ret);
    }

    return STD_ERR_OK;

}


/**
 * This function get a port_pool from the NPU.
 * @param npu id
 * @param ndi_port_pool_id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param[out] ndi_qos_port_pool_struct_t filled if success
 * @return standard error
 */
t_std_error ndi_qos_get_port_pool(npu_id_t npu_id,
                            ndi_obj_id_t ndi_port_pool_id,
                            const nas_attr_id_t *nas_attr_list,
                            uint_t num_attr,
                            ndi_qos_port_pool_struct_t *info)

{
    //set all the flags and call sai
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    std::vector<sai_attribute_t> attr_list;
    sai_attribute_t sai_attr;
    t_std_error rc;

    static const auto & ndi2sai_port_pool_attr_id_map =
        * new std::unordered_map<nas_attr_id_t, sai_attr_id_t, std::hash<int>>
    {
        {BASE_QOS_PORT_POOL_BUFFER_POOL_ID,     SAI_PORT_POOL_ATTR_BUFFER_POOL_ID},
        {BASE_QOS_PORT_POOL_WRED_PROFILE_ID,     SAI_PORT_POOL_ATTR_QOS_WRED_PROFILE_ID},

    };


    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    try {
        for (uint_t i = 0; i < num_attr; i++) {
            memset(&sai_attr, 0, sizeof(sai_attr));
            sai_attr.id = ndi2sai_port_pool_attr_id_map.at(nas_attr_list[i]);
            attr_list.push_back(sai_attr);
        }
    }
    catch(...) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                    "Unexpected error.\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    if ((sai_ret = ndi_sai_qos_port_api(ndi_db_ptr)->
                        get_port_pool_attribute(
                                ndi2sai_port_pool_id(ndi_port_pool_id),
                                attr_list.size(),
                                &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "port_pool get fails: npu_id %u, ndi_port_pool_id 0x%016lx\n",
                npu_id, ndi_port_pool_id);
        return STD_ERR(QOS, CFG, sai_ret);
    }

    for (auto attr: attr_list) {
        if (attr.id == SAI_PORT_POOL_ATTR_PORT_ID) {
            rc = ndi_npu_port_id_get(attr.value.oid,
                                     &info->ndi_port.npu_id,
                                     &info->ndi_port.npu_port);
            if (rc != STD_ERR_OK) {
                EV_LOG_TRACE(ev_log_t_QOS, ev_log_s_MAJOR, "QOS",
                             "Invalid port_id attribute");
                return rc;
            }
        }

        if (attr.id == SAI_PORT_POOL_ATTR_QOS_WRED_PROFILE_ID)
            info->wred_profile_id = sai2ndi_wred_profile_id(attr.value.oid);

        if (attr.id == SAI_PORT_POOL_ATTR_BUFFER_POOL_ID)
            info->ndi_pool_id = sai2ndi_buffer_pool_id(attr.value.oid);
    }

    return STD_ERR_OK;
}


static bool nas2sai_port_pool_counter_type_get(BASE_QOS_PORT_POOL_STAT_t stat_id,
                                            sai_port_pool_stat_t *sai_stat_id)
{
    static const auto &  nas2sai_port_pool_counter_type =
        *new std::unordered_map<BASE_QOS_PORT_POOL_STAT_t, sai_port_pool_stat_t, std::hash<int>>
    {
        {BASE_QOS_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_PACKETS, SAI_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_PACKETS},
        {BASE_QOS_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_BYTES, SAI_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_BYTES},
        {BASE_QOS_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_PACKETS, SAI_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_PACKETS},
        {BASE_QOS_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_BYTES, SAI_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_BYTES},
        {BASE_QOS_PORT_POOL_STAT_RED_DISCARD_DROPPED_PACKETS, SAI_PORT_POOL_STAT_RED_DISCARD_DROPPED_PACKETS},
        {BASE_QOS_PORT_POOL_STAT_RED_DISCARD_DROPPED_BYTES, SAI_PORT_POOL_STAT_RED_DISCARD_DROPPED_BYTES},
        {BASE_QOS_PORT_POOL_STAT_DISCARD_DROPPED_PACKETS, SAI_PORT_POOL_STAT_DISCARD_DROPPED_PACKETS},
        {BASE_QOS_PORT_POOL_STAT_DISCARD_DROPPED_BYTES, SAI_PORT_POOL_STAT_DISCARD_DROPPED_BYTES},
        {BASE_QOS_PORT_POOL_STAT_CURRENT_OCCUPANCY_BYTES, SAI_PORT_POOL_STAT_CURR_OCCUPANCY_BYTES},
        {BASE_QOS_PORT_POOL_STAT_WATERMARK_BYTES, SAI_PORT_POOL_STAT_WATERMARK_BYTES},
        {BASE_QOS_PORT_POOL_STAT_SHARED_CURRENT_OCCUPANCY_BYTES, SAI_PORT_POOL_STAT_SHARED_CURR_OCCUPANCY_BYTES},
        {BASE_QOS_PORT_POOL_STAT_SHARED_WATERMARK_BYTES, SAI_PORT_POOL_STAT_SHARED_WATERMARK_BYTES},
    };

    try {
        *sai_stat_id = nas2sai_port_pool_counter_type.at(stat_id);
    }
    catch (...) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "stats not mapped: stat_id %u\n",
                stat_id);
        return false;
    }
    return true;
}

static void _fill_counter_stat_by_type(sai_port_pool_stat_t type, uint64_t val,
        nas_qos_port_pool_stat_counter_t *stat )
{
    switch(type) {
    case SAI_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_PACKETS:
        stat->green_discard_dropped_packets = val;
        break;
    case SAI_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_BYTES:
        stat->green_discard_dropped_bytes = val;
        break;
    case SAI_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_PACKETS:
        stat->yellow_discard_dropped_packets = val;
        break;
    case SAI_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_BYTES:
        stat->yellow_discard_dropped_bytes = val;
        break;
    case SAI_PORT_POOL_STAT_RED_DISCARD_DROPPED_PACKETS:
        stat->red_discard_dropped_packets = val;
        break;
    case SAI_PORT_POOL_STAT_RED_DISCARD_DROPPED_BYTES:
        stat->red_discard_dropped_bytes = val;
        break;
    case SAI_PORT_POOL_STAT_DISCARD_DROPPED_PACKETS:
        stat->discard_dropped_packets = val;
        break;
    case SAI_PORT_POOL_STAT_DISCARD_DROPPED_BYTES:
        stat->discard_dropped_bytes = val;
        break;
    case SAI_PORT_POOL_STAT_CURR_OCCUPANCY_BYTES:
        stat->current_occupancy_bytes = val;
        break;
    case SAI_PORT_POOL_STAT_WATERMARK_BYTES:
        stat->watermark_bytes = val;
        break;
    case SAI_PORT_POOL_STAT_SHARED_CURR_OCCUPANCY_BYTES:
        stat->shared_current_occupancy_bytes = val;
        break;
    case SAI_PORT_POOL_STAT_SHARED_WATERMARK_BYTES:
        stat->shared_watermark_bytes = val;
        break;
    default:
        break;
    }
}


/**
 * This function gets the port_pool statistics
 * @param ndi_port_id
 * @param ndi_port_pool_id
 * @param list of port_pool counter types to query
 * @param number of port_pool counter types specified
 * @param[out] counter stats
  * return standard error
 */
t_std_error ndi_qos_get_port_pool_stats(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_port_pool_id,
                                BASE_QOS_PORT_POOL_STAT_t *counter_ids,
                                uint_t number_of_counters,
                                nas_qos_port_pool_stat_counter_t *stats)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(ndi_port_id.npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", ndi_port_id.npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_port_pool_stat_t> counter_id_list;
    std::vector<uint64_t> counters(number_of_counters);

    for (uint_t i= 0; i<number_of_counters; i++) {
        sai_port_pool_stat_t sai_stat_id;
        if (nas2sai_port_pool_counter_type_get(counter_ids[i], &sai_stat_id))
            counter_id_list.push_back(sai_stat_id);
    }
    if ((sai_ret = ndi_sai_qos_port_api(ndi_db_ptr)->
                        get_port_pool_stats(
                                ndi2sai_port_pool_id(ndi_port_pool_id),
                                number_of_counters,
                                &counter_id_list[0],
                                &counters[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "port_pool get stats fails: npu_id %u\n",
                ndi_port_id.npu_id);
        return STD_ERR(QOS, CFG, sai_ret);
    }

    // copy the stats out
    for (uint i= 0; i<number_of_counters; i++) {
        _fill_counter_stat_by_type(counter_id_list[i], counters[i], stats);
    }

    return STD_ERR_OK;
}

/**
 * This function clears the port_pool statistics
 * @param ndi_port_id
 * @param ndi_port_pool_id
 * @param list of port_pool counter types to clear
 * @param number of port_pool counter types specified
 * return standard error
 */
t_std_error ndi_qos_clear_port_pool_stats(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_port_pool_id,
                                BASE_QOS_PORT_POOL_STAT_t *counter_ids,
                                uint_t number_of_counters)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(ndi_port_id.npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", ndi_port_id.npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_port_pool_stat_t> counter_id_list;
    std::vector<uint64_t> counters(number_of_counters);

    for (uint_t i= 0; i<number_of_counters; i++) {
        sai_port_pool_stat_t sai_stat_id;
        if (nas2sai_port_pool_counter_type_get(counter_ids[i], &sai_stat_id))
            counter_id_list.push_back(sai_stat_id);
    }
    if ((sai_ret = ndi_sai_qos_port_api(ndi_db_ptr)->
                        clear_port_pool_stats(
                                ndi2sai_port_pool_id(ndi_port_pool_id),
                                number_of_counters,
                                &counter_id_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "port_pool clear stats fails: npu_id %u\n",
                ndi_port_id.npu_id);
        return STD_ERR(QOS, CFG, sai_ret);
    }

    return STD_ERR_OK;

}

