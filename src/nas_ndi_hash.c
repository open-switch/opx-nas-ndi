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
 * filename: nas_ndi_hash.c
 */


#include "std_error_codes.h"
#include "std_assert.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_utils.h"
#include "dell-base-hash.h"

#include "sai.h"
#include "saiswitch.h"
#include "saihash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <inttypes.h>

#define NAS_SWITCH_DEFAULT_HASH_FIELDS_COUNT 5


int32_t nas_ndi_translate_traffic (int32_t nas_traffic)
{
    int32_t sai_traffic = -1;

    switch(nas_traffic) {
    case BASE_TRAFFIC_HASH_TRAFFIC_ECMP_NON_IP:
        sai_traffic = SAI_SWITCH_ATTR_ECMP_HASH;
        break;
    case BASE_TRAFFIC_HASH_TRAFFIC_LAG_NON_IP:
        sai_traffic = SAI_SWITCH_ATTR_LAG_HASH;
        break;
    case BASE_TRAFFIC_HASH_TRAFFIC_ECMP_IPV4:
        sai_traffic = SAI_SWITCH_ATTR_ECMP_HASH_IPV4;
        break;
    case BASE_TRAFFIC_HASH_TRAFFIC_ECMP_IPV4_IN_IPV4:
        sai_traffic = SAI_SWITCH_ATTR_ECMP_HASH_IPV4_IN_IPV4;
        break;
    case BASE_TRAFFIC_HASH_TRAFFIC_ECMP_IPV6:
        sai_traffic = SAI_SWITCH_ATTR_ECMP_HASH_IPV6;
        break;
    case BASE_TRAFFIC_HASH_TRAFFIC_LAG_IPV4:
        sai_traffic = SAI_SWITCH_ATTR_LAG_HASH_IPV4;
        break;
    case BASE_TRAFFIC_HASH_TRAFFIC_LAG_IPV4_IN_IPV4:
        sai_traffic = SAI_SWITCH_ATTR_LAG_HASH_IPV4_IN_IPV4;
        break;
    case BASE_TRAFFIC_HASH_TRAFFIC_LAG_IPV6:
        sai_traffic = SAI_SWITCH_ATTR_LAG_HASH_IPV6;
        break;
    default:
        /* This cannot happen */
        EV_LOGGING(NDI, WARNING, "NAS-HASH",
                   "Invalid NAS traffic type %d", (int) nas_traffic);
        break;
    }

    return sai_traffic;
}


int32_t nas_ndi_translate_nas_field (int32_t nas_field)
{
    int32_t sai_field = -1;

    switch(nas_field) {
    case BASE_TRAFFIC_HASH_FIELD_SRC_IP:
        sai_field = SAI_NATIVE_HASH_FIELD_SRC_IP;
        break;
    case BASE_TRAFFIC_HASH_FIELD_DEST_IP:
        sai_field = SAI_NATIVE_HASH_FIELD_DST_IP;
        break;
    case BASE_TRAFFIC_HASH_FIELD_INNER_SRC_IP:
        sai_field = SAI_NATIVE_HASH_FIELD_INNER_SRC_IP;
        break;
    case BASE_TRAFFIC_HASH_FIELD_INNER_DST_IP:
        sai_field = SAI_NATIVE_HASH_FIELD_INNER_DST_IP;
        break;
    case BASE_TRAFFIC_HASH_FIELD_VLAN_ID:
        sai_field = SAI_NATIVE_HASH_FIELD_VLAN_ID;
        break;
    case BASE_TRAFFIC_HASH_FIELD_IP_PROTOCOL:
        sai_field = SAI_NATIVE_HASH_FIELD_IP_PROTOCOL;
        break;
    case BASE_TRAFFIC_HASH_FIELD_ETHERTYPE:
        sai_field = SAI_NATIVE_HASH_FIELD_ETHERTYPE;
        break;
    case BASE_TRAFFIC_HASH_FIELD_L4_SRC_PORT:
        sai_field = SAI_NATIVE_HASH_FIELD_L4_SRC_PORT;
        break;
    case BASE_TRAFFIC_HASH_FIELD_L4_DEST_PORT:
        sai_field = SAI_NATIVE_HASH_FIELD_L4_DST_PORT;
        break;
    case BASE_TRAFFIC_HASH_FIELD_SRC_MAC:
        sai_field = SAI_NATIVE_HASH_FIELD_SRC_MAC;
        break;
    case BASE_TRAFFIC_HASH_FIELD_DEST_MAC:
        sai_field = SAI_NATIVE_HASH_FIELD_DST_MAC;
        break;
    case BASE_TRAFFIC_HASH_FIELD_IN_PORT:
        sai_field = SAI_NATIVE_HASH_FIELD_IN_PORT;
        break;
    default:
        /* This cannot happen */
        EV_LOGGING(NDI, WARNING, "NAS-HASH",
                   "Invalid NAS hash field %d", (int) nas_field);
        break;
    }

    return sai_field;
}


