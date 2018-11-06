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
 * filename: nas_ndi_acl.cpp
 */


#include "std_assert.h"
#include "dell-base-acl.h"
#include "nas_ndi_int.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_utils.h"
#include "nas_base_utils.h"
#include "nas_ndi_switch.h"
#include "nas_ndi_acl.h"
#include "nas_ndi_acl_utl.h"
#include "nas_ndi_udf_utl.h"
#include <vector>
#include <unordered_map>
#include <string.h>
#include <list>
#include <inttypes.h>

static inline t_std_error _sai_to_ndi_err (sai_status_t st)
{
    return ndi_utl_mk_std_err (e_std_err_ACL, st);
}

extern "C" {

t_std_error ndi_acl_table_create (npu_id_t npu_id, const ndi_acl_table_t* ndi_tbl_p,
                                  ndi_obj_id_t* ndi_tbl_id_p)
{
    t_std_error                   rc = STD_ERR_OK;

    sai_status_t                  sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t               sai_tbl_id = 0;

    sai_attribute_t               sai_tbl_attr = {0}, nil_attr = {0};
    std::vector<sai_attribute_t>  sai_tbl_attr_list;
    sai_attr_id_t                 sai_udf_attr_base = 0;
    nas_ndi_db_t                  *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    /* Reserve some space to avoid repeated memmoves */
    sai_tbl_attr_list.reserve (2 + ndi_tbl_p->filter_count);

    // Table Stage
    sai_tbl_attr.id = SAI_ACL_TABLE_ATTR_ACL_STAGE;
    switch (ndi_tbl_p->stage) {
        case  BASE_ACL_STAGE_INGRESS:
            sai_tbl_attr.value.u32 = SAI_ACL_STAGE_INGRESS;
            break;
        case  BASE_ACL_STAGE_EGRESS:
            sai_tbl_attr.value.u32 = SAI_ACL_STAGE_EGRESS;
            break;
        default:
            NDI_ACL_LOG_ERROR ("Invalid Stage %d", ndi_tbl_p->stage);
            return STD_ERR(ACL, PARAM, 0);
    }
    sai_tbl_attr_list.push_back (sai_tbl_attr);

    // Table Priority
    sai_tbl_attr = nil_attr;
    sai_tbl_attr.id = SAI_ACL_TABLE_ATTR_PRIORITY;
    sai_tbl_attr.value.u32 = ndi_tbl_p->priority;
    sai_tbl_attr_list.push_back (sai_tbl_attr);

    // Table Size
    if (ndi_tbl_p->size != 0) {
        sai_tbl_attr = nil_attr;
        sai_tbl_attr.id = SAI_ACL_TABLE_ATTR_SIZE;
        sai_tbl_attr.value.u32 = ndi_tbl_p->size;
        sai_tbl_attr_list.push_back (sai_tbl_attr);
    }

    bool udf_filter_found = false;
    // Set of Filters allowed in Table
    for (uint_t count = 0; count < ndi_tbl_p->filter_count; count++) {
        sai_tbl_attr = nil_attr;
        auto filter_type = ndi_tbl_p->filter_list[count];

        if ((rc = ndi_acl_utl_ndi2sai_tbl_filter_type (filter_type, &sai_tbl_attr))
            != STD_ERR_OK) {
            NDI_ACL_LOG_ERROR("ACL Filter type %d is not supported in SAI",
                              filter_type);
            return rc;
        }
        if (filter_type == BASE_ACL_MATCH_TYPE_UDF) {
            udf_filter_found = true;
            sai_udf_attr_base = sai_tbl_attr.id;
        } else {
            sai_tbl_attr_list.push_back (sai_tbl_attr);
        }
    }

    if (ndi_tbl_p->udf_grp_count > 0 && !udf_filter_found) {
        NDI_ACL_LOG_ERROR("UDF filter type not in allowed list with UDF group list");
        return STD_ERR(ACL, FAIL, 0);
    }

    // Set of UDF group ID associated with Table
    for (uint idx = 0; idx < ndi_tbl_p->udf_grp_count; idx ++) {
        sai_tbl_attr = nil_attr;
        auto udf_grp_id = ndi_tbl_p->udf_grp_id_list[idx];
        sai_tbl_attr.id = sai_udf_attr_base + idx;
        if (sai_tbl_attr.id > SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MAX) {
            NDI_ACL_LOG_ERROR("Too many UDF configured in table");
            return STD_ERR(ACL, FAIL, 0);
        }
        sai_tbl_attr.value.oid = ndi_udf_ndi2sai_grp_id(udf_grp_id);
        sai_tbl_attr_list.push_back(sai_tbl_attr);
    }

    // Set of Actions allowed in Table
    sai_tbl_attr = nil_attr;
    sai_tbl_attr.id = SAI_ACL_TABLE_ATTR_ACL_ACTION_TYPE_LIST;
    std::vector<int32_t> sai_type_list;
    for (uint_t count = 0; count < ndi_tbl_p->action_count; count++) {
        auto action_type = ndi_tbl_p->action_list[count];

        try {
            sai_type_list.push_back(ndi_acl_utl_ndi2sai_action_type(action_type));
        } catch (std::out_of_range& e) {
            NDI_ACL_LOG_ERROR("ACL Action type %d is not supported in SAI",
                              action_type);
            return STD_ERR(ACL, PARAM, 0);
        }

    }
    if (sai_type_list.size() > 0) {
        sai_tbl_attr.value.s32list.count = sai_type_list.size();
        sai_tbl_attr.value.s32list.list = sai_type_list.data();
        sai_tbl_attr_list.push_back (sai_tbl_attr);
    }

    NDI_ACL_LOG_DETAIL ("Creating ACL Table with %lu attributes",
                        sai_tbl_attr_list.size());

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->create_acl_table (&sai_tbl_id, ndi_switch_id_get(),
                                                                   sai_tbl_attr_list.size(),
                                                                   sai_tbl_attr_list.data()))
        != SAI_STATUS_SUCCESS) {
        NDI_ACL_LOG_ERROR ("Create ACL Table failed in SAI %d", sai_ret);
        return _sai_to_ndi_err (sai_ret);
    }

    *ndi_tbl_id_p = ndi_acl_utl_sai2ndi_table_id (sai_tbl_id);
    NDI_ACL_LOG_INFO ("Successfully created ACL Table - Return NDI ID %" PRIx64,
                      *ndi_tbl_id_p);
    return rc;
}

