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
 * filename: nas_ndi_qos_buffer_pool.cpp
 */

#include "std_error_codes.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_int.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_qos_utl.h"
#include "sai.h"
#include "dell-base-qos.h" //from yang model
#include "nas_ndi_qos.h"

#include <stdio.h>
#include <vector>
#include <unordered_map>


static bool ndi2sai_buffer_pool_attr_id_get(nas_attr_id_t attr_id, sai_attr_id_t *sai_id)
{
    static const auto & ndi2sai_buffer_pool_attr_id_map =
            * new std::unordered_map<nas_attr_id_t, sai_attr_id_t, std::hash<int>>
    {
        {BASE_QOS_BUFFER_POOL_SHARED_SIZE,    SAI_BUFFER_POOL_ATTR_SHARED_SIZE},
        {BASE_QOS_BUFFER_POOL_POOL_TYPE,      SAI_BUFFER_POOL_ATTR_TYPE},
        {BASE_QOS_BUFFER_POOL_SIZE,           SAI_BUFFER_POOL_ATTR_SIZE},
        {BASE_QOS_BUFFER_POOL_THRESHOLD_MODE, SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE},
        {BASE_QOS_BUFFER_POOL_XOFF_SIZE,      SAI_BUFFER_POOL_ATTR_XOFF_SIZE},
        {BASE_QOS_BUFFER_POOL_WRED_PROFILE_ID, SAI_BUFFER_POOL_ATTR_WRED_PROFILE_ID},
    };

    try {
        *sai_id = ndi2sai_buffer_pool_attr_id_map.at(attr_id);
    }
    catch (...) {
         EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                       "attr_id %lu not supported\n", attr_id);
         return false;
    }

    return true;
}


static t_std_error ndi_qos_fill_buffer_pool_attr(nas_attr_id_t attr_id,
                        const qos_buffer_pool_struct_t *p,
                        sai_attribute_t &sai_attr)
{
    // Only the settable attributes are included
    if (ndi2sai_buffer_pool_attr_id_get(attr_id, &(sai_attr.id)) != true)
        return STD_ERR(QOS, CFG, 0);

    if (attr_id == BASE_QOS_BUFFER_POOL_SHARED_SIZE)
        sai_attr.value.u32 = p->shared_size;
    else if (attr_id == BASE_QOS_BUFFER_POOL_POOL_TYPE)
        sai_attr.value.s32 = (p->type == BASE_QOS_BUFFER_POOL_TYPE_INGRESS?
                                SAI_BUFFER_POOL_TYPE_INGRESS: SAI_BUFFER_POOL_TYPE_EGRESS);
    else if (attr_id == BASE_QOS_BUFFER_POOL_SIZE)
        sai_attr.value.u32 = p->size;
    else if (attr_id == BASE_QOS_BUFFER_POOL_THRESHOLD_MODE)
        sai_attr.value.s32 = (p->threshold_mode == BASE_QOS_BUFFER_THRESHOLD_MODE_STATIC?
                                SAI_BUFFER_POOL_THRESHOLD_MODE_STATIC: SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC);
    else if (attr_id == BASE_QOS_BUFFER_POOL_XOFF_SIZE)
        sai_attr.value.u32 = p->xoff_size;
    else if (attr_id == BASE_QOS_BUFFER_POOL_WRED_PROFILE_ID)
        sai_attr.value.oid = ndi2sai_wred_profile_id(p->wred_profile_id);

    return STD_ERR_OK;
}


static t_std_error ndi_qos_fill_buffer_pool_attr_list(const nas_attr_id_t *nas_attr_list,
                                    uint_t num_attr,
                                    const qos_buffer_pool_struct_t *p,
                                    std::vector<sai_attribute_t> &attr_list)
{
    sai_attribute_t sai_attr = {0};
    t_std_error      rc = STD_ERR_OK;

    for (uint_t i = 0; i < num_attr; i++) {
        if ((rc = ndi_qos_fill_buffer_pool_attr(nas_attr_list[i], p, sai_attr)) != STD_ERR_OK)
            return rc;

        attr_list.push_back(sai_attr);

    }

    return STD_ERR_OK;
}



/**
 * This function creates a buffer_pool profile in the NPU.
 * @param npu id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param p buffer_pool structure to be modified
 * @param[out] ndi_buffer_pool_id
 * @return standard error
 */