int32_t nas_ndi_translate_sai_field (int32_t sai_field)
{
    int32_t nas_field = -1;

    switch(sai_field) {
    case SAI_NATIVE_HASH_FIELD_SRC_IP:
        nas_field = BASE_TRAFFIC_HASH_FIELD_SRC_IP;
        break;
    case SAI_NATIVE_HASH_FIELD_DST_IP:
        nas_field = BASE_TRAFFIC_HASH_FIELD_DEST_IP;
        break;
    case SAI_NATIVE_HASH_FIELD_INNER_SRC_IP:
        nas_field = BASE_TRAFFIC_HASH_FIELD_INNER_SRC_IP;
        break;
    case SAI_NATIVE_HASH_FIELD_INNER_DST_IP:
        nas_field = BASE_TRAFFIC_HASH_FIELD_INNER_DST_IP;
        break;
    case SAI_NATIVE_HASH_FIELD_VLAN_ID:
        nas_field = BASE_TRAFFIC_HASH_FIELD_VLAN_ID;
        break;
    case SAI_NATIVE_HASH_FIELD_IP_PROTOCOL:
        nas_field = BASE_TRAFFIC_HASH_FIELD_IP_PROTOCOL;
        break;
    case SAI_NATIVE_HASH_FIELD_ETHERTYPE:
        nas_field = BASE_TRAFFIC_HASH_FIELD_ETHERTYPE;
        break;
    case SAI_NATIVE_HASH_FIELD_L4_SRC_PORT:
        nas_field = BASE_TRAFFIC_HASH_FIELD_L4_SRC_PORT;
        break;
    case SAI_NATIVE_HASH_FIELD_L4_DST_PORT:
        nas_field = BASE_TRAFFIC_HASH_FIELD_L4_DEST_PORT;
        break;
    case SAI_NATIVE_HASH_FIELD_SRC_MAC:
        nas_field = BASE_TRAFFIC_HASH_FIELD_SRC_MAC;
        break;
    case SAI_NATIVE_HASH_FIELD_DST_MAC:
        nas_field = BASE_TRAFFIC_HASH_FIELD_DEST_MAC;
        break;
    case SAI_NATIVE_HASH_FIELD_IN_PORT:
        nas_field = BASE_TRAFFIC_HASH_FIELD_IN_PORT;
        break;
    default:
        /* This cannot happen */
        EV_LOGGING(NDI, WARNING, "NAS-HASH",
                   "Invalid SAI hash field %d", (int) nas_field);
        break;
    }

    return nas_field;
}


t_std_error nas_ndi_create_hash_object (uint32_t sai_traffic,
                                        sai_switch_api_t *sai_switch_api,
                                        sai_hash_api_t *sai_hash_api,
                                        uint32_t attr_count,
                                        sai_attribute_t *hash_attr)
{
    sai_object_id_t obj_id = 0;
    sai_attribute_t switch_attr;
    sai_status_t    status;

    status = sai_hash_api->create_hash(&obj_id, ndi_switch_id_get(),
                                       attr_count, hash_attr);
    if (status == SAI_STATUS_NOT_SUPPORTED) {
        /* Log it and keep going */
        EV_LOGGING(NDI, INFO, "NAS-HASH", "Not supported in hardware");
        return STD_ERR_OK;
    } else if (status != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to create hash object");
        return STD_ERR(NPU, PARAM, 0);
    }

    /*
     * Store this oid in the corresponding switch attribute
     */
    switch_attr.id = sai_traffic;
    switch_attr.value.oid = obj_id;

    status = sai_switch_api->set_switch_attribute(ndi_switch_id_get(),&switch_attr);
    if (status != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to set switch attribute");
        return STD_ERR(NPU, PARAM, 0);
    }

    return STD_ERR_OK;
}


