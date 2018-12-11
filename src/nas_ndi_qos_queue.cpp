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
 * filename: nas_ndi_qos_queue.cpp
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


static t_std_error ndi_qos_fill_queue_attr(nas_attr_id_t attr_id,
                        const ndi_qos_queue_struct_t *p,
                        sai_attribute_t &sai_attr)
{
    // Only the following attributes are settable
    if (attr_id == BASE_QOS_QUEUE_PORT_ID) {
        sai_attr.id = SAI_QUEUE_ATTR_PORT;
        sai_object_id_t sai_port;
        if (ndi_sai_port_id_get(p->ndi_port.npu_id, p->ndi_port.npu_port, &sai_port) != STD_ERR_OK) {
            return STD_ERR(NPU, PARAM, 0);
        }
        sai_attr.value.oid = sai_port;
    }
    else if (attr_id == BASE_QOS_QUEUE_TYPE) {
        sai_attr.id = SAI_QUEUE_ATTR_TYPE;
        sai_attr.value.s32 = (p->type == BASE_QOS_QUEUE_TYPE_UCAST? SAI_QUEUE_TYPE_UNICAST:
                                (p->type == BASE_QOS_QUEUE_TYPE_MULTICAST? SAI_QUEUE_TYPE_MULTICAST:
                                        SAI_QUEUE_TYPE_ALL));
    }
    else if (attr_id == BASE_QOS_QUEUE_QUEUE_NUMBER) {
        sai_attr.id = SAI_QUEUE_ATTR_INDEX;
        sai_attr.value.u8 = p->queue_index;
    }
    else if (attr_id == BASE_QOS_QUEUE_SCHEDULER_PROFILE_ID) {
        sai_attr.id = SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID;
        sai_attr.value.oid = ndi2sai_scheduler_profile_id(p->scheduler_profile);
    }
    else if (attr_id == BASE_QOS_QUEUE_WRED_ID) {
        sai_attr.id = SAI_QUEUE_ATTR_WRED_PROFILE_ID;
        sai_attr.value.oid = ndi2sai_wred_profile_id(p->wred_id);
    }
    else if (attr_id == BASE_QOS_QUEUE_BUFFER_PROFILE_ID) {
        sai_attr.id = SAI_QUEUE_ATTR_BUFFER_PROFILE_ID;
        sai_attr.value.oid = ndi2sai_buffer_profile_id(p->buffer_profile);
    }
    else if (attr_id == BASE_QOS_QUEUE_PARENT) {
        sai_attr.id = SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE;
        sai_attr.value.oid = ndi2sai_scheduler_group_id(p->parent);
    }
    else {
        // unsupported set/create attributes
        return STD_ERR(NPU, PARAM, 0);
    }

    return STD_ERR_OK;
}