t_std_error ndi_acl_table_delete (npu_id_t npu_id, ndi_obj_id_t ndi_tbl_id)
{
    t_std_error       rc = STD_ERR_OK;
    sai_status_t      sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t     *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_tbl_id = ndi_acl_utl_ndi2sai_table_id (ndi_tbl_id);

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->remove_acl_table (sai_tbl_id))
        != SAI_STATUS_SUCCESS) {

        NDI_ACL_LOG_ERROR ("Delete ACL Table failed %d", sai_ret);
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully deleted ACL Table NDI ID %" PRIx64,
                      ndi_tbl_id);
    return rc;
}

t_std_error ndi_acl_table_set_priority (npu_id_t npu_id,
                                        ndi_obj_id_t ndi_tbl_id,
                                        ndi_acl_priority_t tbl_priority)
{
    t_std_error         rc = STD_ERR_OK;
    sai_status_t        sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t     sai_tbl_attr = {0};
    nas_ndi_db_t       *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_tbl_id = ndi_acl_utl_ndi2sai_table_id (ndi_tbl_id);

    sai_tbl_attr.id = SAI_ACL_TABLE_ATTR_PRIORITY;
    sai_tbl_attr.value.u32 = tbl_priority;

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->set_acl_table_attribute (sai_tbl_id,
                                                                          &sai_tbl_attr))
        != SAI_STATUS_SUCCESS) {
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully set priority for ACL Table NDI ID %" PRIx64,
                      ndi_tbl_id);
    return rc;
}

