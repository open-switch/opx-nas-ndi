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
 * filename: nas_ndi_qos_wred.cpp
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

static bool ndi2sai_wred_profile_attr_id_get(nas_attr_id_t attr_id, sai_attr_id_t *sai_id)
{
    static const auto & ndi2sai_wred_profile_attr_id_map =
        * new std::unordered_map<nas_attr_id_t, sai_attr_id_t, std::hash<int>>
    {
        {BASE_QOS_WRED_PROFILE_GREEN_ENABLE,        SAI_WRED_ATTR_GREEN_ENABLE},
        {BASE_QOS_WRED_PROFILE_GREEN_MIN_THRESHOLD, SAI_WRED_ATTR_GREEN_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_GREEN_MAX_THRESHOLD, SAI_WRED_ATTR_GREEN_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_GREEN_DROP_PROBABILITY,  SAI_WRED_ATTR_GREEN_DROP_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_YELLOW_ENABLE,           SAI_WRED_ATTR_YELLOW_ENABLE},
        {BASE_QOS_WRED_PROFILE_YELLOW_MIN_THRESHOLD,    SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_YELLOW_MAX_THRESHOLD,    SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_YELLOW_DROP_PROBABILITY, SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_RED_ENABLE,              SAI_WRED_ATTR_RED_ENABLE},
        {BASE_QOS_WRED_PROFILE_RED_MIN_THRESHOLD,       SAI_WRED_ATTR_RED_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_RED_MAX_THRESHOLD,       SAI_WRED_ATTR_RED_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_RED_DROP_PROBABILITY,    SAI_WRED_ATTR_RED_DROP_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_WEIGHT,        SAI_WRED_ATTR_WEIGHT},
        // TO be deprecated. For now, re-map it to ECN_MARK_MODE
        {BASE_QOS_WRED_PROFILE_ECN_ENABLE,    SAI_WRED_ATTR_ECN_MARK_MODE},
        {BASE_QOS_WRED_PROFILE_ECN_MARK,      SAI_WRED_ATTR_ECN_MARK_MODE},
    };


    try {
        *sai_id = ndi2sai_wred_profile_attr_id_map.at(attr_id);
    }
    catch(...) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                    "attr_id not mapped: %u\n", attr_id);
        return false;
    }

    return true;
}

static t_std_error ndi_qos_fill_wred_attr(nas_attr_id_t attr_id,
                        const qos_wred_struct_t *p,
                        sai_attribute_t &sai_attr)
{
    static const auto &  ndi2sai_wred_profile_ecn_mark_mode_map =
    * new std::unordered_map<int, sai_ecn_mark_mode_t, std::hash<int>>
    {
        {BASE_QOS_ECN_MARK_MODE_NONE,       SAI_ECN_MARK_MODE_NONE},
        {BASE_QOS_ECN_MARK_MODE_GREEN,      SAI_ECN_MARK_MODE_GREEN},
        {BASE_QOS_ECN_MARK_MODE_YELLOW,     SAI_ECN_MARK_MODE_YELLOW},
        {BASE_QOS_ECN_MARK_MODE_RED,        SAI_ECN_MARK_MODE_RED},
        {BASE_QOS_ECN_MARK_MODE_GREEN_YELLOW,   SAI_ECN_MARK_MODE_GREEN_YELLOW},
        {BASE_QOS_ECN_MARK_MODE_GREEN_RED,      SAI_ECN_MARK_MODE_GREEN_RED},
        {BASE_QOS_ECN_MARK_MODE_YELLOW_RED,     SAI_ECN_MARK_MODE_YELLOW_RED},
        {BASE_QOS_ECN_MARK_MODE_ALL,            SAI_ECN_MARK_MODE_ALL},
    };

    // Only the settable attributes are included
    if (ndi2sai_wred_profile_attr_id_get(attr_id, &(sai_attr.id)) != true)
        return STD_ERR(QOS, CFG, 0);

    if (attr_id == BASE_QOS_WRED_PROFILE_GREEN_ENABLE)
        sai_attr.value.booldata = p->g_enable;
    else if (attr_id == BASE_QOS_WRED_PROFILE_GREEN_MIN_THRESHOLD)
        sai_attr.value.u32 = p->g_min;
    else if (attr_id == BASE_QOS_WRED_PROFILE_GREEN_MAX_THRESHOLD)
        sai_attr.value.u32 = p->g_max;
    else if (attr_id == BASE_QOS_WRED_PROFILE_GREEN_DROP_PROBABILITY)
        sai_attr.value.u32 = p->g_drop_prob;
    else if (attr_id == BASE_QOS_WRED_PROFILE_YELLOW_ENABLE)
        sai_attr.value.booldata = p->y_enable;
    else if (attr_id == BASE_QOS_WRED_PROFILE_YELLOW_MIN_THRESHOLD)
        sai_attr.value.u32 = p->y_min;
    else if (attr_id == BASE_QOS_WRED_PROFILE_YELLOW_MAX_THRESHOLD)
        sai_attr.value.u32 = p->y_max;
    else if (attr_id == BASE_QOS_WRED_PROFILE_YELLOW_DROP_PROBABILITY)
        sai_attr.value.u32 = p->y_drop_prob;
    else if (attr_id == BASE_QOS_WRED_PROFILE_RED_ENABLE)
        sai_attr.value.booldata = p->r_enable;
    else if (attr_id == BASE_QOS_WRED_PROFILE_RED_MIN_THRESHOLD)
        sai_attr.value.u32 = p->r_min;
    else if (attr_id == BASE_QOS_WRED_PROFILE_RED_MAX_THRESHOLD)
        sai_attr.value.u32 = p->r_max;
    else if (attr_id == BASE_QOS_WRED_PROFILE_RED_DROP_PROBABILITY)
        sai_attr.value.u32 = p->r_drop_prob;
    else if (attr_id == BASE_QOS_WRED_PROFILE_WEIGHT)
        sai_attr.value.u8 = p->weight;
    // ECN_ENABLE To be deprecated
    else if ((attr_id == BASE_QOS_WRED_PROFILE_ECN_ENABLE) ||
             (attr_id == BASE_QOS_WRED_PROFILE_ECN_MARK)) {
        try {
            sai_attr.value.s32 = ndi2sai_wred_profile_ecn_mark_mode_map.at(p->ecn_mark);
        } catch (...) {
            EV_LOGGING(NDI, ERR, "NDI-QOS",
                      "ECN-mark value %d is out of bound \n", p->ecn_mark);
            return STD_ERR(QOS, CFG, 0);
        }
    }
    return STD_ERR_OK;
}


