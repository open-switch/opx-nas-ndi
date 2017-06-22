/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/*
 * filename: nas_ndi_udf.cpp
 */

#include "std_assert.h"
#include "nas_ndi_int.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_event_logs.h"
#include "nas_base_utils.h"
#include "nas_ndi_udf.h"
#include "nas_ndi_udf_utl.h"
#include <list>
#include <inttypes.h>

static inline t_std_error _sai_to_ndi_err (sai_status_t st)
{
    return ndi_utl_mk_std_err (e_std_err_UDF, st);
}

extern "C" {

t_std_error ndi_udf_group_create(npu_id_t npu_id, const ndi_udf_grp_t *udf_grp_p,
                                 ndi_obj_id_t *udf_grp_id_p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_udf_grp_id = 0;

    sai_attribute_t sai_attr = {0};
    const sai_attribute_t nil_attr = {0};
    std::vector<sai_attribute_t> sai_attr_list;

    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    if (udf_grp_p == NULL || udf_grp_id_p == NULL) {
        return STD_ERR(UDF, PARAM, 0);
    }

    sai_attr.id = SAI_UDF_GROUP_ATTR_TYPE;
    if (udf_grp_p->group_type == NAS_NDI_UDF_GROUP_GENERIC) {
        sai_attr.value.s32 = SAI_UDF_GROUP_TYPE_GENERIC;
    } else if (udf_grp_p->group_type == NAS_NDI_UDF_GROUP_HASH) {
        sai_attr.value.s32 = SAI_UDF_GROUP_TYPE_HASH;
    } else {
        NDI_UDF_LOG_ERROR("Invalid UDF group type: %d", udf_grp_p->group_type);
        return STD_ERR(UDF, FAIL, 0);
    }
    sai_attr_list.push_back(sai_attr);

    sai_attr = nil_attr;
    sai_attr.id = SAI_UDF_GROUP_ATTR_LENGTH;
    sai_attr.value.u16 = udf_grp_p->length;
    sai_attr_list.push_back(sai_attr);

    sai_ret = ndi_udf_api_get(ndi_db_ptr)->create_udf_group(&sai_udf_grp_id,
                    ndi_switch_id_get(), sai_attr_list.size(),
                    sai_attr_list.data());
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Create UDF group failed in SAI %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }
    *udf_grp_id_p = ndi_udf_sai2ndi_grp_id(sai_udf_grp_id);
    NDI_UDF_LOG_INFO("Successfully created UDF group - Return NDI ID %" PRIx64,
                     *udf_grp_id_p);

    return STD_ERR_OK;
}

t_std_error ndi_udf_group_delete(npu_id_t npu_id, ndi_obj_id_t udf_grp_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    sai_object_id_t sai_udf_grp_id = ndi_udf_ndi2sai_grp_id(udf_grp_id);

    sai_ret = ndi_udf_api_get(ndi_db_ptr)->remove_udf_group(sai_udf_grp_id);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Delete UDF group failed %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    NDI_UDF_LOG_INFO("Successfully deleted UDF group NDI ID %" PRIx64,
                     udf_grp_id);
    return STD_ERR_OK;
}