t_std_error ndi_acl_entry_create (npu_id_t npu_id,
                                  const ndi_acl_entry_t* ndi_entry_p,
                                  ndi_obj_id_t* ndi_entry_id_p)
{
    t_std_error                   rc = STD_ERR_OK;

    sai_status_t                  sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t               sai_entry_id = 0;

    sai_attribute_t               sai_entry_attr = {0}, nil_attr = {0};
    std::vector<sai_attribute_t>  sai_entry_attr_list;
    nas_ndi_db_t                  *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    nas::mem_alloc_helper_t       malloc_tracker;

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    /* Reserve some space to avoid repeated memmoves */
    sai_entry_attr_list.reserve (3 + ndi_entry_p->filter_count + ndi_entry_p->action_count);

    // Table Id to which Entry belongs
    sai_entry_attr.id = SAI_ACL_ENTRY_ATTR_TABLE_ID;
    sai_entry_attr.value.oid = ndi_entry_p->table_id;
    sai_entry_attr_list.push_back (sai_entry_attr);

    // Entry Priority
    sai_entry_attr = nil_attr;
    sai_entry_attr.id = SAI_ACL_ENTRY_ATTR_PRIORITY;
    sai_entry_attr.value.u32 = ndi_entry_p->priority;
    sai_entry_attr_list.push_back (sai_entry_attr);

    // Entry Admin State
    sai_entry_attr = nil_attr;
    sai_entry_attr.id = SAI_ACL_ENTRY_ATTR_ADMIN_STATE;
    sai_entry_attr.value.u8 = 1; // Enabled
    sai_entry_attr_list.push_back (sai_entry_attr);

    // Filter fields and their values
    for (uint_t count = 0; count < ndi_entry_p->filter_count; count++) {
        sai_entry_attr = nil_attr;
        ndi_acl_entry_filter_t *filter_p = &(ndi_entry_p->filter_list[count]);

        if ((rc = ndi_acl_utl_fill_sai_filter (&sai_entry_attr, filter_p,
                                                malloc_tracker)) != STD_ERR_OK) {
            return rc;
        }
        sai_entry_attr.value.aclfield.enable = true;
        sai_entry_attr_list.push_back (sai_entry_attr);
    }

    // Actions and their values
    for (uint_t count = 0; count < ndi_entry_p->action_count; count++) {
        sai_entry_attr = nil_attr;
        ndi_acl_entry_action_t *action_p = &(ndi_entry_p->action_list[count]);

        if ((rc = ndi_acl_utl_fill_sai_action (&sai_entry_attr, action_p,
                                                malloc_tracker)) != STD_ERR_OK) {
            return rc;
        }
        sai_entry_attr.value.aclaction.enable = true;
        sai_entry_attr_list.push_back (sai_entry_attr);
    }

    NDI_ACL_LOG_DETAIL ("Creating ACL Entry with %lu attributes",
                        sai_entry_attr_list.size());

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->create_acl_entry (&sai_entry_id, ndi_switch_id_get(),
                                                                   sai_entry_attr_list.size(),
                                                                   sai_entry_attr_list.data()))
        != SAI_STATUS_SUCCESS) {
        NDI_ACL_LOG_ERROR ("Create ACL Entry failed in SAI %d", sai_ret);
        return _sai_to_ndi_err (sai_ret);
    }

    *ndi_entry_id_p = ndi_acl_utl_sai2ndi_entry_id (sai_entry_id);
    NDI_ACL_LOG_INFO ("Successfully created ACL Entry - Return NDI ID %" PRIx64,
                      *ndi_entry_id_p);

    return rc;
}

t_std_error ndi_acl_entry_delete (npu_id_t npu_id, ndi_obj_id_t ndi_entry_id)
{
    t_std_error rc = STD_ERR_OK;
    sai_status_t      sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t     *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_entry_id = ndi_acl_utl_ndi2sai_entry_id (ndi_entry_id);

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->remove_acl_entry (sai_entry_id))
        != SAI_STATUS_SUCCESS) {

        NDI_ACL_LOG_ERROR ("Delete ACL Entry failed in SAI %d", sai_ret);
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully deleted ACL Entry NDI ID %" PRIx64,
                      ndi_entry_id);
    return rc;
}

t_std_error ndi_acl_entry_set_priority (npu_id_t npu_id,
                                        ndi_obj_id_t ndi_entry_id,
                                        ndi_acl_priority_t entry_prio)
{
    t_std_error         rc = STD_ERR_OK;
    sai_status_t        sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t     sai_entry_attr = {0};
    nas_ndi_db_t       *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_entry_id = ndi_acl_utl_ndi2sai_entry_id (ndi_entry_id);

    sai_entry_attr.id = SAI_ACL_ENTRY_ATTR_PRIORITY;
    sai_entry_attr.value.u32 = entry_prio;

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->set_acl_entry_attribute (sai_entry_id,
                                                                          &sai_entry_attr))
        != SAI_STATUS_SUCCESS) {
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully set priority for ACL Entry NDI ID %" PRIx64,
                      ndi_entry_id);
    return rc;
}

t_std_error ndi_acl_entry_set_filter (npu_id_t npu_id,
                                      ndi_obj_id_t ndi_entry_id,
                                      ndi_acl_entry_filter_t* filter_p)
{
    t_std_error           rc = STD_ERR_OK;
    sai_status_t          sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t       sai_entry_attr = {0};
    nas_ndi_db_t         *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    nas::mem_alloc_helper_t malloc_tracker;

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_entry_id = ndi_acl_utl_ndi2sai_entry_id (ndi_entry_id);

    if ((rc = ndi_acl_utl_fill_sai_filter (&sai_entry_attr, filter_p,
                                            malloc_tracker)) != STD_ERR_OK) {
        return rc;
    }

    sai_entry_attr.value.aclfield.enable = true;

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->set_acl_entry_attribute (sai_entry_id,
                                                                          &sai_entry_attr))
        != SAI_STATUS_SUCCESS) {
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully set filter type %d for ACL Table NDI ID %" PRIx64,
                      filter_p->filter_type, ndi_entry_id);
    return rc;
}

