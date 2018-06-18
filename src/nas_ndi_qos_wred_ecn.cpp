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
 * filename: nas_ndi_qos_wred_ecn.cpp
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
        {BASE_QOS_WRED_PROFILE_GREEN_ENABLE,            SAI_WRED_ATTR_GREEN_ENABLE},
        {BASE_QOS_WRED_PROFILE_GREEN_MIN_THRESHOLD,     SAI_WRED_ATTR_GREEN_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_GREEN_MAX_THRESHOLD,     SAI_WRED_ATTR_GREEN_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_GREEN_DROP_PROBABILITY,  SAI_WRED_ATTR_GREEN_DROP_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_YELLOW_ENABLE,           SAI_WRED_ATTR_YELLOW_ENABLE},
        {BASE_QOS_WRED_PROFILE_YELLOW_MIN_THRESHOLD,    SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_YELLOW_MAX_THRESHOLD,    SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_YELLOW_DROP_PROBABILITY, SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_RED_ENABLE,              SAI_WRED_ATTR_RED_ENABLE},
        {BASE_QOS_WRED_PROFILE_RED_MIN_THRESHOLD,       SAI_WRED_ATTR_RED_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_RED_MAX_THRESHOLD,       SAI_WRED_ATTR_RED_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_RED_DROP_PROBABILITY,    SAI_WRED_ATTR_RED_DROP_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_WEIGHT,                    SAI_WRED_ATTR_WEIGHT},
        {BASE_QOS_WRED_PROFILE_ECN_MARK,                  SAI_WRED_ATTR_ECN_MARK_MODE},
        {BASE_QOS_WRED_PROFILE_ECN_GREEN_MIN_THRESHOLD,  SAI_WRED_ATTR_ECN_GREEN_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_ECN_GREEN_MAX_THRESHOLD,  SAI_WRED_ATTR_ECN_GREEN_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_ECN_GREEN_PROBABILITY,    SAI_WRED_ATTR_ECN_GREEN_MARK_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_ECN_YELLOW_MIN_THRESHOLD, SAI_WRED_ATTR_ECN_YELLOW_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_ECN_YELLOW_MAX_THRESHOLD, SAI_WRED_ATTR_ECN_YELLOW_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_ECN_YELLOW_PROBABILITY,   SAI_WRED_ATTR_ECN_YELLOW_MARK_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_ECN_RED_MIN_THRESHOLD,    SAI_WRED_ATTR_ECN_RED_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_ECN_RED_MAX_THRESHOLD,    SAI_WRED_ATTR_ECN_RED_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_ECN_RED_PROBABILITY,      SAI_WRED_ATTR_ECN_RED_MARK_PROBABILITY},
        {BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MIN_THRESHOLD, SAI_WRED_ATTR_ECN_COLOR_UNAWARE_MIN_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MAX_THRESHOLD, SAI_WRED_ATTR_ECN_COLOR_UNAWARE_MAX_THRESHOLD},
        {BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_PROBABILITY,   SAI_WRED_ATTR_ECN_COLOR_UNAWARE_MARK_PROBABILITY},
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