static t_std_error ndi_qos_fill_queue_attr_list(const nas_attr_id_t *nas_attr_list,
                                    uint_t num_attr,
                                    const ndi_qos_queue_struct_t *p,
                                    std::vector<sai_attribute_t> &attr_list)
{
    sai_attribute_t sai_attr = {0};
    t_std_error     rc = STD_ERR_OK;

    for (uint_t i = 0; i < num_attr; i++) {
        if ((rc = ndi_qos_fill_queue_attr(nas_attr_list[i], p, sai_attr)) != STD_ERR_OK)
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
 * @param[out] ndi_queue_id
 * @return standard error
 */
t_std_error ndi_qos_create_queue(npu_id_t npu_id,
                                const nas_attr_id_t *nas_attr_list,
                                uint_t num_attr,
                                const ndi_qos_queue_struct_t *p,
                                ndi_obj_id_t *ndi_queue_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_attribute_t>  attr_list;

    if (ndi_qos_fill_queue_attr_list(nas_attr_list, num_attr, p,
                                attr_list) != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    sai_object_id_t sai_qos_queue_id;
    if ((sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
            create_queue(&sai_qos_queue_id,
                                ndi_switch_id_get(),
                                num_attr,
                                &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d queue creation failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }
    *ndi_queue_id = sai2ndi_queue_id(sai_qos_queue_id);
    return STD_ERR_OK;

}

 /**
  * This function sets the queue attributes in the NPU.
  * @param npu id
  * @param ndi_queue_id
  * @param attr_id based on the CPS API attribute enumeration values
  * @param p queue structure to be modified
  * @return standard error
  */
t_std_error ndi_qos_set_queue_attr(npu_id_t npu_id,
                                    ndi_obj_id_t ndi_queue_id,
                                    BASE_QOS_QUEUE_t attr_id,
                                    const ndi_qos_queue_struct_t *p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    sai_attribute_t sai_attr;
    if (ndi_qos_fill_queue_attr(attr_id, p, sai_attr) != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
                set_queue_attribute(
                    ndi2sai_queue_id(ndi_queue_id),
                    &sai_attr);

    if (sai_ret != SAI_STATUS_SUCCESS && sai_ret != SAI_STATUS_ITEM_ALREADY_EXISTS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d queue 0x%016lx set failed, rc %d\n",
                      npu_id, ndi_queue_id, sai_ret);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }
    return STD_ERR_OK;

}

/**
 * This function deletes a queue in the NPU.
 * @param npu_id npu id
 * @param ndi_queue_id
 * @return standard error
 */
t_std_error ndi_qos_delete_queue(npu_id_t npu_id,
                                    ndi_obj_id_t ndi_queue_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    if ((sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
            remove_queue(ndi2sai_queue_id(ndi_queue_id)))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d queue deletion failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;

}


/**
 * This function get a queue from the NPU.
 * @param npu id
 * @param ndi_queue_id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param[out] ndi_qos_queue_struct_t filled if success
 * @return standard error
 */
t_std_error ndi_qos_get_queue(npu_id_t npu_id,
                            ndi_obj_id_t ndi_queue_id,
                            const nas_attr_id_t *nas_attr_list,
                            uint_t num_attr,
                            ndi_qos_queue_struct_t *info)

{
    //set all the flags and call sai
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    std::vector<sai_attribute_t> attr_list;
    sai_attribute_t sai_attr;
    t_std_error rc;

    static const auto & ndi2sai_queue_attr_id_map =
        * new std::unordered_map<nas_attr_id_t, sai_attr_id_t, std::hash<int>>
    {
        {BASE_QOS_QUEUE_TYPE,                 SAI_QUEUE_ATTR_TYPE},
        {BASE_QOS_QUEUE_QUEUE_NUMBER,         SAI_QUEUE_ATTR_INDEX},
        {BASE_QOS_QUEUE_PORT_ID,              SAI_QUEUE_ATTR_PORT},
        {BASE_QOS_QUEUE_BUFFER_PROFILE_ID,    SAI_QUEUE_ATTR_BUFFER_PROFILE_ID},
        {BASE_QOS_QUEUE_SCHEDULER_PROFILE_ID, SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID},
        {BASE_QOS_QUEUE_WRED_ID,              SAI_QUEUE_ATTR_WRED_PROFILE_ID},
        {BASE_QOS_QUEUE_PARENT,               SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE},

    };


    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    try {
        for (uint_t i = 0; i < num_attr; i++) {
            if (nas_attr_list[i] == BASE_QOS_QUEUE_MMU_INDEX_LIST)
                continue; // use new api for the attribute
            memset(&sai_attr, 0, sizeof(sai_attr));
            sai_attr.id = ndi2sai_queue_attr_id_map.at(nas_attr_list[i]);
            attr_list.push_back(sai_attr);
        }
    }
    catch(...) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                    "Unexpected error.\n");
        return STD_ERR(QOS, CFG, 0);
    }


    if ((sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
                        get_queue_attribute(ndi2sai_queue_id(ndi_queue_id),
                                attr_list.size(),
                                &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "queue get fails: npu_id %u, ndi_queue_id 0x%016lx\n sai queue id 0x%016lx",
                npu_id, ndi_queue_id, ndi2sai_queue_id(ndi_queue_id));
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    for (auto attr: attr_list) {
        if (attr.id == SAI_QUEUE_ATTR_PORT) {
            rc = ndi_npu_port_id_get(attr.value.oid,
                                     &info->ndi_port.npu_id,
                                     &info->ndi_port.npu_port);
            if (rc != STD_ERR_OK) {
                EV_LOG_TRACE(ev_log_t_QOS, ev_log_s_MAJOR, "QOS",
                             "Invalid port_id attribute");
                return rc;
            }
        }

        if (attr.id == SAI_QUEUE_ATTR_TYPE) {
            info->type = (attr.value.s32 == SAI_QUEUE_TYPE_UNICAST?
                            BASE_QOS_QUEUE_TYPE_UCAST:
                            (attr.value.s32 == SAI_QUEUE_TYPE_MULTICAST?
                             BASE_QOS_QUEUE_TYPE_MULTICAST: BASE_QOS_QUEUE_TYPE_NONE));
        }

        if (attr.id == SAI_QUEUE_ATTR_INDEX)
            info->queue_index = attr.value.u8;

        if (attr.id == SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE)
            info->parent = sai2ndi_scheduler_group_id(attr.value.oid);

        if (attr.id == SAI_QUEUE_ATTR_WRED_PROFILE_ID)
            info->wred_id = sai2ndi_wred_profile_id(attr.value.oid);

        if (attr.id == SAI_QUEUE_ATTR_BUFFER_PROFILE_ID)
            info->buffer_profile = sai2ndi_buffer_profile_id(attr.value.oid);

        if (attr.id == SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID)
            info->scheduler_profile = sai2ndi_scheduler_profile_id(attr.value.oid);
    }

    return STD_ERR_OK;
}

/**
 * This function gets the total number of queues on a port
 * @param ndi_port_id
 * @Return standard error code
 */
uint_t ndi_qos_get_number_of_queues(ndi_port_t ndi_port_id)
{
    static uint_t fp_queue_count = 0;
    static uint_t cpu_queue_count = 0;
    npu_port_t ndi_cpu_port = 0;

    if (fp_queue_count == 0 || cpu_queue_count == 0) {
        ndi_switch_get_queue_numbers(ndi_port_id.npu_id, NULL, NULL, &fp_queue_count, &cpu_queue_count);
    }

    (void) ndi_cpu_port_get(ndi_port_id.npu_id, &ndi_cpu_port);

    if (ndi_port_id.npu_port == ndi_cpu_port) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "CPU queue count %d \n", cpu_queue_count);
        return cpu_queue_count;
    }
    else {
        return fp_queue_count;
    }

}

/**
 * This function gets the list of queues of a port
 * @param ndi_port_id
 * @param count size of the queue_list
 * @param[out] ndi_queue_id_list[] to be filled with either the number of queues
 *            that the port owns or the size of array itself, whichever is less.
 * @Return Number of queues that the port owns.
 */
uint_t ndi_qos_get_queue_id_list(ndi_port_t ndi_port_id,
                                uint_t count,
                                ndi_obj_id_t *ndi_queue_id_list)
{
    /* get queue list */
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(ndi_port_id.npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", ndi_port_id.npu_id);

        return 0;
    }
    EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_port_id %d  queue count %d \n", ndi_port_id.npu_port, count);

    sai_attribute_t sai_attr;
    std::vector<sai_object_id_t> sai_queue_id_list(count);

    sai_attr.id = SAI_PORT_ATTR_QOS_QUEUE_LIST;
    sai_attr.value.objlist.count = count;
    sai_attr.value.objlist.list = &sai_queue_id_list[0];

    sai_object_id_t sai_port;
    if (ndi_sai_port_id_get(ndi_port_id.npu_id, ndi_port_id.npu_port, &sai_port) != STD_ERR_OK) {
        return 0;
    }

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_ret = ndi_sai_qos_port_api(ndi_db_ptr)->
                        get_port_attribute(sai_port,
                                    1, &sai_attr);

    if (sai_ret != SAI_STATUS_SUCCESS  &&
        sai_ret != SAI_STATUS_BUFFER_OVERFLOW) {
        return 0;
    }

    EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                            "No of queue ids retrieved are %d",
                            sai_attr.value.objlist.count);

    // copy out sai-returned queue ids to nas
    if (ndi_queue_id_list) {
        for (uint_t i = 0; (i< sai_attr.value.objlist.count) && (i < count); i++) {
            ndi_queue_id_list[i] = sai2ndi_queue_id(sai_attr.value.objlist.list[i]);
            EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                "sai queue id: 0x%016lx ndi_queue_id: 0x%016lx\n",
                sai_attr.value.objlist.list[i], ndi_queue_id_list[i]);

        }
    }

    return sai_attr.value.objlist.count;

}

static bool nas2sai_queue_counter_type_get(BASE_QOS_QUEUE_STAT_t stat_id,
                                           sai_queue_stat_t *sai_stat_id,
                                           bool is_snapshot)
{
    static const auto &  nas2sai_queue_counter_type =
        *new std::unordered_map<BASE_QOS_QUEUE_STAT_t, sai_queue_stat_t, std::hash<int>>
    {
        {BASE_QOS_QUEUE_STAT_PACKETS, SAI_QUEUE_STAT_PACKETS},
        {BASE_QOS_QUEUE_STAT_BYTES, SAI_QUEUE_STAT_BYTES},
        {BASE_QOS_QUEUE_STAT_DROPPED_PACKETS, SAI_QUEUE_STAT_DROPPED_PACKETS},
        {BASE_QOS_QUEUE_STAT_DROPPED_BYTES, SAI_QUEUE_STAT_DROPPED_BYTES},
        {BASE_QOS_QUEUE_STAT_GREEN_PACKETS, SAI_QUEUE_STAT_GREEN_PACKETS},
        {BASE_QOS_QUEUE_STAT_GREEN_BYTES, SAI_QUEUE_STAT_GREEN_BYTES},
        {BASE_QOS_QUEUE_STAT_GREEN_DROPPED_PACKETS, SAI_QUEUE_STAT_GREEN_DROPPED_PACKETS},
        {BASE_QOS_QUEUE_STAT_GREEN_DROPPED_BYTES, SAI_QUEUE_STAT_GREEN_DROPPED_BYTES},
        {BASE_QOS_QUEUE_STAT_YELLOW_PACKETS, SAI_QUEUE_STAT_YELLOW_PACKETS},
        {BASE_QOS_QUEUE_STAT_YELLOW_BYTES, SAI_QUEUE_STAT_YELLOW_BYTES},
        {BASE_QOS_QUEUE_STAT_YELLOW_DROPPED_PACKETS, SAI_QUEUE_STAT_YELLOW_DROPPED_PACKETS},
        {BASE_QOS_QUEUE_STAT_YELLOW_DROPPED_BYTES, SAI_QUEUE_STAT_YELLOW_DROPPED_BYTES},
        {BASE_QOS_QUEUE_STAT_RED_PACKETS, SAI_QUEUE_STAT_RED_PACKETS},
        {BASE_QOS_QUEUE_STAT_RED_BYTES, SAI_QUEUE_STAT_RED_BYTES},
        {BASE_QOS_QUEUE_STAT_RED_DROPPED_PACKETS, SAI_QUEUE_STAT_RED_DROPPED_PACKETS},
        {BASE_QOS_QUEUE_STAT_RED_DROPPED_BYTES, SAI_QUEUE_STAT_RED_DROPPED_BYTES},
        {BASE_QOS_QUEUE_STAT_GREEN_DISCARD_DROPPED_PACKETS, SAI_QUEUE_STAT_GREEN_WRED_DROPPED_PACKETS},
        {BASE_QOS_QUEUE_STAT_GREEN_DISCARD_DROPPED_BYTES, SAI_QUEUE_STAT_GREEN_WRED_DROPPED_BYTES},
        {BASE_QOS_QUEUE_STAT_YELLOW_DISCARD_DROPPED_PACKETS, SAI_QUEUE_STAT_YELLOW_WRED_DROPPED_PACKETS},
        {BASE_QOS_QUEUE_STAT_YELLOW_DISCARD_DROPPED_BYTES, SAI_QUEUE_STAT_YELLOW_WRED_DROPPED_BYTES},
        {BASE_QOS_QUEUE_STAT_RED_DISCARD_DROPPED_PACKETS, SAI_QUEUE_STAT_RED_WRED_DROPPED_PACKETS},
        {BASE_QOS_QUEUE_STAT_RED_DISCARD_DROPPED_BYTES, SAI_QUEUE_STAT_RED_WRED_DROPPED_BYTES},
        {BASE_QOS_QUEUE_STAT_DISCARD_DROPPED_PACKETS, SAI_QUEUE_STAT_WRED_DROPPED_PACKETS},
        {BASE_QOS_QUEUE_STAT_DISCARD_DROPPED_BYTES, SAI_QUEUE_STAT_WRED_DROPPED_BYTES},
        {BASE_QOS_QUEUE_STAT_CURRENT_OCCUPANCY_BYTES, SAI_QUEUE_STAT_CURR_OCCUPANCY_BYTES},
        {BASE_QOS_QUEUE_STAT_WATERMARK_BYTES, SAI_QUEUE_STAT_WATERMARK_BYTES},
        {BASE_QOS_QUEUE_STAT_SHARED_CURRENT_OCCUPANCY_BYTES, SAI_QUEUE_STAT_SHARED_CURR_OCCUPANCY_BYTES},
        {BASE_QOS_QUEUE_STAT_SHARED_WATERMARK_BYTES, SAI_QUEUE_STAT_SHARED_WATERMARK_BYTES},
    };

    static const auto &  nas2sai_queue_snapshot_counter_type =
        *new std::unordered_map<BASE_QOS_QUEUE_STAT_t, sai_queue_stat_t, std::hash<int>>
    {
        {BASE_QOS_QUEUE_STAT_CURRENT_OCCUPANCY_BYTES, SAI_QUEUE_STAT_EXTENSIONS_SNAPSHOT_CURR_OCCUPANCY_BYTES},
        {BASE_QOS_QUEUE_STAT_WATERMARK_BYTES, SAI_QUEUE_STAT_EXTENSIONS_SNAPSHOT_WATERMARK_BYTES},
        {BASE_QOS_QUEUE_STAT_SHARED_CURRENT_OCCUPANCY_BYTES, SAI_QUEUE_STAT_EXTENSIONS_SNAPSHOT_SHARED_CURR_OCCUPANCY_BYTES},
        {BASE_QOS_QUEUE_STAT_SHARED_WATERMARK_BYTES, SAI_QUEUE_STAT_EXTENSIONS_SNAPSHOT_SHARED_WATERMARK_BYTES},
    };

    try {
        if (is_snapshot == false)
            *sai_stat_id = nas2sai_queue_counter_type.at(stat_id);
        else
            *sai_stat_id = nas2sai_queue_snapshot_counter_type.at(stat_id);
    }
    catch (...) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "stats not mapped: stat_id %u\n",
                stat_id);
        return false;
    }
    return true;
}