t_std_error ndi_acl_entry_disable_filter (npu_id_t npu_id,
                                          ndi_obj_id_t ndi_entry_id,
                                          BASE_ACL_MATCH_TYPE_t filter_type)
{
    t_std_error           rc = STD_ERR_OK;
    sai_status_t          sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t       sai_entry_attr = {0};
    nas_ndi_db_t         *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_entry_id = ndi_acl_utl_ndi2sai_entry_id (ndi_entry_id);

    // Action ID
    if ((rc = ndi_acl_utl_ndi2sai_filter_type (filter_type, &sai_entry_attr))
        != STD_ERR_OK) {
        NDI_ACL_LOG_ERROR ("Filter type %d is not supported by SAI",
                           filter_type);
        return rc;
    }
    sai_entry_attr.value.aclfield.enable = false;

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->set_acl_entry_attribute (sai_entry_id,
                                                                          &sai_entry_attr))
        != SAI_STATUS_SUCCESS) {
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully disabled filter type %d for ACL Table NDI ID %" PRIx64,
                      filter_type, ndi_entry_id);
    return rc;
}

t_std_error ndi_acl_entry_set_action (npu_id_t npu_id,
                                      ndi_obj_id_t ndi_entry_id,
                                      ndi_acl_entry_action_t* action_p)
{
    t_std_error           rc = STD_ERR_OK;
    sai_status_t          sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t       sai_entry_attr = {0};
    nas_ndi_db_t         *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    nas::mem_alloc_helper_t malloc_tracker;

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_entry_id = ndi_acl_utl_ndi2sai_entry_id (ndi_entry_id);

    if ((rc = ndi_acl_utl_fill_sai_action (&sai_entry_attr, action_p,
                                            malloc_tracker)) != STD_ERR_OK) {
        return rc;
    }

    sai_entry_attr.value.aclaction.enable = true;

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->set_acl_entry_attribute (sai_entry_id,
                                                                          &sai_entry_attr))
        != SAI_STATUS_SUCCESS) {
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully set action type %d for ACL Entry NDI ID %" PRIx64,
                      action_p->action_type, ndi_entry_id);
    return rc;
}

t_std_error ndi_acl_entry_disable_action (npu_id_t npu_id,
                                          ndi_obj_id_t ndi_entry_id,
                                          BASE_ACL_ACTION_TYPE_t action_type)
{
    t_std_error           rc = STD_ERR_OK;
    sai_status_t          sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t       sai_entry_attr = {0};
    nas_ndi_db_t         *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_entry_id = ndi_acl_utl_ndi2sai_entry_id (ndi_entry_id);

    // Action ID
    if ((rc = ndi_acl_utl_ndi2sai_action_type (action_type, &sai_entry_attr))
        != STD_ERR_OK) {
        NDI_ACL_LOG_ERROR ("Action type %d is not supported by SAI",
                           action_type);
        return rc;
    }
    sai_entry_attr.value.aclaction.enable = false;

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->set_acl_entry_attribute (sai_entry_id,
                                                                          &sai_entry_attr))
        != SAI_STATUS_SUCCESS) {
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully disabled action type %d for ACL Table NDI ID %" PRIx64,
                      action_type, ndi_entry_id);
    return rc;
}

t_std_error ndi_acl_entry_disable_counter_action (npu_id_t npu_id,
                                                  ndi_obj_id_t ndi_entry_id,
                                                  ndi_obj_id_t ndi_counter_id)
{
    t_std_error           rc = STD_ERR_OK;
    sai_status_t          sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t       sai_entry_attr = {0};
    nas_ndi_db_t         *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_entry_id = ndi_acl_utl_ndi2sai_entry_id (ndi_entry_id);

    // Action ID
    if ((rc = ndi_acl_utl_ndi2sai_action_type (BASE_ACL_ACTION_TYPE_SET_COUNTER, &sai_entry_attr))
        != STD_ERR_OK) {
        NDI_ACL_LOG_ERROR ("Action type set_counter is not supported by SAI");
        return rc;
    }
    sai_entry_attr.value.aclaction.enable = false;
    auto sai_oid = static_cast<sai_object_id_t>(ndi_counter_id);
    sai_entry_attr.value.aclaction.parameter.oid = sai_oid;

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->set_acl_entry_attribute (sai_entry_id,
                                                                          &sai_entry_attr))
        != SAI_STATUS_SUCCESS) {
        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully disabled Counter NDI ID %" PRIx64" for ACL Entry NDI ID %" PRIx64,
                      ndi_counter_id, ndi_entry_id);
    return rc;
}

