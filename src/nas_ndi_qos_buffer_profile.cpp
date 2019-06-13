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
 * filename: nas_ndi_qos_buffer_profile.cpp
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

#define NAS_BUFFER_SHARED_DYNAMIC_THRESHOLD_START 0
#define SAI_BUFFER_SHARED_DYNAMIC_THRESHOLD_START (-7)

#define NAS_TO_SAI_BUFFER_SHARED_DYNAMIC_THESHOLD(x) \
           ((x) - NAS_BUFFER_SHARED_DYNAMIC_THRESHOLD_START \
        + SAI_BUFFER_SHARED_DYNAMIC_THRESHOLD_START)

#define SAI_TO_NAS_BUFFER_SHARED_DYNAMIC_THESHOLD(x) \
           ((x) + NAS_BUFFER_SHARED_DYNAMIC_THRESHOLD_START \
        - SAI_BUFFER_SHARED_DYNAMIC_THRESHOLD_START)

static bool ndi2sai_buffer_profile_attr_id_get(nas_attr_id_t attr_id, sai_attr_id_t *sai_id)
{
    static const auto &  ndi2sai_buffer_profile_attr_id_map =
        * new std::unordered_map<nas_attr_id_t, sai_attr_id_t, std::hash<int>>
    {
        {BASE_QOS_BUFFER_PROFILE_POOL_ID,                   SAI_BUFFER_PROFILE_ATTR_POOL_ID},
        {BASE_QOS_BUFFER_PROFILE_BUFFER_SIZE,               SAI_BUFFER_PROFILE_ATTR_BUFFER_SIZE},
        {BASE_QOS_BUFFER_PROFILE_THRESHOLD_MODE,            SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE},
        {BASE_QOS_BUFFER_PROFILE_SHARED_DYNAMIC_THRESHOLD,  SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH},
        {BASE_QOS_BUFFER_PROFILE_SHARED_STATIC_THRESHOLD,   SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH},
        {BASE_QOS_BUFFER_PROFILE_XOFF_THRESHOLD,            SAI_BUFFER_PROFILE_ATTR_XOFF_TH},
        {BASE_QOS_BUFFER_PROFILE_XON_THRESHOLD,             SAI_BUFFER_PROFILE_ATTR_XON_TH},
        {BASE_QOS_BUFFER_PROFILE_XON_OFFSET_THRESHOLD,      SAI_BUFFER_PROFILE_ATTR_XON_OFFSET_TH},
    };

    try {
        *sai_id = ndi2sai_buffer_profile_attr_id_map.at(attr_id);
    }
    catch (...) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "attr_id %lu not supported\n", attr_id);
        return false;
    }
    return true;
}

static t_std_error ndi_qos_fill_buffer_profile_attr(nas_attr_id_t attr_id,
                        const ndi_qos_buffer_profile_struct_t *p,
                        sai_attribute_t &sai_attr)
{
    // Only the settable attributes are included
    if (ndi2sai_buffer_profile_attr_id_get(attr_id, &(sai_attr.id)) != true) {
        return STD_ERR(QOS, CFG, 0);
    }

    if (attr_id == BASE_QOS_BUFFER_PROFILE_POOL_ID)
        sai_attr.value.u64 = (p->pool_id == NDI_QOS_NULL_OBJECT_ID? SAI_NULL_OBJECT_ID: p->pool_id);
    else if (attr_id == BASE_QOS_BUFFER_PROFILE_BUFFER_SIZE)
        sai_attr.value.u32 = p->buffer_size;
    else if (attr_id == BASE_QOS_BUFFER_PROFILE_THRESHOLD_MODE)
        sai_attr.value.s32 = (p->threshold_mode == BASE_QOS_BUFFER_THRESHOLD_MODE_STATIC?
                                  SAI_BUFFER_PROFILE_THRESHOLD_MODE_STATIC:
                                  SAI_BUFFER_PROFILE_THRESHOLD_MODE_DYNAMIC);
    else if (attr_id == BASE_QOS_BUFFER_PROFILE_SHARED_DYNAMIC_THRESHOLD)
        // Convert NAS range [0..10] to SAI-API range [-7 .. 3]
        sai_attr.value.s8 = NAS_TO_SAI_BUFFER_SHARED_DYNAMIC_THESHOLD(p->shared_dynamic_th);
    else if (attr_id == BASE_QOS_BUFFER_PROFILE_SHARED_STATIC_THRESHOLD)
        sai_attr.value.u32 = p->shared_static_th;
    else if (attr_id == BASE_QOS_BUFFER_PROFILE_XOFF_THRESHOLD)
        sai_attr.value.s32 = p->xoff_th;
    else if (attr_id == BASE_QOS_BUFFER_PROFILE_XON_THRESHOLD)
        sai_attr.value.s32 = p->xon_th;
    else if (attr_id == BASE_QOS_BUFFER_PROFILE_XON_OFFSET_THRESHOLD)
        sai_attr.value.s32 = p->xon_offset_th;

    return STD_ERR_OK;
}


static t_std_error ndi_qos_fill_buffer_profile_attr_list(const nas_attr_id_t *nas_attr_list,
                                    uint_t num_attr,
                                    const ndi_qos_buffer_profile_struct_t *p,
                                    std::vector<sai_attribute_t> &attr_list)
{
    sai_attribute_t sai_attr = {0};
    t_std_error      rc = STD_ERR_OK;

    for (uint_t i = 0; i < num_attr; i++) {
        if ((rc = ndi_qos_fill_buffer_profile_attr(nas_attr_list[i], p, sai_attr)) != STD_ERR_OK)
            return rc;

        attr_list.push_back(sai_attr);

    }

    return STD_ERR_OK;
}