t_std_error nas_ndi_create_all_hash_objects (void)

{
    sai_switch_api_t  *sai_switch_api = NULL;
    sai_hash_api_t    *sai_hash_api = NULL;
    nas_ndi_db_t      *ndi_db_ptr;
    uint32_t          attr_count = 1;
    sai_attribute_t   hash_attr;
    uint32_t          nas_traffic;
    t_std_error       rc;
    int32_t           s32list[NAS_SWITCH_DEFAULT_HASH_FIELDS_COUNT] =
        {
            SAI_NATIVE_HASH_FIELD_SRC_IP,
            SAI_NATIVE_HASH_FIELD_DST_IP,
            SAI_NATIVE_HASH_FIELD_L4_SRC_PORT,
            SAI_NATIVE_HASH_FIELD_L4_DST_PORT,
            SAI_NATIVE_HASH_FIELD_IP_PROTOCOL
        };

    /*
     * Get pointers to the switch and hash API tables
     */
    ndi_db_ptr = ndi_db_ptr_get(0);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get db pointer");
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_switch_api = ndi_db_ptr->ndi_sai_api_tbl.n_sai_switch_api_tbl;

    sai_hash_api = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hash_api_tbl;

    if ((sai_switch_api == NULL) || (sai_hash_api == NULL)) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get API tables");
        return STD_ERR(NPU, PARAM, 0);
    }

    hash_attr.id = SAI_HASH_ATTR_NATIVE_HASH_FIELD_LIST;
    hash_attr.value.s32list.count = NAS_SWITCH_DEFAULT_HASH_FIELDS_COUNT;
    hash_attr.value.s32list.list = s32list;

    /*
     * SAI has already created hash objects for SAI_SWITCH_ATTR_ECMP_HASH and
     * SAI_SWITCH_ATTR_LAG_HASH, so we must create hash objects for the
     * remaining supported traffic types.
     */
    for (nas_traffic = BASE_TRAFFIC_HASH_TRAFFIC_ECMP_IPV4;
         nas_traffic <= BASE_TRAFFIC_HASH_TRAFFIC_LAG_IPV6;
         nas_traffic++) {
        rc = nas_ndi_create_hash_object(nas_ndi_translate_traffic(nas_traffic),
                                        sai_switch_api,
                                        sai_hash_api,
                                        attr_count, &hash_attr);
        /* if the HW doesn't support it, log it and move on */
        if (rc != STD_ERR_OK) {
            EV_LOGGING(NDI, ERR, "NAS-HASH",
                       "Unable to create hash object %d", nas_traffic);
        }
    }

    EV_LOGGING(NDI, INFO, "NAS-HASH", "Created hash objects");

    return STD_ERR_OK;
}