static t_std_error ndi_qos_fill_wred_attr(nas_attribute_t nas_attr,
                        sai_attribute_t &sai_attr)
{
    static const auto &  ndi2sai_wred_profile_ecn_mark_mode_map =
    * new std::unordered_map<int, sai_ecn_mark_mode_t, std::hash<int>>
    {
        {BASE_QOS_ECN_MARK_MODE_NONE,           SAI_ECN_MARK_MODE_NONE},
        {BASE_QOS_ECN_MARK_MODE_GREEN,          SAI_ECN_MARK_MODE_GREEN},
        {BASE_QOS_ECN_MARK_MODE_YELLOW,         SAI_ECN_MARK_MODE_YELLOW},
        {BASE_QOS_ECN_MARK_MODE_RED,            SAI_ECN_MARK_MODE_RED},
        {BASE_QOS_ECN_MARK_MODE_GREEN_YELLOW,   SAI_ECN_MARK_MODE_GREEN_YELLOW},
        {BASE_QOS_ECN_MARK_MODE_GREEN_RED,      SAI_ECN_MARK_MODE_GREEN_RED},
        {BASE_QOS_ECN_MARK_MODE_YELLOW_RED,     SAI_ECN_MARK_MODE_YELLOW_RED},
        {BASE_QOS_ECN_MARK_MODE_ALL,            SAI_ECN_MARK_MODE_ALL},
    };

    // Only the settable attributes are included
    if (ndi2sai_wred_profile_attr_id_get(nas_attr.id, &(sai_attr.id)) != true)
        return STD_ERR(QOS, CFG, 0);

    switch (nas_attr.id) {
    case BASE_QOS_WRED_PROFILE_GREEN_ENABLE:
    case BASE_QOS_WRED_PROFILE_YELLOW_ENABLE:
    case BASE_QOS_WRED_PROFILE_RED_ENABLE:
        sai_attr.value.booldata = *(bool *)nas_attr.data;
        break;

    case BASE_QOS_WRED_PROFILE_GREEN_MIN_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_GREEN_MAX_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_GREEN_DROP_PROBABILITY:
    case BASE_QOS_WRED_PROFILE_YELLOW_MIN_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_YELLOW_MAX_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_YELLOW_DROP_PROBABILITY:
    case BASE_QOS_WRED_PROFILE_RED_MIN_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_RED_MAX_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_RED_DROP_PROBABILITY:
    case BASE_QOS_WRED_PROFILE_ECN_GREEN_MIN_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_ECN_GREEN_MAX_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_ECN_GREEN_PROBABILITY:
    case BASE_QOS_WRED_PROFILE_ECN_YELLOW_MIN_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_ECN_YELLOW_MAX_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_ECN_YELLOW_PROBABILITY:
    case BASE_QOS_WRED_PROFILE_ECN_RED_MIN_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_ECN_RED_MAX_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_ECN_RED_PROBABILITY:
    case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MIN_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MAX_THRESHOLD:
    case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_PROBABILITY:
        sai_attr.value.u32 = *(uint32_t *)nas_attr.data;
        break;

    case BASE_QOS_WRED_PROFILE_WEIGHT:
        sai_attr.value.u8 = *(unsigned char *)nas_attr.data;
        break;

    case BASE_QOS_WRED_PROFILE_ECN_MARK:
        try {
            sai_attr.value.s32 =
                ndi2sai_wred_profile_ecn_mark_mode_map.at(*(int32_t *)nas_attr.data);
        } catch (...) {
            EV_LOGGING(NDI, ERR, "NDI-QOS",
                      "ECN-mark value %d is out of bound \n", *(int32_t *)nas_attr.data);
            return STD_ERR(QOS, CFG, 0);
        }
        break;
    }

    return STD_ERR_OK;
}


static t_std_error ndi_qos_fill_wred_attr_list(
                                    uint_t num_attr,
                                    const nas_attribute_t *nas_attr_list,
                                    std::vector<sai_attribute_t> &attr_list)
{
    sai_attribute_t sai_attr = {0};
    t_std_error      rc = STD_ERR_OK;

    for (uint_t i = 0; i < num_attr; i++) {
        if ((rc = ndi_qos_fill_wred_attr(nas_attr_list[i], sai_attr)) != STD_ERR_OK)
            return rc;

        attr_list.push_back(sai_attr);

    }

    return STD_ERR_OK;
}


/**
 * This function creates a WRED profile in the NPU.
 * @param npu id
 * @param num_attr number of attributes in attr_list array
 * @param nas_attr_list list of CPS API attribute IDs and values
 * @param[out] ndi_wred_id
 * @return standard error
 */