static void _fill_counter_stat_by_type(sai_queue_stat_t type, uint64_t val,
        nas_qos_queue_stat_counter_t *stat )
{
    switch(type) {
    case SAI_QUEUE_STAT_PACKETS:
        stat->packets = val;
        break;
    case SAI_QUEUE_STAT_BYTES:
        stat->bytes = val;
        break;
    case SAI_QUEUE_STAT_DROPPED_PACKETS:
        stat->dropped_packets = val;
        break;
    case SAI_QUEUE_STAT_DROPPED_BYTES:
        stat->dropped_bytes = val;
        break;
    case SAI_QUEUE_STAT_GREEN_PACKETS:
        stat->green_packets = val;
        break;
    case SAI_QUEUE_STAT_GREEN_BYTES:
        stat->green_bytes = val;
        break;
    case SAI_QUEUE_STAT_GREEN_DROPPED_PACKETS:
        stat->green_dropped_packets = val;
        break;
    case SAI_QUEUE_STAT_GREEN_DROPPED_BYTES:
        stat->green_dropped_bytes = val;
        break;
    case SAI_QUEUE_STAT_YELLOW_PACKETS:
        stat->yellow_packets = val;
        break;
    case SAI_QUEUE_STAT_YELLOW_BYTES:
        stat->yellow_bytes = val;
        break;
    case SAI_QUEUE_STAT_YELLOW_DROPPED_PACKETS:
        stat->yellow_dropped_packets = val;
        break;
    case SAI_QUEUE_STAT_YELLOW_DROPPED_BYTES:
        stat->yellow_dropped_bytes = val;
        break;
    case SAI_QUEUE_STAT_RED_PACKETS:
        stat->red_packets = val;
        break;
    case SAI_QUEUE_STAT_RED_BYTES:
        stat->red_bytes = val;
        break;
    case SAI_QUEUE_STAT_RED_DROPPED_PACKETS:
        stat->red_dropped_packets = val;
        break;
    case SAI_QUEUE_STAT_RED_DROPPED_BYTES:
        stat->red_dropped_bytes = val;
        break;
    case SAI_QUEUE_STAT_GREEN_WRED_DROPPED_PACKETS:
        stat->green_discard_dropped_packets = val;
        break;
    case SAI_QUEUE_STAT_GREEN_WRED_DROPPED_BYTES:
        stat->green_discard_dropped_bytes = val;
        break;
    case SAI_QUEUE_STAT_YELLOW_WRED_DROPPED_PACKETS:
        stat->yellow_discard_dropped_packets = val;
        break;
    case SAI_QUEUE_STAT_YELLOW_WRED_DROPPED_BYTES:
        stat->yellow_discard_dropped_bytes = val;
        break;
    case SAI_QUEUE_STAT_RED_WRED_DROPPED_PACKETS:
        stat->red_discard_dropped_packets = val;
        break;
    case SAI_QUEUE_STAT_RED_WRED_DROPPED_BYTES:
        stat->red_discard_dropped_bytes = val;
        break;
    case SAI_QUEUE_STAT_WRED_DROPPED_PACKETS:
        stat->discard_dropped_packets = val;
        break;
    case SAI_QUEUE_STAT_WRED_DROPPED_BYTES:
        stat->discard_dropped_bytes = val;
        break;
    case SAI_QUEUE_STAT_CURR_OCCUPANCY_BYTES:
        stat->current_occupancy_bytes = val;
        break;
    case SAI_QUEUE_STAT_WATERMARK_BYTES:
        stat->watermark_bytes = val;
        break;
    case SAI_QUEUE_STAT_SHARED_CURR_OCCUPANCY_BYTES:
        stat->shared_current_occupancy_bytes = val;
        break;
    case SAI_QUEUE_STAT_SHARED_WATERMARK_BYTES:
        stat->shared_watermark_bytes = val;
        break;
    default:
        break;
    }
}