t_std_error ndi_qos_create_buffer_pool(npu_id_t npu_id,
                                const nas_attr_id_t *nas_attr_list,
                                uint_t num_attr,
                                const qos_buffer_pool_struct_t *p,
                                ndi_obj_id_t *ndi_buffer_pool_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_attribute_t>  attr_list;

    if (ndi_qos_fill_buffer_pool_attr_list(nas_attr_list, num_attr, p, attr_list)
            != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    sai_object_id_t sai_qos_buffer_pool_id;
    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
            create_buffer_pool(&sai_qos_buffer_pool_id,
                                ndi_switch_id_get(),
                                attr_list.size(),
                                &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d buffer_pool creation failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }
    *ndi_buffer_pool_id = sai2ndi_buffer_pool_id(sai_qos_buffer_pool_id);

    return STD_ERR_OK;
}

 /**
  * This function sets the buffer_pool profile attributes in the NPU.
  * @param npu id
  * @param ndi_buffer_pool_id
  * @param attr_id based on the CPS API attribute enumeration values
  * @param p buffer_pool structure to be modified
  * @return standard error
  */
t_std_error ndi_qos_set_buffer_pool_attr(npu_id_t npu_id, ndi_obj_id_t ndi_buffer_pool_id,
                                  BASE_QOS_BUFFER_POOL_t attr_id, const qos_buffer_pool_struct_t *p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    sai_attribute_t sai_attr;
    if (ndi_qos_fill_buffer_pool_attr(attr_id, p, sai_attr) != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
            set_buffer_pool_attribute(
                    ndi2sai_buffer_pool_id(ndi_buffer_pool_id),
                    &sai_attr))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d buffer_pool profile set failed, rc = 0x%x\n",
                      npu_id, sai_ret);
       return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;
}

/**
 * This function deletes a buffer_pool profile in the NPU.
 * @param npu_id npu id
 * @param ndi_buffer_pool_id
 * @return standard error
 */
t_std_error ndi_qos_delete_buffer_pool(npu_id_t npu_id, ndi_obj_id_t ndi_buffer_pool_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
            remove_buffer_pool(ndi2sai_buffer_pool_id(ndi_buffer_pool_id)))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d buffer_pool profile deletion failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;

}

static t_std_error _fill_ndi_qos_buffer_pool_struct(sai_attribute_t *attr_list,
                        uint_t num_attr, qos_buffer_pool_struct_t *p)
{

    for (uint_t i = 0 ; i< num_attr; i++ ) {
        sai_attribute_t *attr = &attr_list[i];
        if (attr->id == SAI_BUFFER_POOL_ATTR_SHARED_SIZE)
            p->shared_size = attr->value.u32;
        else if (attr->id == SAI_BUFFER_POOL_ATTR_TYPE)
            p->type = (attr->value.s32 == SAI_BUFFER_POOL_TYPE_INGRESS?
                          BASE_QOS_BUFFER_POOL_TYPE_INGRESS: BASE_QOS_BUFFER_POOL_TYPE_EGRESS);
        else if (attr->id == SAI_BUFFER_POOL_ATTR_SIZE)
            p->size = attr->value.u32;
        else if (attr->id == SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE)
            p->threshold_mode = (attr->value.s32 == SAI_BUFFER_POOL_THRESHOLD_MODE_STATIC?
                                     BASE_QOS_BUFFER_THRESHOLD_MODE_STATIC: BASE_QOS_BUFFER_THRESHOLD_MODE_DYNAMIC);
        else if (attr->id == SAI_BUFFER_POOL_ATTR_XOFF_SIZE)
            p->xoff_size = attr->value.u32;
        else if (attr->id == SAI_BUFFER_POOL_ATTR_WRED_PROFILE_ID)
            p->wred_profile_id = sai2ndi_wred_profile_id(attr->value.oid);

    }

    return STD_ERR_OK;
}


/**
 * This function get a buffer_pool profile from the NPU.
 * @param npu id
 * @param ndi_buffer_pool_id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param[out] qos_buffer_pool_struct_t filled if success
 * @return standard error
 */
t_std_error ndi_qos_get_buffer_pool(npu_id_t npu_id,
                            ndi_obj_id_t ndi_buffer_pool_id,
                            const nas_attr_id_t *nas_attr_list,
                            uint_t num_attr,
                            qos_buffer_pool_struct_t *p)