t_std_error ndi_acl_counter_create (npu_id_t npu_id,
                                    const ndi_acl_counter_t* ndi_counter_p,
                                    ndi_obj_id_t* ndi_counter_id_p)
{
    sai_status_t                  sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t               sai_counter_id = 0;

    sai_attribute_t               sai_counter_attr = {0}, nil_attr = {0};
    std::vector<sai_attribute_t>  sai_counter_attr_list;
    nas_ndi_db_t                  *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    /* Reserve some space to avoid repeated memmoves */
    sai_counter_attr_list.reserve (3);

    // Table Id to which Entry belongs
    sai_counter_attr.id = SAI_ACL_COUNTER_ATTR_TABLE_ID;
    sai_counter_attr.value.oid = ndi_counter_p->table_id;
    sai_counter_attr_list.push_back (sai_counter_attr);

    // Enable/Disable Pkt count mode
    sai_counter_attr = nil_attr;
    sai_counter_attr.id = SAI_ACL_COUNTER_ATTR_ENABLE_PACKET_COUNT;
    sai_counter_attr.value.u8 = ndi_counter_p->enable_pkt_count;
    sai_counter_attr_list.push_back (sai_counter_attr);

    // Enable/Disable Byte count mode
    sai_counter_attr = nil_attr;
    sai_counter_attr.id = SAI_ACL_COUNTER_ATTR_ENABLE_BYTE_COUNT;
    sai_counter_attr.value.u8 = ndi_counter_p->enable_byte_count; // Enabled
    sai_counter_attr_list.push_back (sai_counter_attr);

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->create_acl_counter (&sai_counter_id, ndi_switch_id_get(),
                                                                     sai_counter_attr_list.size(),
                                                                     sai_counter_attr_list.data()))
        != SAI_STATUS_SUCCESS) {
        return _sai_to_ndi_err (sai_ret);
    }

    *ndi_counter_id_p = ndi_acl_utl_sai2ndi_counter_id (sai_counter_id);

    NDI_ACL_LOG_INFO ("Successfully created counter - Return NDI ID %" PRIx64,
                      *ndi_counter_id_p);

    return STD_ERR_OK;
}

t_std_error ndi_acl_counter_delete (npu_id_t npu_id,
                                    ndi_obj_id_t ndi_counter_id)
{
    sai_status_t      sai_ret = SAI_STATUS_FAILURE;
    nas_ndi_db_t     *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) return STD_ERR(ACL, FAIL, 0);

    sai_object_id_t sai_counter_id = ndi_acl_utl_ndi2sai_counter_id (ndi_counter_id);

    // Call SAI API
    if ((sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->remove_acl_counter (sai_counter_id))
        != SAI_STATUS_SUCCESS) {

        return _sai_to_ndi_err (sai_ret);
    }

    NDI_ACL_LOG_INFO ("Successfully deleted counter - Return NDI ID %" PRIx64,
                      ndi_counter_id);

    return STD_ERR_OK;
}

t_std_error ndi_acl_counter_enable_pkt_count (npu_id_t npu_id,
                                              ndi_obj_id_t ndi_counter_id,
                                              bool enable)
{
    sai_attribute_t    sai_counter_attr = {0};

    sai_counter_attr.id = SAI_ACL_COUNTER_ATTR_ENABLE_PACKET_COUNT;
    sai_counter_attr.value.u8 = enable;

    return ndi_acl_utl_set_counter_attr (npu_id, ndi_counter_id, &sai_counter_attr);
}

t_std_error ndi_acl_counter_enable_byte_count (npu_id_t npu_id,
                                               ndi_obj_id_t ndi_counter_id,
                                               bool enable)
{
    sai_attribute_t    sai_counter_attr = {0};

    sai_counter_attr.id = SAI_ACL_COUNTER_ATTR_ENABLE_BYTE_COUNT;
    sai_counter_attr.value.u8 = enable;

    return ndi_acl_utl_set_counter_attr (npu_id, ndi_counter_id, &sai_counter_attr);
}

t_std_error ndi_acl_counter_set_pkt_count (npu_id_t npu_id,
                                           ndi_obj_id_t ndi_counter_id,
                                           uint64_t  pkt_count)
{
    sai_attribute_t   sai_counter_attr = {0};

    sai_counter_attr.id = SAI_ACL_COUNTER_ATTR_PACKETS;
    sai_counter_attr.value.u64 = pkt_count;

    return ndi_acl_utl_set_counter_attr (npu_id, ndi_counter_id, &sai_counter_attr);
}

t_std_error ndi_acl_counter_set_byte_count (npu_id_t npu_id,
                                           ndi_obj_id_t ndi_counter_id,
                                           uint64_t  byte_count)
{
    sai_attribute_t   sai_counter_attr = {0};

    sai_counter_attr.id = SAI_ACL_COUNTER_ATTR_BYTES;
    sai_counter_attr.value.u64 = byte_count;

    return ndi_acl_utl_set_counter_attr (npu_id, ndi_counter_id, &sai_counter_attr);
}