static t_std_error ndi_qos_fill_wred_attr_list(const nas_attr_id_t *nas_attr_list,
                                    uint_t num_attr,
                                    const qos_wred_struct_t *p,
                                    std::vector<sai_attribute_t> &attr_list)
{
    sai_attribute_t sai_attr = {0};
    t_std_error      rc = STD_ERR_OK;

    for (uint_t i = 0; i < num_attr; i++) {
        if ((rc = ndi_qos_fill_wred_attr(nas_attr_list[i], p, sai_attr)) != STD_ERR_OK)
            return rc;

        attr_list.push_back(sai_attr);

    }

    return STD_ERR_OK;
}



/**
 * This function creates a WRED profile in the NPU.
 * @param npu id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param p WRED structure to be modified
 * @param[out] ndi_wred_id
 * @return standard error
 */
t_std_error ndi_qos_create_wred_profile(npu_id_t npu_id,
                                const nas_attr_id_t *nas_attr_list,
                                uint_t num_attr,
                                const qos_wred_struct_t *p,
                                ndi_obj_id_t *ndi_wred_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    std::vector<sai_attribute_t>  attr_list;

    if (ndi_qos_fill_wred_attr_list(nas_attr_list, num_attr, p, attr_list)
            != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    sai_object_id_t sai_qos_wred_profile_id;
    if ((sai_ret = ndi_sai_qos_wred_api(ndi_db_ptr)->
            create_wred_profile(&sai_qos_wred_profile_id,
                                ndi_switch_id_get(),
                                attr_list.size(),
                                &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d wred profile creation failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }
    *ndi_wred_id = sai2ndi_wred_profile_id(sai_qos_wred_profile_id);

    return STD_ERR_OK;
}

 /**
  * This function sets the wred profile attributes in the NPU.
  * @param npu id
  * @param ndi_wred_id
  * @param attr_id based on the CPS API attribute enumeration values
  * @param p wred structure to be modified
  * @return standard error
  */
t_std_error ndi_qos_set_wred_profile_attr(npu_id_t npu_id, ndi_obj_id_t ndi_wred_id,
                                  BASE_QOS_WRED_PROFILE_t attr_id, const qos_wred_struct_t *p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    sai_attribute_t sai_attr;
    if (ndi_qos_fill_wred_attr(attr_id, p, sai_attr) != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    if ((sai_ret = ndi_sai_qos_wred_api(ndi_db_ptr)->
            set_wred_attribute(
                    ndi2sai_wred_profile_id(ndi_wred_id),
                    &sai_attr))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d wred profile set failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;
}

/**
 * This function deletes a wred profile in the NPU.
 * @param npu_id npu id
 * @param ndi_wred_id
 * @return standard error
 */
t_std_error ndi_qos_delete_wred_profile(npu_id_t npu_id, ndi_obj_id_t ndi_wred_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    if ((sai_ret = ndi_sai_qos_wred_api(ndi_db_ptr)->
            remove_wred_profile(ndi2sai_wred_profile_id(ndi_wred_id)))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d wred profile deletion failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;

}

static t_std_error _fill_ndi_qos_wred_profile_struct(sai_attribute_t *attr_list,
                        uint_t num_attr, qos_wred_struct_t *p)
{
    static const auto & sai2ndi_wred_profile_ecn_mark_mode_map =
    * new std::unordered_map<int, BASE_QOS_ECN_MARK_MODE_t, std::hash<int>>
    {
        {SAI_ECN_MARK_MODE_NONE,    BASE_QOS_ECN_MARK_MODE_NONE},
        {SAI_ECN_MARK_MODE_GREEN,   BASE_QOS_ECN_MARK_MODE_GREEN},
        {SAI_ECN_MARK_MODE_YELLOW,  BASE_QOS_ECN_MARK_MODE_YELLOW},
        {SAI_ECN_MARK_MODE_RED,     BASE_QOS_ECN_MARK_MODE_RED},
        {SAI_ECN_MARK_MODE_GREEN_YELLOW, BASE_QOS_ECN_MARK_MODE_GREEN_YELLOW},
        {SAI_ECN_MARK_MODE_GREEN_RED,   BASE_QOS_ECN_MARK_MODE_GREEN_RED},
        {SAI_ECN_MARK_MODE_YELLOW_RED,  BASE_QOS_ECN_MARK_MODE_YELLOW_RED},
        {SAI_ECN_MARK_MODE_ALL,         BASE_QOS_ECN_MARK_MODE_ALL},
    };

    for (uint_t i = 0 ; i< num_attr; i++ ) {
        sai_attribute_t *attr = &attr_list[i];
        if (attr->id == SAI_WRED_ATTR_GREEN_ENABLE)
            p->g_enable = attr->value.booldata;
        else if (attr->id == SAI_WRED_ATTR_GREEN_MIN_THRESHOLD)
            p->g_min = attr->value.u32;
        else if (attr->id == SAI_WRED_ATTR_GREEN_MAX_THRESHOLD)
            p->g_max = attr->value.u32;
        else if (attr->id == SAI_WRED_ATTR_GREEN_DROP_PROBABILITY)
            p->g_drop_prob = attr->value.u8;
        else if (attr->id == SAI_WRED_ATTR_YELLOW_ENABLE)
            p->y_enable = attr->value.booldata;
        else if (attr->id == SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD)
            p->y_min = attr->value.u32;
        else if (attr->id == SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD)
            p->y_max = attr->value.u32;
        else if (attr->id == SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY)
            p->y_drop_prob = attr->value.u8;
        else if (attr->id == SAI_WRED_ATTR_RED_ENABLE)
            p->r_enable = attr->value.booldata;
        else if (attr->id == SAI_WRED_ATTR_RED_MIN_THRESHOLD)
            p->r_min = attr->value.u32;
        else if (attr->id == SAI_WRED_ATTR_RED_MAX_THRESHOLD)
            p->r_max = attr->value.u32;
        else if (attr->id == SAI_WRED_ATTR_RED_DROP_PROBABILITY)
            p->r_drop_prob = attr->value.u8;
        else if (attr->id == SAI_WRED_ATTR_WEIGHT)
            p->weight = attr->value.u8;
        else if (attr->id == SAI_WRED_ATTR_ECN_MARK_MODE) {
            try {
                p->ecn_mark = sai2ndi_wred_profile_ecn_mark_mode_map.at(attr->value.s32);
            }
            catch (...) {
                EV_LOGGING(NDI, ERR, "NDI-QOS",
                      "wred profile ecn-mark mode %d is not mapped to user.\n", attr->value.s32);
            }
        }
    }

    return STD_ERR_OK;
}


/**
 * This function get a wred profile from the NPU.
 * @param npu id
 * @param ndi_wred_id
 * @param nas_attr_list based on the CPS API attribute enumeration values
 * @param num_attr number of attributes in attr_list array
 * @param[out] qos_wred_struct_t filled if success
 * @return standard error
 */
t_std_error ndi_qos_get_wred_profile(npu_id_t npu_id,
                            ndi_obj_id_t ndi_wred_id,
                            const nas_attr_id_t *nas_attr_list,
                            uint_t num_attr,
                            qos_wred_struct_t *p)

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
        if (ndi2sai_wred_profile_attr_id_get(nas_attr_list[i], &(sai_attr.id)) != true)
            return STD_ERR(QOS, CFG, 0);

        attr_list.push_back(sai_attr);
    }

    if ((sai_ret = ndi_sai_qos_wred_api(ndi_db_ptr)->
            get_wred_attribute(
                    ndi2sai_wred_profile_id(ndi_wred_id),
                    num_attr,
                    &attr_list[0]))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d wred profile get failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    // convert sai result to NAS format
    _fill_ndi_qos_wred_profile_struct(&attr_list[0], num_attr, p);


    return STD_ERR_OK;

}