{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    std::vector<sai_attribute_t> attr_list;
    sai_attribute_t sai_attr;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    for (uint_t i = 0; i < num_attr; i++) {
        if (ndi2sai_buffer_pool_attr_id_get(nas_attr_list[i], &(sai_attr.id)) == true) {
            attr_list.push_back(sai_attr);
        }
        else {
            return STD_ERR(QOS, CFG, 0);
        }
    }

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
            get_buffer_pool_attribute(
                    ndi2sai_buffer_pool_id(ndi_buffer_pool_id),
                    num_attr,
                    &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d buffer_pool get failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    // convert sai result to NAS format
    _fill_ndi_qos_buffer_pool_struct(&attr_list[0], num_attr, p);


    return STD_ERR_OK;

}


static void _fill_counter_stat_by_type(sai_buffer_pool_stat_t type, uint64_t val,
        nas_qos_buffer_pool_stat_counter_t *stat )
{
    switch(type) {

    case SAI_BUFFER_POOL_STAT_CURR_OCCUPANCY_BYTES:
        stat->current_occupancy_bytes = val;
        break;
    case SAI_BUFFER_POOL_STAT_WATERMARK_BYTES:
        stat->watermark_bytes = val;
        break;
    default:
        break;
    }
}

static bool nas2sai_buffer_pool_counter_type_get(BASE_QOS_BUFFER_POOL_STAT_t stat_id,
                                            sai_buffer_pool_stat_t *sai_stat_id)
{
    static const auto & nas2sai_buffer_pool_counter_type =
        * new std::unordered_map<BASE_QOS_BUFFER_POOL_STAT_t, sai_buffer_pool_stat_t,
                                    std::hash<int>>
    {
        {BASE_QOS_BUFFER_POOL_STAT_CURRENT_OCCUPANCY_BYTES,
                SAI_BUFFER_POOL_STAT_CURR_OCCUPANCY_BYTES},
        {BASE_QOS_BUFFER_POOL_STAT_WATERMARK_BYTES,
                SAI_BUFFER_POOL_STAT_WATERMARK_BYTES},
        {BASE_QOS_BUFFER_POOL_STAT_XOFF_HEADROOM_OCCUPANCY_BYTES,
                SAI_BUFFER_POOL_STAT_XOFF_ROOM_CURR_OCCUPANCY_BYTES},
        {BASE_QOS_BUFFER_POOL_STAT_XOFF_HEADROOM_WATERMARK_BYTES,
                SAI_BUFFER_POOL_STAT_XOFF_ROOM_WATERMARK_BYTES},
    };

    try {
        *sai_stat_id = nas2sai_buffer_pool_counter_type.at(stat_id);
    }
    catch (...) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "stats not mapped: stat_id %u\n",
                stat_id);
        return false;
    }
    return true;
}


/**
 * This function gets the buffer_pool statistics
 * @param npu_id
 * @param ndi_buffer_pool_id
 * @param list of buffer_pool counter types to query
 * @param number of buffer_pool counter types specified
 * @param[out] counter stats
  * return standard error
 */