/**
 * This function creates a buffer_profile profile in the NPU.
 * @param npu id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param p buffer_profile structure to be modified
 * @param[out] ndi_buffer_profile_id
 * @return standard error
 */
t_std_error ndi_qos_create_buffer_profile(npu_id_t npu_id,
                                const nas_attr_id_t *nas_attr_list,
                                uint_t num_attr,
                                const ndi_qos_buffer_profile_struct_t *p,
                                ndi_obj_id_t *ndi_buffer_profile_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_attribute_t>  attr_list;

    if (ndi_qos_fill_buffer_profile_attr_list(nas_attr_list, num_attr, p, attr_list)
            != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    sai_object_id_t sai_qos_buffer_profile_id;
    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
            create_buffer_profile(&sai_qos_buffer_profile_id,ndi_switch_id_get(),
                                attr_list.size(),
                                &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d buffer_profile creation failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }
    *ndi_buffer_profile_id = sai2ndi_buffer_profile_id(sai_qos_buffer_profile_id);

    return STD_ERR_OK;
}

 /**
  * This function sets the buffer_profile profile attributes in the NPU.
  * @param npu id
  * @param ndi_buffer_profile_id
  * @param attr_id based on the CPS API attribute enumeration values
  * @param p buffer_profile structure to be modified
  * @return standard error
  */
t_std_error ndi_qos_set_buffer_profile_attr(npu_id_t npu_id, ndi_obj_id_t ndi_buffer_profile_id,
                                  BASE_QOS_BUFFER_PROFILE_t attr_id, const ndi_qos_buffer_profile_struct_t *p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    sai_attribute_t sai_attr;
    if (ndi_qos_fill_buffer_profile_attr(attr_id, p, sai_attr) != STD_ERR_OK) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                        "attr_id %d set failed\n", attr_id);
        return STD_ERR(QOS, CFG, 0);
    }

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
            set_buffer_profile_attribute(
                    ndi2sai_buffer_profile_id(ndi_buffer_profile_id),
                    &sai_attr))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d buffer_profile profile set failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;
}

/**
 * This function deletes a buffer_profile profile in the NPU.
 * @param npu_id npu id
 * @param ndi_buffer_profile_id
 * @return standard error
 */
t_std_error ndi_qos_delete_buffer_profile(npu_id_t npu_id, ndi_obj_id_t ndi_buffer_profile_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
            remove_buffer_profile(ndi2sai_buffer_profile_id(ndi_buffer_profile_id)))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d buffer_profile profile deletion failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;

}

static t_std_error _fill_ndi_qos_buffer_profile_struct(sai_attribute_t *attr_list,
                        uint_t num_attr, ndi_qos_buffer_profile_struct_t *p)
{

    for (uint_t i = 0 ; i< num_attr; i++ ) {
        sai_attribute_t *attr = &attr_list[i];
        if (attr->id == SAI_BUFFER_PROFILE_ATTR_POOL_ID)
            p->pool_id = (attr->value.u64 == SAI_NULL_OBJECT_ID? NDI_QOS_NULL_OBJECT_ID: attr->value.u64);
        else if (attr->id == SAI_BUFFER_PROFILE_ATTR_BUFFER_SIZE)
            p->buffer_size = attr->value.u32;
        else if (attr->id == SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE)
            p->threshold_mode = (attr->value.s32 == SAI_BUFFER_PROFILE_THRESHOLD_MODE_STATIC?
                                 BASE_QOS_BUFFER_THRESHOLD_MODE_STATIC: BASE_QOS_BUFFER_THRESHOLD_MODE_DYNAMIC);
        else if (attr->id == SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH)
            // convert SAI range [-7 .. 3] to NAS range [0 .. 10]
            p->shared_dynamic_th = SAI_TO_NAS_BUFFER_SHARED_DYNAMIC_THESHOLD(attr->value.s8);
        else if (attr->id == SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH)
            p->shared_static_th = attr->value.u32;
        else if (attr->id == SAI_BUFFER_PROFILE_ATTR_XOFF_TH)
            p->xoff_th = attr->value.u32;
        else if (attr->id == SAI_BUFFER_PROFILE_ATTR_XON_TH)
            p->xon_th = attr->value.u32;
        else if (attr->id == SAI_BUFFER_PROFILE_ATTR_XON_OFFSET_TH)
            p->xon_offset_th = attr->value.u32;
    }

    return STD_ERR_OK;
}


/**
 * This function get a buffer_profile profile from the NPU.
 * @param npu id
 * @param ndi_buffer_profile_id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param[out] qos_buffer_profile_struct_t filled if success
 * @return standard error
 */
t_std_error ndi_qos_get_buffer_profile(npu_id_t npu_id,
                            ndi_obj_id_t ndi_buffer_profile_id,
                            const nas_attr_id_t *nas_attr_list,
                            uint_t num_attr,
                            ndi_qos_buffer_profile_struct_t *p)

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
        if (ndi2sai_buffer_profile_attr_id_get(nas_attr_list[i], &(sai_attr.id)) != true)
            return STD_ERR(QOS, CFG, 0);
        else
            attr_list.push_back(sai_attr);
    }

    if ((sai_ret = ndi_sai_qos_buffer_api(ndi_db_ptr)->
            get_buffer_profile_attribute(
                    ndi2sai_buffer_profile_id(ndi_buffer_profile_id),
                    num_attr,
                    &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d buffer_profile get failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    // convert sai result to NAS format
    _fill_ndi_qos_buffer_profile_struct(&attr_list[0], num_attr, p);


    return STD_ERR_OK;

}