t_std_error ndi_qos_create_wred_ecn_profile(npu_id_t npu_id,
                                uint_t num_attr,
                                const nas_attribute_t * nas_attr_list,
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

    if (ndi_qos_fill_wred_attr_list(num_attr, nas_attr_list, attr_list)
            != STD_ERR_OK)
        return STD_ERR(QOS, CFG, 0);

    sai_object_id_t sai_qos_wred_profile_id;
    if ((sai_ret = ndi_sai_qos_wred_api(ndi_db_ptr)->
            create_wred(&sai_qos_wred_profile_id,
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
  * @param nas_attr attribute id and value
  * @return standard error
  */
t_std_error ndi_qos_set_wred_ecn_profile_attr(npu_id_t npu_id,
                                  ndi_obj_id_t ndi_wred_id,
                                  nas_attribute_t nas_attr)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    sai_attribute_t sai_attr;
    if (ndi_qos_fill_wred_attr(nas_attr, sai_attr) != STD_ERR_OK)
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
 * @param npu id
 * @param ndi_wred_id
 * @return standard error
 */
t_std_error ndi_qos_delete_wred_ecn_profile(npu_id_t npu_id, ndi_obj_id_t ndi_wred_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, DEBUG, "NDI-QOS",
                      "npu_id %d not exist\n", npu_id);
        return STD_ERR(QOS, CFG, 0);
    }

    if ((sai_ret = ndi_sai_qos_wred_api(ndi_db_ptr)->
            remove_wred(ndi2sai_wred_profile_id(ndi_wred_id)))
                         != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, NOTICE, "NDI-QOS",
                      "npu_id %d wred profile deletion failed\n", npu_id);
        return ndi_utl_mk_qos_std_err(sai_ret);
    }

    return STD_ERR_OK;
}


static t_std_error _fill_ndi_qos_wred_profile(
                        uint_t num_attr,
                        sai_attribute_t *attr_list,
                        nas_attribute_t *nas_attr_list)
{
    static const auto & sai2ndi_wred_profile_ecn_mark_mode_map =
    * new std::unordered_map<int, BASE_QOS_ECN_MARK_MODE_t, std::hash<int>>
    {
        {SAI_ECN_MARK_MODE_NONE,        BASE_QOS_ECN_MARK_MODE_NONE},
        {SAI_ECN_MARK_MODE_GREEN,       BASE_QOS_ECN_MARK_MODE_GREEN},
        {SAI_ECN_MARK_MODE_YELLOW,      BASE_QOS_ECN_MARK_MODE_YELLOW},
        {SAI_ECN_MARK_MODE_RED,         BASE_QOS_ECN_MARK_MODE_RED},
        {SAI_ECN_MARK_MODE_GREEN_YELLOW,BASE_QOS_ECN_MARK_MODE_GREEN_YELLOW},
        {SAI_ECN_MARK_MODE_GREEN_RED,   BASE_QOS_ECN_MARK_MODE_GREEN_RED},
        {SAI_ECN_MARK_MODE_YELLOW_RED,  BASE_QOS_ECN_MARK_MODE_YELLOW_RED},
        {SAI_ECN_MARK_MODE_ALL,         BASE_QOS_ECN_MARK_MODE_ALL},
    };

    for (uint_t i = 0 ; i< num_attr; i++ ) {
        sai_attribute_t *attr = &attr_list[i];
        switch (attr->id) {
        case SAI_WRED_ATTR_GREEN_ENABLE:
        case SAI_WRED_ATTR_YELLOW_ENABLE:
        case SAI_WRED_ATTR_RED_ENABLE:
            if (nas_attr_list[i].len < sizeof(bool)) {
                EV_LOGGING(NDI, ERR, "NDI-QOS",
                      "Out Param space overflow: attr_id %d, len %d.\n", attr->id, nas_attr_list[i].len);
                return STD_ERR (QOS, PARAM, 0);
            }
            *(bool *)nas_attr_list[i].data = attr->value.booldata;
            break;

        case SAI_WRED_ATTR_GREEN_MIN_THRESHOLD:
        case SAI_WRED_ATTR_GREEN_MAX_THRESHOLD:
        case SAI_WRED_ATTR_GREEN_DROP_PROBABILITY:
        case SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD:
        case SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD:
        case SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY:
        case SAI_WRED_ATTR_RED_MIN_THRESHOLD:
        case SAI_WRED_ATTR_RED_MAX_THRESHOLD:
        case SAI_WRED_ATTR_RED_DROP_PROBABILITY:
        case SAI_WRED_ATTR_ECN_GREEN_MIN_THRESHOLD:
        case SAI_WRED_ATTR_ECN_GREEN_MAX_THRESHOLD:
        case SAI_WRED_ATTR_ECN_GREEN_MARK_PROBABILITY:
        case SAI_WRED_ATTR_ECN_YELLOW_MIN_THRESHOLD:
        case SAI_WRED_ATTR_ECN_YELLOW_MAX_THRESHOLD:
        case SAI_WRED_ATTR_ECN_YELLOW_MARK_PROBABILITY:
        case SAI_WRED_ATTR_ECN_RED_MIN_THRESHOLD:
        case SAI_WRED_ATTR_ECN_RED_MAX_THRESHOLD:
        case SAI_WRED_ATTR_ECN_RED_MARK_PROBABILITY:
        case SAI_WRED_ATTR_ECN_COLOR_UNAWARE_MIN_THRESHOLD:
        case SAI_WRED_ATTR_ECN_COLOR_UNAWARE_MAX_THRESHOLD:
        case SAI_WRED_ATTR_ECN_COLOR_UNAWARE_MARK_PROBABILITY:
            if (nas_attr_list[i].len < sizeof(uint32_t)) {
                EV_LOGGING(NDI, ERR, "NDI-QOS",
                      "Out Param space overflow: attr_id %d, len %d.\n", attr->id, nas_attr_list[i].len);
                return STD_ERR (QOS, PARAM, 0);

            }
            *(uint32_t *)(nas_attr_list[i].data) = attr->value.u32;
            break;

        case SAI_WRED_ATTR_WEIGHT:
            if (nas_attr_list[i].len < sizeof(uint8_t)) {
                EV_LOGGING(NDI, ERR, "NDI-QOS",
                      "Out Param space overflow: attr_id %d, len %d.\n", attr->id, nas_attr_list[i].len);
                return STD_ERR (QOS, PARAM, 0);

            }
            *(uint8_t *)(nas_attr_list[i].data) = attr->value.u8;
            break;

        case SAI_WRED_ATTR_ECN_MARK_MODE:
            try {
                if (nas_attr_list[i].len < sizeof(int32_t)) {
                    EV_LOGGING(NDI, ERR, "NDI-QOS",
                          "Out Param space overflow: attr_id %d, len %d.\n", attr->id, nas_attr_list[i].len);
                    return STD_ERR (QOS, PARAM, 0);

                }
                *(int32_t *)(nas_attr_list[i].data) =
                        sai2ndi_wred_profile_ecn_mark_mode_map.at(attr->value.s32);
            }
            catch (...) {
                EV_LOGGING(NDI, ERR, "NDI-QOS",
                      "wred profile ecn-mark mode %d is not mapped to user.\n", attr->value.s32);
            }
            break;
        } //switch
    } //for

    return STD_ERR_OK;
}


/**
 * This function get a wred profile from the NPU.
 * @param npu id
 * @param ndi_wred_id
 * @param num_attr number of attributes in attr_list array
 * @param[in/out] nas_attr_list list of CPS API attribute IDs and values
 * @return standard error. When successful, nas_attr_list is filled with values
 */
t_std_error ndi_qos_get_wred_ecn_profile(npu_id_t npu_id,
                            ndi_obj_id_t ndi_wred_id,
                            uint_t num_attr,
                            nas_attribute_t * nas_attr_list)
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
        if (ndi2sai_wred_profile_attr_id_get(nas_attr_list[i].id, &(sai_attr.id)) != true)
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
    _fill_ndi_qos_wred_profile(num_attr, &attr_list[0], nas_attr_list);

    return STD_ERR_OK;
}