t_std_error ndi_qos_get_buffer_pool_stats(npu_id_t npu_id,
                                ndi_obj_id_t ndi_buffer_pool_id,
                                BASE_QOS_BUFFER_POOL_STAT_t *counter_ids,
                                uint_t number_of_counters,
                                nas_qos_buffer_pool_stat_counter_t *stats)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_buffer_pool_stat_t> counter_id_list;
    std::vector<uint64_t> counters(number_of_counters);
    sai_buffer_pool_stat_t sai_stat_id;

    for (uint_t i= 0; i<number_of_counters; i++) {
        if (nas2sai_buffer_pool_counter_type_get(counter_ids[i], &sai_stat_id))
            counter_id_list.push_back(sai_stat_id);
    }
    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
                        get_buffer_pool_stats(ndi2sai_buffer_pool_id(ndi_buffer_pool_id),
                                counter_id_list.size(),
                                &counter_id_list[0],
                                &counters[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "buffer_pool get stats fails: buffer pool id %lu\n",
                ndi_buffer_pool_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    // copy the stats out
    for (uint i= 0; i<counter_id_list.size(); i++) {
        _fill_counter_stat_by_type(counter_id_list[i], counters[i], stats);
    }

    return STD_ERR_OK;
}

/**
 * This function gets the buffer_pool statistics
 * @param npu_id
 * @param ndi_buffer_pool_id
 * @param list of buffer_pool counter types to query
 * @param number of buffer_pool counter types specified
 * @param[out] counters: stats will be stored in the same order of the counter_ids
  * return standard error
 */
t_std_error ndi_qos_get_buffer_pool_statistics(npu_id_t npu_id,
                                ndi_obj_id_t ndi_buffer_pool_id,
                                BASE_QOS_BUFFER_POOL_STAT_t *counter_ids,
                                uint_t number_of_counters,
                                uint64_t *counters)
{

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_buffer_pool_stat_t> sai_counter_id_list;
    sai_buffer_pool_stat_t sai_stat_id;
    uint_t i, j;

    for (i= 0; i<number_of_counters; i++) {
        if (nas2sai_buffer_pool_counter_type_get(counter_ids[i], &sai_stat_id))
            sai_counter_id_list.push_back(sai_stat_id);
        else {
            EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                    "NAS Buffer Pool Stat id %d is not mapped to any SAI stat id",
                    counter_ids[i]);
        }
    }
    std::vector<uint64_t> sai_counters(sai_counter_id_list.size());

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
                        get_buffer_pool_stats(ndi2sai_buffer_pool_id(ndi_buffer_pool_id),
                                sai_counter_id_list.size(),
                                &sai_counter_id_list[0],
                                &sai_counters[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "buffer_pool get stats fails: buffer pool id %lu\n",
                ndi_buffer_pool_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    for (i= 0, j= 0; i<number_of_counters; i++) {
        if (nas2sai_buffer_pool_counter_type_get(counter_ids[i], &sai_stat_id)) {
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
 * This function gets the list of shadow buffer_pool object on different MMUs
 * @param npu_id
 * @param ndi_buffer_pool_id
 * @param count, size of ndi_shadow_pool_list[]
 * @param[out] ndi_shadow_pool_list[] will be filled if successful
 * @Return The total number of shadow buffer_pool objects on different MMUs.
 *         If the count is smaller than the actual number of shadow buffer_pool
 *         objects, ndi_shadow_pool_list[] will not be filled.
 */
uint_t ndi_qos_get_shadow_buffer_pool_list(npu_id_t npu_id,
                            ndi_obj_id_t ndi_buffer_pool_id,
                            uint_t count,
                            ndi_obj_id_t * ndi_shadow_pool_list)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t sai_attr;
    std::vector<sai_object_id_t> shadow_pool_list(count);

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return 0;
    }

    sai_attr.id = SAI_BUFFER_POOL_ATTR_SHADOW_POOL_LIST;
    sai_attr.value.objlist.count = count;
    sai_attr.value.objlist.list = &(shadow_pool_list[0]);

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
                        get_buffer_pool_attribute(
                                ndi2sai_buffer_pool_id(ndi_buffer_pool_id),
                                1, &sai_attr))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                "shadow buffer_pool object get fails: npu_id %u, ndi_buffer_pool_id 0x%016lx\n sai buffer_pool id 0x%016lx",
                npu_id, ndi_buffer_pool_id, ndi2sai_buffer_pool_id(ndi_buffer_pool_id));
        if (sai_ret == SAI_STATUS_BUFFER_OVERFLOW)
            return sai_attr.value.objlist.count;
        else
            return 0;
    }

    for (uint i= 0; i< sai_attr.value.objlist.count; i++) {
        ndi_shadow_pool_list[i] = sai2ndi_buffer_pool_id(shadow_pool_list[i]);
    }

    return sai_attr.value.objlist.count;

}

/**
 * This function clears the buffer_pool statistics
 * @param npu_id
 * @param ndi_buffer_pool_id
 * @param list of buffer_pool counter types to clear
 * @param number of buffer_pool counter types specified
 * return standard error
 */
t_std_error ndi_qos_clear_buffer_pool_stats(npu_id_t npu_id,
                                ndi_obj_id_t ndi_buffer_pool_id,
                                BASE_QOS_BUFFER_POOL_STAT_t *counter_ids,
                                uint_t number_of_counters)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_buffer_pool_stat_t> counter_id_list;

    for (uint_t i= 0; i<number_of_counters; i++) {
        sai_buffer_pool_stat_t sai_stat_id;
        if (nas2sai_buffer_pool_counter_type_get(counter_ids[i], &sai_stat_id))
            counter_id_list.push_back(sai_stat_id);
        else {
            EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                    "NAS Buffer Pool Stat id %d is not mapped to any SAI stat id",
                    counter_ids[i]);
        }
    }

    if (counter_id_list.size() == 0) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS", "no valid counter id \n");
        return STD_ERR_OK;
    }

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
                        clear_buffer_pool_stats(
                                ndi2sai_buffer_pool_id(ndi_buffer_pool_id),
                                counter_id_list.size(),
                                &counter_id_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                "buffer_pool clear stats fails: npu_id %u\n",
                npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;

}
