/*
 * Copyright (c) 2019 Dell/EMC
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
 * filename: nas_ndi_trap.cpp
 */


#include "std_assert.h"
#include "dell-base-acl.h"
#include "dell-base-trap.h"
#include "nas_ndi_int.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_utils.h"
#include "nas_base_utils.h"
#include "nas_ndi_switch.h"
#include "nas_ndi_acl.h"
#include "nas_ndi_acl_utl.h"
#include "nas_ndi_trap.h"
#include "nas_ndi_udf_utl.h"
#include <vector>
#include <unordered_map>
#include <string.h>
#include <list>
#include <inttypes.h>

#define NAS_ACL_TRAP_GRP_ID_NONE           0

static inline t_std_error _sai_to_ndi_err (sai_status_t st)
{
    return ndi_utl_mk_std_err (e_std_err_ACL, st);
}

extern "C" {

// ======== Trap Group ==============================================================

t_std_error ndi_acl_trapgrp_create(npu_id_t npu_id, nas_acl_trap_attr_t *trap_attr_params, size_t plen,
                                   ndi_obj_id_t *ndi_trap_grp_p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_trap_grp = 0;

    sai_attribute_t *sai_attr_arr;
    size_t count, sai_attr_count = NAS_ACL_TRAP_PARAM_L;

    t_std_error ndi_err = STD_ERR_OK;

    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        NDI_ACL_LOG_ERROR("Invalid switch db");
        return STD_ERR(ACL, FAIL, 0);
    }

    if (ndi_trap_grp_p == NULL) {
        NDI_ACL_LOG_ERROR("Invalid trap group object");
        return STD_ERR(ACL, PARAM, 0);
    }

    sai_attr_arr = new sai_attribute_t[sai_attr_count];
    if (!sai_attr_arr) {
        NDI_ACL_LOG_ERROR("SAI attribute allocation");
        return STD_ERR(ACL, PARAM, 0);
    }
    
    memset(sai_attr_arr, 0, sizeof(sai_attribute_t) * sai_attr_count);
    count = 0;

    if (trap_attr_params[count].attr_id != BASE_TRAP_TRAP_GROUP_QUEUE_ID) {
        NDI_ACL_LOG_INFO("Mandatory queue id not present in create attribute[%d].id 0x%llx",
                         count, trap_attr_params[count].attr_id);
    } else {
        sai_attr_arr[count].id = SAI_HOSTIF_TRAP_GROUP_ATTR_QUEUE;
        sai_attr_arr[count].value.oid = ndi_acl_utl_ndi2sai_queue(npu_id, trap_attr_params[count].val.oid);
        count ++;

        /* When trap group create calls with admin state attribute */
        if ((plen > 1) && (trap_attr_params[count].attr_id == BASE_TRAP_TRAP_GROUP_ADMIN)) {
            sai_attr_arr[count].id = SAI_HOSTIF_TRAP_GROUP_ATTR_ADMIN_STATE;
            sai_attr_arr[count].value.booldata = trap_attr_params[count].val.u32;
            count ++;
        }

        if (!ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->create_hostif_trap_group) {
            ndi_err = STD_ERR(ACL, PARAM, 0);
        } else if (count) {
            sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->create_hostif_trap_group
                                (&sai_trap_grp, ndi_switch_id_get(), count, sai_attr_arr);
            if (sai_ret != SAI_STATUS_SUCCESS) {
                NDI_ACL_LOG_ERROR("Create ACL Trap grp attr failed in SAI, ret %d", sai_ret);
                ndi_err = _sai_to_ndi_err(sai_ret);
            } else {
                *ndi_trap_grp_p = ndi_acl_utl_sai2ndi_trap_grp(npu_id, sai_trap_grp);
                NDI_ACL_LOG_INFO("Successfully created trap grp - Count %d Return NDI ID %" PRIx64,
                                 count, *ndi_trap_grp_p);
            }
        }   
    }

    delete[] sai_attr_arr;
    return ndi_err;
}

