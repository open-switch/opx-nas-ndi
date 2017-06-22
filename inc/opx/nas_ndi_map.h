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
 * nas_ndi_map.h
 */

#ifndef _NAS_NDI_MAP_H_
#define _NAS_NDI_MAP_H_

#include "std_error_codes.h"
#include "saitypes.h"

typedef enum {
    /*
     * nas_ndi_map_key_t.id1: Next Hop Group Object Id,
     * nas_ndi_map_data_t.val1: SAI NH Object Id,
     * nas_ndi_map_data_t.val2: SAI NH Member Object Id
     *
     * saiNhGroupOid --> list <{saiNhOid, saiNhMemberOid}>
     */
    NAS_NDI_MAP_TYPE_NH_GRP_MEMBER,

    /*
     * nas_ndi_map_key_t.id1: NPU Id (32 bit) + VLAN Id (32 bit)
     * nas_ndi_map_data_t.val1: VLAN SAI Object Id
     *
     * Used to retrieve SAI vlan id given vlan id and npu id
     */
    NAS_NDI_MAP_TYPE_NPU_VLAN_ID,

    /*
     * nas_ndi_map_key_t.id1: NPU Id (32 bit) + VLAN Id (32 bit)
     * nas_ndi_map_key_t.id2: SAI Port Object Id,
     * nas_ndi_map_data_t.val1: VLAN Member SAI Object Id
     * nas_ndi_map_data_t.val2: VLAN Member tagging mode
     *
     * Used to retrieve SAI member id given vlan id, npu id and port id
     */
    NAS_NDI_MAP_TYPE_VLAN_MEMBER_ID,

    /*
     * nas_ndi_map_key_t.id1: NPU Id (32 bit) + VLAN Id (32 bit)
     * nas_ndi_map_data_t.val1: SAI Port Object Id,
     * nas_ndi_map_data_t.val2: VLAN Member SAI Object Id
     *
     * Used to retrieve SAI member list given vlan id and npu id
     */
    NAS_NDI_MAP_TYPE_VLAN_PORTS,

    /*
     * nas_ndi_map_key_t.id1: SAI STP Object Id
     * nas_ndi_map_key_t.id2: SAI Port Object Id
     * nas_ndi_map_data_t.val1: SAI STP Port Object Id
     *
     * Used to retrieve SAI STP port object Id given STP Object Id and
     * Port Object Id.
     */
    NAS_NDI_MAP_TYPE_SAI_PORT_ID,

} nas_ndi_map_type_t;

typedef struct _nas_ndi_map_key_t {
    /* Used to determine which of the subsequent fields are valid. */
    nas_ndi_map_type_t type;
    sai_object_id_t    id1;
    sai_object_id_t    id2;
} nas_ndi_map_key_t;

typedef enum {
    /*
     * Bitmap values specifying which fields in 'nas_ndi_map_val_t'
     * should be matched.
     */
    NAS_NDI_MAP_VAL_FILTER_NONE = 0x00000001,
    NAS_NDI_MAP_VAL_FILTER_VAL1 = 0x00000002,
    NAS_NDI_MAP_VAL_FILTER_VAL2 = 0x00000004,
} nas_ndi_map_val_filter_t;

typedef struct _nas_ndi_map_data_t {
    sai_object_id_t val1;
    sai_object_id_t val2;
} nas_ndi_map_data_t;

typedef struct _nas_ndi_map_val_t {
    /**
     * - In get operation, the calling function provides the buffer for the
     *   'data' field. The 'count' specifies the size of the list. If there
     *   are more elements to be filled, than the buffer could accomodate,
     *   then the function returns 'STD_ERR (NPU, NOMEM, 0)' and fills the
     *   'count' field with the total number of elements. The calling
     *   function must then invoke the function again, with the sufficient
     *   buffer to accomodate the elements.
     *
     * - In NON-Get operation, the calling function must set the 'data'
     *   field to the actual number of valid elements in the 'data' field.
     *
     * NOTE:
     *   The memory pointed to by 'data' must be freed by the calling function.
     */
    size_t              count;
    nas_ndi_map_data_t *data;
} nas_ndi_map_val_t;

#ifdef __cplusplus
extern "C"{
#endif

t_std_error nas_ndi_map_insert (nas_ndi_map_key_t *key, nas_ndi_map_val_t *value);

t_std_error nas_ndi_map_delete (nas_ndi_map_key_t *key);

t_std_error nas_ndi_map_delete_elements (nas_ndi_map_key_t        *key,
                                         nas_ndi_map_val_t        *value,
                                         nas_ndi_map_val_filter_t  filter);

t_std_error nas_ndi_map_get (nas_ndi_map_key_t *key, nas_ndi_map_val_t *val);

t_std_error nas_ndi_map_get_elements (nas_ndi_map_key_t        *key,
                                      nas_ndi_map_val_t        *value,
                                      nas_ndi_map_val_filter_t  filter);

t_std_error nas_ndi_map_get_val_count (nas_ndi_map_key_t *key, size_t *count);

#ifdef __cplusplus
}
#endif

#endif  /* _NAS_NDI_MAP_H_ */