t_std_error ndi_udf_group_get_udf_list(npu_id_t npu_id, ndi_obj_id_t udf_grp_id,
                                       ndi_obj_id_t *udf_id_list, size_t *udf_id_count)
{
    sai_attribute_t sai_attr = {.id = SAI_UDF_GROUP_ATTR_UDF_LIST};
    sai_status_t sai_ret;
    size_t copy_count;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    if (udf_id_count == NULL) {
        return STD_ERR(UDF, PARAM, 0);
    }

    sai_attr.value.objlist.count = 0;
    sai_attr.value.objlist.list = NULL;
    sai_object_id_t sai_udf_grp_id = ndi_udf_ndi2sai_grp_id(udf_grp_id);
    sai_ret = ndi_udf_api_get(ndi_db_ptr)->get_udf_group_attribute(sai_udf_grp_id,
                                                                   1, &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Failed to get UDF group attribute: %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    if (udf_id_list == NULL) {
        *udf_id_count = sai_attr.value.objlist.count;
        return STD_ERR_OK;
    }
    copy_count = sai_attr.value.objlist.count;
    sai_attr.value.objlist.list = (sai_object_id_t *)calloc(copy_count,
                                    sizeof(sai_object_id_t));
    if (sai_attr.value.objlist.list == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }
    if (copy_count > *udf_id_count) {
        copy_count = *udf_id_count;
    }
    *udf_id_count = sai_attr.value.objlist.count;

    sai_ret = ndi_udf_api_get(ndi_db_ptr)->get_udf_group_attribute(sai_udf_grp_id,
                                                                   1, &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Failed to get UDF group attribute: %d", sai_ret);
        free(sai_attr.value.objlist.list);
        return _sai_to_ndi_err(sai_ret);
    }
    for (size_t idx = 0; idx < copy_count; idx ++) {
        udf_id_list[idx] = ndi_udf_sai2ndi_udf_id(sai_attr.value.objlist.list[idx]);
    }
    free(sai_attr.value.objlist.list);

    return STD_ERR_OK;
}

t_std_error ndi_udf_match_create(npu_id_t npu_id, const ndi_udf_match_t *udf_match_p,
                                 ndi_obj_id_t *udf_match_id_p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_udf_match_id = 0;

    std::vector<sai_attribute_t> attr_list;
    const sai_attribute_t nil_attr = {0};
    sai_attribute_t sai_attr = {0};

    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    if (udf_match_p == NULL || udf_match_id_p == NULL) {
        return STD_ERR(UDF, PARAM, 0);
    }

    sai_attr.id = SAI_UDF_MATCH_ATTR_PRIORITY;
    sai_attr.value.u8 = udf_match_p->priority;
    attr_list.push_back(sai_attr);

    if (udf_match_p->type == NAS_NDI_UDF_MATCH_NON_TUNNEL) {
        sai_attr = nil_attr;
        sai_attr.id = SAI_UDF_MATCH_ATTR_L2_TYPE;
        sai_attr.value.aclfield.data.u16 = udf_match_p->non_tunnel.l2_type;
        sai_attr.value.aclfield.mask.u16 = udf_match_p->non_tunnel.l2_type_mask;
        attr_list.push_back(sai_attr);

        sai_attr = nil_attr;
        sai_attr.id = SAI_UDF_MATCH_ATTR_L3_TYPE;
        sai_attr.value.aclfield.data.u8 = udf_match_p->non_tunnel.l3_type;
        sai_attr.value.aclfield.mask.u8 = udf_match_p->non_tunnel.l3_type_mask;
        attr_list.push_back(sai_attr);
    } else if (udf_match_p->type == NAS_NDI_UDF_MATCH_GRE_TUNNEL) {
        sai_attr = nil_attr;
        sai_attr.id = SAI_UDF_MATCH_ATTR_L2_TYPE;
        sai_attr.value.aclfield.data.u16 = udf_match_p->gre_tunnel.outer_ip_type;
        sai_attr.value.aclfield.mask.u16 = 0xffff;
        attr_list.push_back(sai_attr);

        sai_attr = nil_attr;
        sai_attr.id = SAI_UDF_MATCH_ATTR_L3_TYPE;
        sai_attr.value.aclfield.data.u8 = NAS_NDI_L3_TYPE_GRE;
        sai_attr.value.aclfield.mask.u8 = 0xff;
        attr_list.push_back(sai_attr);

        sai_attr = nil_attr;
        sai_attr.id = SAI_UDF_MATCH_ATTR_GRE_TYPE;
        sai_attr.value.aclfield.data.u16 = udf_match_p->gre_tunnel.inner_ip_type;
        sai_attr.value.aclfield.mask.u16 = 0xffff;
        attr_list.push_back(sai_attr);
    } else {
        NDI_UDF_LOG_ERROR("Invalid UDF match type");
        return STD_ERR(UDF, FAIL, 0);
    }
    sai_ret = ndi_udf_api_get(ndi_db_ptr)->create_udf_match(&sai_udf_match_id,
                    ndi_switch_id_get(), attr_list.size(), attr_list.data());
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Create UDF match failed in SAI: err_code=%d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    *udf_match_id_p = ndi_udf_sai2ndi_match_id(sai_udf_match_id);
    NDI_UDF_LOG_INFO("Successfully created UDF match - Return NDI ID %" PRIx64,
                     *udf_match_id_p);

    return STD_ERR_OK;
}

t_std_error ndi_udf_match_delete(npu_id_t npu_id, ndi_obj_id_t udf_match_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    sai_object_id_t sai_udf_match_id = ndi_udf_ndi2sai_match_id(udf_match_id);

    sai_ret = ndi_udf_api_get(ndi_db_ptr)->remove_udf_match(sai_udf_match_id);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Delete UDF match failed %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    NDI_UDF_LOG_INFO("Successfully deleted UDF match NDI ID %" PRIx64,
                     udf_match_id);
    return STD_ERR_OK;
}

t_std_error ndi_udf_create(npu_id_t npu_id, const ndi_udf_t *udf_p,
                           ndi_obj_id_t *udf_id_p)
{
    sai_status_t sai_ret;
    sai_object_id_t sai_udf_id = 0;

    std::vector<sai_attribute_t> attr_list;
    const sai_attribute_t nil_attr = {0};
    sai_attribute_t sai_attr = {0};
    uint8_t *u8list_buf = NULL;

    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    if (udf_p == NULL || udf_id_p == NULL) {
        return STD_ERR(UDF, PARAM, 0);
    }

    sai_attr.id = SAI_UDF_ATTR_MATCH_ID;
    sai_attr.value.oid = ndi_udf_ndi2sai_match_id(udf_p->udf_match_id);
    attr_list.push_back(sai_attr);

    sai_attr = nil_attr;
    sai_attr.id = SAI_UDF_ATTR_GROUP_ID;
    sai_attr.value.oid = ndi_udf_ndi2sai_grp_id(udf_p->udf_group_id);
    attr_list.push_back(sai_attr);

    sai_attr = nil_attr;
    sai_attr.id = SAI_UDF_ATTR_BASE;
    switch(udf_p->udf_base) {
    case NAS_NDI_UDF_BASE_L2:
        sai_attr.value.s32 = SAI_UDF_BASE_L2;
        break;
    case NAS_NDI_UDF_BASE_L3:
        sai_attr.value.s32 = SAI_UDF_BASE_L3;
        break;
    case NAS_NDI_UDF_BASE_L4:
        sai_attr.value.s32 = SAI_UDF_BASE_L4;
        break;
    default:
        NDI_UDF_LOG_ERROR("Invalid UDF base");
        return STD_ERR(UDF, PARAM, 0);
    }
    attr_list.push_back(sai_attr);

    sai_attr = nil_attr;
    sai_attr.id = SAI_UDF_ATTR_OFFSET;
    sai_attr.value.u16 = udf_p->udf_offset;
    attr_list.push_back(sai_attr);

    if (udf_p->hash_mask_count > 0 && udf_p->udf_hash_mask != NULL) {
        sai_attr = nil_attr;
        sai_attr.id = SAI_UDF_ATTR_HASH_MASK;
        u8list_buf = (uint8_t *)calloc(udf_p->hash_mask_count, 1);
        if (u8list_buf != NULL) {
            sai_attr.value.u8list.count = udf_p->hash_mask_count;
            memcpy(u8list_buf, udf_p->udf_hash_mask, udf_p->hash_mask_count);
            sai_attr.value.u8list.list = u8list_buf;
        }
        attr_list.push_back(sai_attr);
    }

    sai_ret = ndi_udf_api_get(ndi_db_ptr)->create_udf(&sai_udf_id,
                    ndi_switch_id_get(), attr_list.size(), attr_list.data());
    if (u8list_buf != NULL) {
        free(u8list_buf);
    }
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Create UDF failed in SAI %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    *udf_id_p = ndi_udf_sai2ndi_udf_id(sai_udf_id);
    NDI_UDF_LOG_INFO("Successfully created UDF - Return NDI ID %" PRIx64,
                     *udf_id_p);

    return STD_ERR_OK;
}

t_std_error ndi_udf_delete(npu_id_t npu_id, ndi_obj_id_t udf_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    sai_object_id_t sai_udf_id = ndi_udf_ndi2sai_udf_id(udf_id);

    sai_ret = ndi_udf_api_get(ndi_db_ptr)->remove_udf(sai_udf_id);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Delete UDF failed %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    NDI_UDF_LOG_INFO("Successfully deleted UDF NDI ID %" PRIx64,
                     udf_id);
    return STD_ERR_OK;
}

t_std_error ndi_udf_get_hash_mask(npu_id_t npu_id, ndi_obj_id_t udf_id,
                                  uint8_t *hash_mask_list, size_t *hash_mask_count)
{
    sai_attribute_t sai_attr = {.id = SAI_UDF_ATTR_HASH_MASK};
    sai_status_t sai_ret;
    size_t copy_count;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    if (hash_mask_count == NULL) {
        return STD_ERR(UDF, PARAM, 0);
    }

    sai_attr.value.u8list.count = 0;
    sai_attr.value.u8list.list = NULL;
    sai_object_id_t sai_udf_id = ndi_udf_ndi2sai_udf_id(udf_id);
    sai_ret = ndi_udf_api_get(ndi_db_ptr)->get_udf_attribute(sai_udf_id,
                                                             1, &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Failed to get UDF attribute: %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    if (hash_mask_list == NULL) {
        *hash_mask_count = sai_attr.value.u8list.count;
        return STD_ERR_OK;
    }
    copy_count = sai_attr.value.u8list.count;
    sai_attr.value.u8list.list = (uint8_t *)calloc(copy_count, 1);
    if (sai_attr.value.u8list.list == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }
    if (copy_count > *hash_mask_count) {
        copy_count = *hash_mask_count;
    }
    *hash_mask_count = sai_attr.value.u8list.count;

    sai_ret = ndi_udf_api_get(ndi_db_ptr)->get_udf_attribute(sai_udf_id,
                                                             1, &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Failed to get UDF attribute: %d", sai_ret);
        free(sai_attr.value.u8list.list);
        return _sai_to_ndi_err(sai_ret);
    }
    for (size_t idx = 0; idx < copy_count; idx ++) {
        hash_mask_list[idx] = sai_attr.value.u8list.list[idx];
    }
    free(sai_attr.value.u8list.list);

    return STD_ERR_OK;
}

t_std_error ndi_udf_set_hash_mask(npu_id_t npu_id, ndi_obj_id_t udf_id,
                                  uint8_t *hash_mask_list, size_t hash_mask_count)
{
    sai_attribute_t sai_attr = {.id = SAI_UDF_ATTR_HASH_MASK};
    sai_status_t sai_ret;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    if (hash_mask_list == NULL) {
        return STD_ERR(UDF, PARAM, 0);
    }

    sai_attr.value.u8list.count = hash_mask_count;
    sai_attr.value.u8list.list = (uint8_t *)calloc(hash_mask_count, 1);
    if (sai_attr.value.u8list.list == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }
    for (size_t idx = 0; idx < hash_mask_count; idx ++) {
        sai_attr.value.u8list.list[idx] = hash_mask_list[idx];
    }
    sai_object_id_t sai_udf_id = ndi_udf_ndi2sai_udf_id(udf_id);
    sai_ret = ndi_udf_api_get(ndi_db_ptr)->set_udf_attribute(sai_udf_id, &sai_attr);
    free(sai_attr.value.u8list.list);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_UDF_LOG_ERROR("Failed to set UDF attribute: %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    return STD_ERR_OK;
}

} // end Extern C