t_std_error ndi_acl_trapgrp_set(npu_id_t npu_id, nas_acl_trap_attr_t *trap_attr_params, size_t plen,
                                ndi_obj_id_t ndi_trap_grp)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_trap_grp = 0;

    sai_attribute_t sai_attr;

    t_std_error ndi_err = STD_ERR_OK;

    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(ACL, FAIL, 0);
    }

    if (plen != 1) {
        return STD_ERR(ACL, PARAM, 0);
    }

    size_t count = 0;

    memset(&sai_attr, 0, sizeof(sai_attr));
    switch (trap_attr_params->attr_id) {
    case BASE_TRAP_TRAP_GROUP_QUEUE_ID:
        sai_attr.id = SAI_HOSTIF_TRAP_GROUP_ATTR_QUEUE;
        sai_attr.value.oid = ndi_acl_utl_ndi2sai_queue(npu_id, trap_attr_params->val.oid);
        count ++;

        break;

    case BASE_TRAP_TRAP_GROUP_ADMIN:
        sai_attr.id = SAI_HOSTIF_TRAP_GROUP_ATTR_ADMIN_STATE;
        sai_attr.value.booldata = trap_attr_params->val.u32;
        count ++;

        break;

    default:
        NDI_ACL_LOG_INFO("Unsupported set trap grp attr id %d vlen %d",
                          trap_attr_params->attr_id, trap_attr_params->vlen);
        break;
    }

    if (count != 1) { // SAI API allow one attribute
        NDI_ACL_LOG_ERROR("Set ACL Trap Grp set %" PRIx64 " did not provide attribute: count %d", ndi_trap_grp, count);
        return STD_ERR(ACL, PARAM, 0);
    } else if (! ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->set_hostif_trap_group_attribute) {
        NDI_ACL_LOG_ERROR("Set ACL Trap Grp set %" PRIx64 " not implemented", ndi_trap_grp);
        return STD_ERR(ACL, PARAM, 0);
    } else {
        sai_trap_grp = ndi_acl_utl_ndi2sai_trap_grp(npu_id, ndi_trap_grp);

        sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->set_hostif_trap_group_attribute
              (sai_trap_grp, &sai_attr);

        if (sai_ret != SAI_STATUS_SUCCESS) {
            NDI_ACL_LOG_ERROR("Set ACL Trap grp attr failed in SAI, ret %d", sai_ret);
            ndi_err = _sai_to_ndi_err(sai_ret);
        } else {
            NDI_ACL_LOG_INFO("Successfully Set ACL trap grp NDI ID %" PRIx64,
                             ndi_trap_grp);
        }
    }
    
    return ndi_err;
}


t_std_error ndi_acl_trapgrp_delete(npu_id_t npu_id, ndi_obj_id_t ndi_trap_grp)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_trap_grp = 0;
    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);

    t_std_error ndi_err = STD_ERR_OK;

    if (ndi_db_ptr == NULL) {
        return STD_ERR(ACL, FAIL, 0);
    }


    if (! ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->remove_hostif_trap_group) {
        NDI_ACL_LOG_ERROR("Set ACL %d Trap Grp set not implemented", ndi_trap_grp);
    } else {
        sai_trap_grp = ndi_acl_utl_ndi2sai_trap_grp(npu_id, ndi_trap_grp);

        sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->remove_hostif_trap_group
                    (sai_trap_grp);
        
        if (sai_ret != SAI_STATUS_SUCCESS) {
            NDI_ACL_LOG_ERROR("Remove ACL Trap Grp failed in SAI, ret %d", sai_ret);
            ndi_err = _sai_to_ndi_err(sai_ret);
        } else {
            NDI_ACL_LOG_INFO("Successfully deleted ACL Trap grp NDI ID %" PRIx64,
                             ndi_trap_grp);
        }
    }
    
    return ndi_err;
}


// ======== Trap ID ==============================================================
  