t_std_error ndi_acl_counter_get_count (npu_id_t npu_id,
                                       ndi_obj_id_t ndi_counter_id,
                                       uint64_t*  byte_count_p,
                                       uint64_t*  pkt_count_p)
{
    if (byte_count_p == nullptr && pkt_count_p == nullptr) {
        NDI_ACL_LOG_ERROR("Invalid input arguments");
        return STD_ERR(ACL, PARAM, 0);
    }

    uint_t attr_idx = 0;
    sai_attribute_t   sai_counter_attr[2] = {0};

    if (byte_count_p != nullptr) {
        sai_counter_attr[attr_idx].id = SAI_ACL_COUNTER_ATTR_BYTES;
        attr_idx ++;
    }

    if (pkt_count_p != nullptr) {
        sai_counter_attr[attr_idx].id = SAI_ACL_COUNTER_ATTR_PACKETS;
        attr_idx ++;
    }


    t_std_error rc = ndi_acl_utl_get_counter_attr (npu_id, ndi_counter_id,
                                                   sai_counter_attr, attr_idx);
    if (rc != STD_ERR_OK) {
        NDI_ACL_LOG_ERROR("Failed to get counter value");
        return rc;
    }

    attr_idx = 0;
    if (byte_count_p != nullptr) {
        *byte_count_p = sai_counter_attr[attr_idx].value.u64;
        attr_idx ++;
    }
    if (pkt_count_p != nullptr) {
        *pkt_count_p = sai_counter_attr[attr_idx].value.u64;
    }

    return STD_ERR_OK;
}

t_std_error ndi_acl_counter_get_pkt_count (npu_id_t npu_id,
                                           ndi_obj_id_t ndi_counter_id,
                                           uint64_t*  pkt_count_p)
{
    return ndi_acl_counter_get_count(npu_id, ndi_counter_id, nullptr, pkt_count_p);
}

t_std_error ndi_acl_counter_get_byte_count (npu_id_t npu_id,
                                           ndi_obj_id_t ndi_counter_id,
                                           uint64_t*  byte_count_p)
{
    return ndi_acl_counter_get_count(npu_id, ndi_counter_id, byte_count_p, nullptr);
}

t_std_error ndi_acl_range_create(npu_id_t npu_id, const ndi_acl_range_t *acl_range_p,
                                 ndi_obj_id_t *ndi_range_id_p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_range_id = 0;

    sai_attribute_t sai_attr = {0};
    const sai_attribute_t nil_attr = {0};
    std::vector<sai_attribute_t> sai_attr_list;

    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(UDF, FAIL, 0);
    }

    if (acl_range_p == NULL || ndi_range_id_p == NULL) {
        return STD_ERR(ACL, PARAM, 0);
    }

    sai_attr.id = SAI_ACL_RANGE_ATTR_TYPE;
    switch(acl_range_p->type) {
    case NDI_ACL_RANGE_L4_SRC_PORT:
        sai_attr.value.s32 = SAI_ACL_RANGE_TYPE_L4_SRC_PORT_RANGE;
        break;
    case NDI_ACL_RANGE_L4_DST_PORT:
        sai_attr.value.s32 = SAI_ACL_RANGE_TYPE_L4_DST_PORT_RANGE;
        break;
    case NDI_ACL_RANGE_OUTER_VLAN:
        sai_attr.value.s32 = SAI_ACL_RANGE_TYPE_OUTER_VLAN;
        break;
    case NDI_ACL_RANGE_INNER_VLAN:
        sai_attr.value.s32 = SAI_ACL_RANGE_TYPE_INNER_VLAN;
        break;
    case NDI_ACL_RANGE_PACKET_LENGTH:
        sai_attr.value.s32 = SAI_ACL_RANGE_TYPE_PACKET_LENGTH;
        break;
    default:
        NDI_ACL_LOG_ERROR("Unsupported range type %d", acl_range_p->type);
        return STD_ERR(ACL, PARAM, 0);
    }
    sai_attr_list.push_back(sai_attr);

    sai_attr = nil_attr;
    sai_attr.id = SAI_ACL_RANGE_ATTR_LIMIT;
    sai_attr.value.s32range.min = acl_range_p->min;
    sai_attr.value.s32range.max = acl_range_p->max;
    sai_attr_list.push_back(sai_attr);

    sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->create_acl_range(&sai_range_id,
                    ndi_switch_id_get(), sai_attr_list.size(),
                    sai_attr_list.data());
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_ACL_LOG_ERROR("Create ACL range failed in SAI %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }
    *ndi_range_id_p = ndi_acl_utl_sai2ndi_range_id(sai_range_id);
    NDI_ACL_LOG_INFO("Successfully created ACL range - Return NDI ID %" PRIx64,
                     *ndi_range_id_p);

    return STD_ERR_OK;
}

t_std_error ndi_acl_range_delete(npu_id_t npu_id, ndi_obj_id_t ndi_range_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(ACL, FAIL, 0);
    }

    sai_object_id_t sai_range_id = ndi_acl_utl_ndi2sai_range_id(ndi_range_id);

    sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->remove_acl_range(sai_range_id);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_ACL_LOG_ERROR("Delete ACL range failed %d", sai_ret);
        return _sai_to_ndi_err(sai_ret);
    }

    NDI_ACL_LOG_INFO("Successfully deleted ACL range NDI ID %" PRIx64,
                     ndi_range_id);
    return STD_ERR_OK;
}