t_std_error nas_ndi_set_hash_obj (uint32_t nas_traffic, uint32_t count,
                                  uint32_t *lst)
{
    sai_hash_api_t    *sai_hash_api_tbl = NULL;
    sai_switch_api_t  *sai_switch_api_tbl = NULL;
    nas_ndi_db_t      *ndi_db_ptr;
    uint32_t          attr_count = 1;
    sai_attribute_t   hash_attr, switch_attr;
    sai_status_t      status;
    int32_t           s32list[BASE_TRAFFIC_HASH_FIELD_MAX];
    uint32_t          i, j = 0;


    /*
     * Setup
     */
    ndi_db_ptr = ndi_db_ptr_get(0);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get db pointer");
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_switch_api_tbl = ndi_db_ptr->ndi_sai_api_tbl.n_sai_switch_api_tbl;

    sai_hash_api_tbl = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hash_api_tbl;

    if ((sai_switch_api_tbl == NULL) || (sai_hash_api_tbl == NULL)) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get API tables");
        return STD_ERR(NPU, PARAM, 0);
    }


    /*
     * Obtain the hash object's ID
     */
    switch_attr.value.oid = SAI_NULL_OBJECT_ID;
    switch_attr.id = nas_ndi_translate_traffic(nas_traffic);

    status = sai_switch_api_tbl->get_switch_attribute(ndi_switch_id_get(),
                                                    attr_count, &switch_attr);
    if (status != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get hash object ID");
        return STD_ERR(NPU, PARAM, 0);
    }

    memset(s32list, 0, sizeof(s32list));

    for (i = 0; i < count; i++) {
        if (lst[i]) {
            j++;
            s32list[i] = nas_ndi_translate_nas_field(lst[i]);
        }
    }

    hash_attr.id = SAI_HASH_ATTR_NATIVE_HASH_FIELD_LIST;
    hash_attr.value.s32list.count = j;
    hash_attr.value.s32list.list = s32list;

    /*
     * Set the hash fields
     */
    status = sai_hash_api_tbl->set_hash_attribute(switch_attr.value.oid,
                                                  &hash_attr);
    if (status != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to set hash fields");
        return STD_ERR(NPU, PARAM, 0);
    }

    return STD_ERR_OK;
}


t_std_error nas_ndi_get_hash (uint64_t nas_traffic, uint32_t *count, uint32_t *lst)
{
    nas_ndi_db_t      *ndi_db_ptr;
    sai_switch_api_t  *sai_switch_api_tbl = NULL;
    sai_hash_api_t    *sai_hash_api_tbl = NULL;
    uint32_t          attr_count = 1;
    sai_attribute_t   hash_attr, switch_attr;
    sai_status_t      status;
    int32_t           s32list[BASE_TRAFFIC_HASH_FIELD_MAX] = {-1};
    uint32_t          i;

    /*
     * Setup
     */
    ndi_db_ptr = ndi_db_ptr_get(0);
    if (ndi_db_ptr == NULL) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get db pointer");
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_switch_api_tbl = ndi_db_ptr->ndi_sai_api_tbl.n_sai_switch_api_tbl;

    sai_hash_api_tbl = ndi_db_ptr->ndi_sai_api_tbl.n_sai_hash_api_tbl;

    if ((sai_switch_api_tbl == NULL) || (sai_hash_api_tbl == NULL)) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get API tables");
        return STD_ERR(NPU, PARAM, 0);
    }

    /*
     * Get the ID of the hash object for this traffic type
     */
    switch_attr.value.oid = SAI_NULL_OBJECT_ID;
    switch_attr.id = nas_ndi_translate_traffic(nas_traffic);

    status = sai_switch_api_tbl->get_switch_attribute(ndi_switch_id_get(),
                                                    attr_count, &switch_attr);
    if (status != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get hash object's ID");
        return STD_ERR(NPU, PARAM, 0);
    }

    /*
     * Call the SAI GET_ATTRIBUTE function for the standard fields
     */
    hash_attr.id = SAI_HASH_ATTR_NATIVE_HASH_FIELD_LIST;
    hash_attr.value.s32list.count = BASE_TRAFFIC_HASH_FIELD_MAX;
    hash_attr.value.s32list.list = s32list;

    status = sai_hash_api_tbl->get_hash_attribute(switch_attr.value.oid,
                                                  attr_count, &hash_attr);
    if (status != SAI_STATUS_SUCCESS) {
        EV_LOGGING(NDI, ERR, "NAS-HASH", "Failed to get hash fields");
        return STD_ERR(NPU, PARAM, 0);
    }

    /*
     * Fill in the arrays which will be used to pass the values back to
     * CPS
     */
    for (i = 0; i < hash_attr.value.s32list.count; i++) {
        if (s32list[i] >= 0) {
            lst[i] = nas_ndi_translate_sai_field(s32list[i]);
        }
    }

    *count = hash_attr.value.s32list.count;

    return STD_ERR_OK;
}