t_std_error ndi_acl_trapid_create(npu_id_t npu_id, nas_acl_trap_attr_t *trap_attr_params, size_t plen,
                                  ndi_obj_id_t *ndi_trap_id_p)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_trap_id = 0;

    sai_attribute_t *sai_attr_arr;
    size_t count, sai_attr_count = NAS_ACL_TRAP_PARAM_L;

    bool is_user_defined = false;

    t_std_error ndi_err = STD_ERR_OK;

    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(ACL, FAIL, 0);
    }

    if ((plen <= 0) || ndi_trap_id_p == NULL) {
        return STD_ERR(ACL, PARAM, 0);
    }

    sai_attr_arr = new sai_attribute_t[sai_attr_count];
    if (!sai_attr_arr)
        return STD_ERR(ACL, PARAM, 0);

    memset(sai_attr_arr, 0, sizeof(sai_attribute_t) * sai_attr_count);
    switch (trap_attr_params[0].attr_id) {
    case BASE_TRAP_TRAP_TYPE:
        if (BASE_TRAP_TRAP_TYPE_ACL == trap_attr_params[0].val.u32) {
            is_user_defined = true;
            sai_attr_arr[0].id =  SAI_HOSTIF_USER_DEFINED_TRAP_ATTR_TYPE;
            sai_attr_arr[0].value.u32 = SAI_HOSTIF_USER_DEFINED_TRAP_TYPE_ACL;

            if (!ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->create_hostif_user_defined_trap) {
                ndi_err = STD_ERR(ACL, PARAM, 0);
                break;
            }

        } else if (BASE_TRAP_TRAP_TYPE_SAMPLEPACKET == trap_attr_params[0].val.u32) {
            is_user_defined = false;
            sai_attr_arr[0].id =  SAI_HOSTIF_TRAP_ATTR_TRAP_TYPE;
            sai_attr_arr[0].value.s32 = SAI_HOSTIF_TRAP_TYPE_SAMPLEPACKET;

            if (!ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->create_hostif_trap) {
                ndi_err = STD_ERR(ACL, PARAM, 0);
                break;
            }
        }

        break;
    }

    count = 1; // 0 is taken for BASE_TRAP_TRAP_TYPE
    if (plen > 1) {
        switch (trap_attr_params[count].attr_id) {
        case BASE_TRAP_TRAP_GROUP:
            if (NAS_ACL_TRAP_GRP_ID_NONE == trap_attr_params[count].val.oid) {
                sai_attr_arr[count].value.oid = SAI_NULL_OBJECT_ID;

            } else {
                sai_attr_arr[count].value.oid = ndi_acl_utl_ndi2sai_trap_grp(npu_id, trap_attr_params[count].val.oid);
            }

            if (is_user_defined == false) {
                sai_attr_arr[count].id = SAI_HOSTIF_USER_DEFINED_TRAP_ATTR_TRAP_GROUP;
            } else {
                sai_attr_arr[count].id = SAI_HOSTIF_TRAP_ATTR_TRAP_GROUP;
            }
            
            count ++;

            break;

        default:
            break;
        }
    }

    if (is_user_defined == false) {
        sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->create_hostif_trap
                       (&sai_trap_id, ndi_switch_id_get(), count, sai_attr_arr);
    } else {
        sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->create_hostif_user_defined_trap
                       (&sai_trap_id, ndi_switch_id_get(), count, sai_attr_arr);
    }

    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_ACL_LOG_ERROR("Create ACL Trap ID failed in SAI, ret %d, %s",
                          sai_ret,  (is_user_defined)? "user-def": "pre-def");
        ndi_err = _sai_to_ndi_err(sai_ret);
    } else {
        *ndi_trap_id_p = ndi_acl_utl_sai2ndi_trap_id(npu_id, sai_trap_id);
        NDI_ACL_LOG_INFO("Successfully created trap - Return NDI ID %" PRIx64,
                         *ndi_trap_id_p);
    }
    
    delete[] sai_attr_arr;
    return ndi_err;
}