/**
 * This function gets the queue statistics
 * @param ndi_port_id
 * @param ndi_queue_id
 * @param list of queue counter types to query
 * @param number of queue counter types specified
 * @param[out] counter stats
 * return standard error
 * @deprecated since 7.7.0+opx1
 * @see ndi_qos_get_extended_queue_statistics()
 *
 */
t_std_error ndi_qos_get_queue_stats(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_queue_id,
                                BASE_QOS_QUEUE_STAT_t *counter_ids,
                                uint_t number_of_counters,
                                nas_qos_queue_stat_counter_t *stats)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(ndi_port_id.npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", ndi_port_id.npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_queue_stat_t> counter_id_list;
    std::vector<uint64_t> counters(number_of_counters);

    for (uint_t i= 0; i<number_of_counters; i++) {
        sai_queue_stat_t sai_stat_id;
        if (nas2sai_queue_counter_type_get(counter_ids[i], &sai_stat_id, false))
            counter_id_list.push_back(sai_stat_id);
    }
    if ((sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
                        get_queue_stats(ndi2sai_queue_id(ndi_queue_id),
                                counter_id_list.size(),
                                &counter_id_list[0],
                                &counters[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "queue get stats fails: npu_id %u\n",
                ndi_port_id.npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    // copy the stats out
    for (uint i= 0; i<counter_id_list.size(); i++) {
        _fill_counter_stat_by_type(counter_id_list[i], counters[i], stats);
    }

    return STD_ERR_OK;
}

/**
 * This function gets the queue statistics
 * @param ndi_port_id
 * @param ndi_queue_id
 * @param list of queue counter types to query
 * @param number of queue counter types specified
 * @param[out] counters: stats will be stored in the same order of the counter_ids
 * return standard error
 * @deprecated since 7.7.0+opx1
 * @see ndi_qos_get_extended_queue_statistics()
 */
t_std_error ndi_qos_get_queue_statistics(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_queue_id,
                                BASE_QOS_QUEUE_STAT_t *counter_ids,
                                uint_t number_of_counters,
                                uint64_t *counters)
{
    return ndi_qos_get_extended_queue_statistics (ndi_port_id,
                            ndi_queue_id, counter_ids,
                            number_of_counters, counters, false, false);
}

/**
 * This function gets the queue statistics
 * @param ndi_port_id
 * @param ndi_queue_id
 * @param list of queue counter types to query
 * @param number of queue counter types specified
 * @param[out] counters: stats will be stored in the same order of the counter_ids
 * @param read on clear
 * @param is snapshot counter
 * return standard error
 */
t_std_error ndi_qos_get_extended_queue_statistics(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_queue_id,
                                BASE_QOS_QUEUE_STAT_t *counter_ids,
                                uint_t number_of_counters,
                                uint64_t *counters,
                                bool is_read_and_clear,
                                bool is_snapshot_counters)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(ndi_port_id.npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", ndi_port_id.npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_queue_stat_t> sai_counter_id_list;
    sai_queue_stat_t sai_stat_id;
    uint_t i, j;

    for (i= 0; i<number_of_counters; i++) {
        if (nas2sai_queue_counter_type_get(counter_ids[i], &sai_stat_id,
                                           is_snapshot_counters))
            sai_counter_id_list.push_back(sai_stat_id);
        else {
            EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                    "NAS Queue Stat id %d is not mapped to any SAI stat id",
                    counter_ids[i]);
        }
    }

    std::vector<uint64_t> sai_counters(sai_counter_id_list.size());

    if ((sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
                        get_queue_stats(ndi2sai_queue_id(ndi_queue_id),
                                sai_counter_id_list.size(),
                                &sai_counter_id_list[0],
                                &sai_counters[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "queue get stats fails: npu_id %u\n",
                ndi_port_id.npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }
    for (i= 0, j= 0; i < number_of_counters; i++) {
        if (nas2sai_queue_counter_type_get(counter_ids[i], &sai_stat_id,
                                           is_snapshot_counters)) {
            counters[i] = sai_counters[j];
            j++;
        }
        else {
            // zero-filled for counters not able to poll
            counters[i] = 0;
        }
    }

    return STD_ERR_OK;
}


/**
 * This function clears the queue statistics
 * @param ndi_port_id
 * @param ndi_queue_id
 * @param list of queue counter types to clear
 * @param number of queue counter types specified
 * return standard error
 * @deprecated since 7.7.0+opx1
 * @see ndi_qos_clear_queue_statistics()
*/
t_std_error ndi_qos_clear_queue_stats(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_queue_id,
                                BASE_QOS_QUEUE_STAT_t *counter_ids,
                                uint_t number_of_counters)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(ndi_port_id.npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", ndi_port_id.npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_queue_stat_t> counter_id_list;

    for (uint_t i= 0; i<number_of_counters; i++) {
        sai_queue_stat_t sai_stat_id;
        if (nas2sai_queue_counter_type_get(counter_ids[i], &sai_stat_id, false))
            counter_id_list.push_back(sai_stat_id);
    }

    if (counter_id_list.size() == 0) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS", "no valid counter id \n");
        return STD_ERR_OK;
    }

    if ((sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
                        clear_queue_stats(ndi2sai_queue_id(ndi_queue_id),
                                counter_id_list.size(),
                                &counter_id_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "queue clear stats fails: npu_id %u\n",
                ndi_port_id.npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;

}

/**
 * This function clears the queue statistics
 * @param ndi_port_id
 * @param ndi_queue_id
 * @param list of queue counter types to clear
 * @param number of queue counter types specified
 * @param snapshot counters
 * return standard error
 */
t_std_error ndi_qos_clear_extended_queue_statistics(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_queue_id,
                                BASE_QOS_QUEUE_STAT_t *counter_ids,
                                uint_t number_of_counters,
                                bool is_snapshot_counters)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(ndi_port_id.npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", ndi_port_id.npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_queue_stat_t> counter_id_list;

    for (uint_t i= 0; i<number_of_counters; i++) {
        sai_queue_stat_t sai_stat_id;
        if (nas2sai_queue_counter_type_get(counter_ids[i], &sai_stat_id,
                                           is_snapshot_counters))
            counter_id_list.push_back(sai_stat_id);
    }

    if (counter_id_list.size() == 0) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS", "no valid counter id \n");
        return STD_ERR_OK;
    }

    if ((sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
                        clear_queue_stats(ndi2sai_queue_id(ndi_queue_id),
                                counter_id_list.size(),
                                &counter_id_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "queue clear stats fails: npu_id %u\n",
                ndi_port_id.npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;
}


/**
 * This function gets the list of shadow queue object on different MMUs
 * @param npu_id
 * @param ndi_queue_id
 * @param count, size of ndi_shadow_q_list[]
 * @param[out] ndi_shadow_q_list[] will be filled if successful
 * @Return The total number of shadow queue objects on different MMUs.
 *         If the count is smaller than the actual number of shadow queue
 *         objects, ndi_shadow_q_list[] will not be filled.
 */
uint_t ndi_qos_get_shadow_queue_list(npu_id_t npu_id,
                            ndi_obj_id_t ndi_queue_id,
                            uint_t count,
                            ndi_obj_id_t * ndi_shadow_q_list)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t sai_attr;
    std::vector<sai_object_id_t> shadow_q_list(count);

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return 0;
    }

    sai_attr.id = SAI_QUEUE_ATTR_SHADOW_QUEUE_LIST;
    sai_attr.value.objlist.count = count;
    sai_attr.value.objlist.list = &(shadow_q_list[0]);

    if ((sai_ret = ndi_sai_qos_queue_api(ndi_db_ptr)->
                        get_queue_attribute(ndi2sai_queue_id(ndi_queue_id),
                                1, &sai_attr))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                "shadow queue object get fails: npu_id %u, ndi_queue_id 0x%016lx\n sai queue id 0x%016lx",
                npu_id, ndi_queue_id, ndi2sai_queue_id(ndi_queue_id));
        if (sai_ret == SAI_STATUS_BUFFER_OVERFLOW)
            return sai_attr.value.objlist.count;
        else
            return 0;
    }

    for (uint i= 0; i< sai_attr.value.objlist.count; i++) {
        ndi_shadow_q_list[i] = sai2ndi_queue_id(shadow_q_list[i]);
    }

    return sai_attr.value.objlist.count;

}