t_std_error ndi_acl_get_slice_attribute (npu_id_t npu_id, ndi_obj_id_t slice_id,
                                         ndi_acl_slice_attr_t *slice_attr)
{
#define NDI_MAX_ACL_SLICE_ATTR   6
    size_t                    attr_count = 0;
    size_t                    attr_idx = 0;
    size_t                    list_sz = slice_attr->acl_table_count;
    sai_attribute_t           sai_attr[NDI_MAX_ACL_SLICE_ATTR];
    sai_status_t              sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(ACL, FAIL, 0);
    }
    memset (&sai_attr, 0, sizeof (sai_attr));

    std::vector<sai_object_id_t> slice_acl_tbl_obj_list(list_sz);

    sai_object_id_t sai_slice_id = ndi_acl_ndi2sai_slice_id (slice_id);

    //retrieve below slice attributes
    sai_attr[attr_idx++].id = SAI_ACL_SLICE_ATTR_EXTENSIONS_SLICE_ID;
    sai_attr[attr_idx++].id = SAI_ACL_SLICE_ATTR_EXTENSIONS_PIPE_ID;
    sai_attr[attr_idx++].id = SAI_ACL_SLICE_ATTR_EXTENSIONS_ACL_STAGE;
    sai_attr[attr_idx++].id = SAI_ACL_SLICE_ATTR_EXTENSIONS_USED_ACL_ENTRY;
    sai_attr[attr_idx++].id = SAI_ACL_SLICE_ATTR_EXTENSIONS_AVAILABLE_ACL_ENTRY;

    sai_attr[attr_idx].id = SAI_ACL_SLICE_ATTR_EXTENSIONS_ACL_TABLE_LIST;
    sai_attr[attr_idx].value.objlist.count = list_sz;
    sai_attr[attr_idx].value.objlist.list = &(slice_acl_tbl_obj_list[0]);

    attr_idx++;

    // Call SAI API
    sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->get_acl_slice_attribute(sai_slice_id,
                                                                     attr_idx, &sai_attr[0]);
    if ((sai_ret != SAI_STATUS_SUCCESS) && (sai_ret != SAI_STATUS_BUFFER_OVERFLOW)) {
        NDI_ACL_LOG_ERROR ("ACL slice attribute get failed for ID:0x%lx in SAI ret:%d",
                           slice_id, sai_ret);
        return _sai_to_ndi_err (sai_ret);
    }

    /* return the list count to caller in case of buffer overflow error or
     * if the list pointer is not valid.
     */
    if ((sai_ret == SAI_STATUS_BUFFER_OVERFLOW) ||
        (slice_attr->acl_table_list == NULL)) {
        slice_attr->acl_table_count = sai_attr[attr_idx-1].value.objlist.count;
        NDI_ACL_LOG_INFO ("ACL slice attribute get ACL table list failed for ID:0x%lx, list_count:%d in SAI ret:%d",
                           slice_id, slice_attr->acl_table_count, sai_ret);
        return STD_ERR_OK;
    }
    attr_count = attr_idx;

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(sai_attr[attr_idx].id) {
            case SAI_ACL_SLICE_ATTR_EXTENSIONS_SLICE_ID:
                slice_attr->slice_index = sai_attr[attr_idx].value.u32;
                break;
            case SAI_ACL_SLICE_ATTR_EXTENSIONS_PIPE_ID:
                slice_attr->pipeline_index = sai_attr[attr_idx].value.u32;
                break;
            case SAI_ACL_SLICE_ATTR_EXTENSIONS_ACL_STAGE:
                switch (sai_attr[attr_idx].value.u32) {
                    case SAI_ACL_STAGE_INGRESS:
                        slice_attr->stage = BASE_ACL_STAGE_INGRESS;
                        break;
                    case SAI_ACL_STAGE_EGRESS:
                        slice_attr->stage = BASE_ACL_STAGE_EGRESS;
                        break;
                    default:
                        NDI_ACL_LOG_ERROR ("Invalid Stage %d", sai_attr[attr_idx].value.u32);
                        break;
                }
                break;
            case SAI_ACL_SLICE_ATTR_EXTENSIONS_USED_ACL_ENTRY:
                slice_attr->used_count = sai_attr[attr_idx].value.u32;
                break;
            case SAI_ACL_SLICE_ATTR_EXTENSIONS_AVAILABLE_ACL_ENTRY:
                slice_attr->avail_count = sai_attr[attr_idx].value.u32;
                break;
            case SAI_ACL_SLICE_ATTR_EXTENSIONS_ACL_TABLE_LIST:
                if (slice_attr->acl_table_count < sai_attr[attr_idx].value.objlist.count) {
                    slice_attr->acl_table_count = sai_attr[attr_idx].value.objlist.count;
                } else {
                    slice_attr->acl_table_count = sai_attr[attr_idx].value.objlist.count;
                    for (size_t idx = 0; idx < slice_attr->acl_table_count; idx ++) {
                        slice_attr->acl_table_list[idx] = sai_attr[attr_idx].value.objlist.list[idx];
                    }
                }
                break;
            default:
                NDI_ACL_LOG_INFO ("ACL slice attribute get returned invalid attribute");
                break;
        }
    }

    NDI_ACL_LOG_INFO ("ACL slice attribute get success");
    return STD_ERR_OK;
}