t_std_error ndi_acl_trapid_set(npu_id_t npu_id, nas_acl_trap_attr_t *trap_attr_params, size_t plen,
                               ndi_obj_id_t ndi_trap_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_trap_id = 0;

    sai_attribute_t sai_attr;
    size_t count = 0;
    bool is_user_defined = false;

    t_std_error ndi_err = STD_ERR_OK;

    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(ACL, FAIL, 0);
    }

    if (plen < 2) {
        return STD_ERR(ACL, PARAM, 0);
    }

    memset(&sai_attr, 0, sizeof(sai_attr));
    switch (trap_attr_params[0].attr_id) {
    case BASE_TRAP_TRAP_TYPE:
      if (trap_attr_params[0].val.u32 == BASE_TRAP_TRAP_TYPE_ACL) {
          is_user_defined = true;
      } else if (trap_attr_params[0].val.u32 == BASE_TRAP_TRAP_TYPE_SAMPLEPACKET) {
          is_user_defined = false;
      } else {
          ndi_err = STD_ERR(ACL, PARAM, 0);
          break;
      }
      break;

    default:
      break;
    }

    count = 1; // move the index; index = 0 was for the trap type

    switch (trap_attr_params[count].attr_id) {
    case BASE_TRAP_TRAP_TRAP_GROUP_ID:
        if (NAS_ACL_TRAP_GRP_ID_NONE == trap_attr_params[count].val.oid) {
            sai_attr.value.oid = SAI_NULL_OBJECT_ID;

        } else {
            sai_attr.value.oid = ndi_acl_utl_ndi2sai_trap_grp(npu_id, trap_attr_params[count].val.oid);
        }

        if (is_user_defined == true) {
            sai_attr.id = SAI_HOSTIF_USER_DEFINED_TRAP_ATTR_TRAP_GROUP;
        } else {
            sai_attr.id = SAI_HOSTIF_TRAP_ATTR_TRAP_GROUP;
        }
        
        break;

    default:
        break;
    }

    sai_trap_id = ndi_acl_utl_ndi2sai_trap_id(npu_id, ndi_trap_id);

    if (is_user_defined)
        sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->set_hostif_user_defined_trap_attribute
                    (sai_trap_id, &sai_attr);
    else // pre-defined
        sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->set_hostif_trap_attribute
                    (sai_trap_id, &sai_attr);

    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_ACL_LOG_ERROR("Set ACL trap group failed in SAI, ret 0x%x, attr id 0x%llx",
                          sai_ret, sai_attr.id);
        ndi_err = _sai_to_ndi_err(sai_ret);
    } else {
        NDI_ACL_LOG_INFO("Successfully Set ACL User defined trap NDI ID %" PRIx64,
                         ndi_trap_id);
    }
    
    return ndi_err;
}


t_std_error ndi_acl_trapid_delete(npu_id_t npu_id, nas_acl_trap_attr_t *trap_attr_params, ndi_obj_id_t ndi_trap_id)
{
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_object_id_t sai_trap_id = 0;
    nas_ndi_db_t* ndi_db_ptr = ndi_db_ptr_get(npu_id);
    bool is_user_defined = false;

    t_std_error ndi_err = STD_ERR_OK;

    if (ndi_db_ptr == NULL) {
        return STD_ERR(ACL, FAIL, 0);
    }

    switch (trap_attr_params->attr_id) {
        case BASE_TRAP_TRAP_TYPE:
            if (trap_attr_params->val.u32 == BASE_TRAP_TRAP_TYPE_ACL) {
                is_user_defined = true;
                if (!ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->remove_hostif_user_defined_trap) {
                    ndi_err = STD_ERR(ACL, PARAM, 0);
                    break;
                }

            } else if (trap_attr_params->val.u32 == BASE_TRAP_TRAP_TYPE_SAMPLEPACKET) {
                is_user_defined = false;
                if (!ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->remove_hostif_trap) {
                    ndi_err = STD_ERR(ACL, PARAM, 0);
                    break;
                }
            } else {
                ndi_err = STD_ERR(ACL, PARAM, 0);
                break;
            }

            break;

        default:
            NDI_ACL_LOG_ERROR("Delete ACL Trap ID failed for %" PRIx64, ndi_trap_id);
            ndi_err = STD_ERR(ACL, PARAM, 0);
            break;
    }

    if (ndi_err == STD_ERR_OK) {
        sai_trap_id = ndi_acl_utl_ndi2sai_trap_id(npu_id, ndi_trap_id);


        if (is_user_defined)
            sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->remove_hostif_user_defined_trap
                        (sai_trap_id);
        else // pre-defined
            sai_ret = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hostif_api_tbl->remove_hostif_trap
                        (sai_trap_id);

        if (sai_ret != SAI_STATUS_SUCCESS) {
            NDI_ACL_LOG_ERROR("Remove ACL User defined ID failed in SAI, ret %d", sai_ret);
            ndi_err = _sai_to_ndi_err(sai_ret);
        } else {
            NDI_ACL_LOG_INFO("Successfully deleted ACL User defined trap NDI ID %" PRIx64,
                             ndi_trap_id);
        }   
    }
    
    return ndi_err;
}
  
} // end Extern C