t_std_error ndi_acl_get_acl_table_attribute (npu_id_t npu_id, ndi_obj_id_t table_id,
                                             ndi_acl_table_attr_t *table_attr)
{
#define NDI_MAX_ACL_TABLE_ATTR   2
    size_t                    attr_count = 0;
    size_t                    attr_idx = 0;
    size_t                    list_sz = table_attr->acl_table_used_entry_list_count;
    sai_attribute_t           sai_attr[NDI_MAX_ACL_TABLE_ATTR];
    sai_status_t              sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(ACL, FAIL, 0);
    }

    memset (&sai_attr, 0, sizeof (sai_attr));

    sai_object_id_t sai_table_id = ndi_acl_utl_ndi2sai_table_id(table_id);

    std::vector<uint32_t> used_entry_obj_list(list_sz);
    std::vector<uint32_t> avail_entry_obj_list(list_sz);

    sai_attr[attr_idx].id = SAI_ACL_TABLE_ATTR_EXTENSIONS_USED_ACL_ENTRY_LIST;
    sai_attr[attr_idx].value.u32list.count = list_sz;
    sai_attr[attr_idx].value.u32list.list = &(used_entry_obj_list[0]);
    attr_idx++;

    sai_attr[attr_idx].id = SAI_ACL_TABLE_ATTR_EXTENSIONS_AVAILABLE_ACL_ENTRY_LIST;
    sai_attr[attr_idx].value.u32list.count = list_sz;
    sai_attr[attr_idx].value.u32list.list = &(avail_entry_obj_list[0]);
    attr_idx++;

    attr_count = attr_idx;

    // Call SAI API
    sai_ret = ndi_acl_utl_api_get(ndi_db_ptr)->get_acl_table_attribute(sai_table_id,
                                                                       attr_idx, &sai_attr[0]);

    if ((sai_ret != SAI_STATUS_SUCCESS) &&
        (sai_ret != SAI_STATUS_BUFFER_OVERFLOW)) {
        NDI_ACL_LOG_ERROR ("ACL table attribute get used/avail entry count failed for ID:0x%lx in SAI %d",
                           sai_table_id, sai_ret);
        return _sai_to_ndi_err (sai_ret);
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {

        switch(sai_attr[attr_idx].id) {
            case SAI_ACL_TABLE_ATTR_EXTENSIONS_USED_ACL_ENTRY_LIST:
                if ((table_attr->acl_table_used_entry_list_count < sai_attr[attr_idx].value.u32list.count) ||
                    (table_attr->acl_table_used_entry_list == NULL)) {
                    table_attr->acl_table_used_entry_list_count = sai_attr[attr_idx].value.u32list.count;
                } else {
                    table_attr->acl_table_used_entry_list_count = sai_attr[attr_idx].value.u32list.count;
                    for (size_t idx = 0; idx < table_attr->acl_table_used_entry_list_count; idx ++) {
                        table_attr->acl_table_used_entry_list[idx] = sai_attr[attr_idx].value.u32list.list[idx];
                    }
                }
                break;
            case SAI_ACL_TABLE_ATTR_EXTENSIONS_AVAILABLE_ACL_ENTRY_LIST:
                if ((table_attr->acl_table_avail_entry_list_count < sai_attr[attr_idx].value.u32list.count) ||
                    (table_attr->acl_table_avail_entry_list == NULL)) {
                    table_attr->acl_table_avail_entry_list_count = sai_attr[attr_idx].value.u32list.count;
                } else {
                    table_attr->acl_table_avail_entry_list_count = sai_attr[attr_idx].value.u32list.count;
                    for (size_t idx = 0; idx < table_attr->acl_table_avail_entry_list_count; idx ++) {
                        table_attr->acl_table_avail_entry_list[idx] = sai_attr[attr_idx].value.u32list.list[idx];
                    }
                }
                break;

            default:
                NDI_ACL_LOG_INFO ("ACL table attribute get returned invalid attribute");
                break;
        }
    }

    NDI_ACL_LOG_INFO ("ACL table attribute get success");
    return STD_ERR_OK;
}

} // end Extern C
