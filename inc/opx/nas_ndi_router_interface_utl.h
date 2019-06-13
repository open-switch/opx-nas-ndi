/*
 * Copyright (c) 2019 Dell Inc.
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
 * filename: nas_ndi_router_interface_utl.h
 */

#ifndef __NAS_NDI_ROUTER_INTERFACE_UTL_H__
#define __NAS_NDI_ROUTER_INTERFACE_UTL_H__

#include "nas_ndi_router_interface.h"
#include "std_mac_utils.h"

#ifdef __cplusplus

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <memory>
#include <deque>
#include <utility>

typedef struct _ndi_rif_entry_key_t {
    npu_id_t               npu_id;
    ndi_vrf_id_t           vrf_oid;
    ndi_rif_type           rif_type;
    uint32_t               ref_cnt;
    bool                   is_virtual;
    hal_mac_addr_t         src_mac;

    union {
        ndi_port_t          port_id;
        hal_vlan_id_t       vlan_id;
        ndi_obj_id_t        lag_id;
        nas_bridge_id_t     bridge_id;
    } attachment;

} ndi_rif_entry_db_t;


struct ndi_rif_entry_key_hash {
    size_t operator()(const ndi_rif_entry_db_t& key) const {
        size_t hash = 0;
        hash ^= (std::hash<npu_id_t>()(key.npu_id) << 1);
        hash ^= (std::hash<hal_vrf_id_t>()(key.vrf_oid) << 1);
        hash ^= (std::hash<ndi_rif_type>()(key.rif_type) << 1);
        hash ^= (std::hash<bool>()(key.is_virtual) << 1);

        //RIF MAC check for key is used in validation only for virtual RIF for now.
        if (key.is_virtual) {
            hash ^= (std::hash<std::string>()(std::string{std::begin(key.src_mac), std::end(key.src_mac) - 1}) << 1);
        }
        if (key.rif_type == NDI_RIF_TYPE_PORT) {
            hash ^= (std::hash<npu_port_t>()(key.attachment.port_id.npu_port) << 1);
        } else if (key.rif_type == NDI_RIF_TYPE_LAG) {
            hash ^= (std::hash<hal_vlan_id_t>()(key.attachment.vlan_id) << 1);
        } else if (key.rif_type == NDI_RIF_TYPE_VLAN) {
            hash ^= (std::hash<ndi_obj_id_t>()(key.attachment.lag_id) << 1);
        } else if (key.rif_type == NDI_RIF_TYPE_DOT1D_BRIDGE) {
            hash ^= (std::hash<ndi_obj_id_t>()(key.attachment.bridge_id) << 1);
        }
        return hash;
    }
};

struct ndi_rif_entry_key_equal {
    bool operator()(const ndi_rif_entry_db_t& k1, const ndi_rif_entry_db_t& k2) const{

        //RIF MAC check for key is used in validation only for virtual RIF for now.
        if ((k1.npu_id != k2.npu_id) || (k1.vrf_oid != k2.vrf_oid) ||
            (k1.rif_type != k2.rif_type) ||
            (k1.is_virtual != k2.is_virtual) ||
            ((k1.is_virtual) && (k2.is_virtual) &&
             (memcmp(k1.src_mac, k2.src_mac, sizeof(hal_mac_addr_t)))) ||
            (k1.attachment.port_id.npu_port !=
                k2.attachment.port_id.npu_port) ||
            (k1.attachment.vlan_id != k2.attachment.vlan_id) ||
            (k1.attachment.bridge_id != k2.attachment.bridge_id) ||
            (k1.attachment.lag_id != k2.attachment.lag_id)) {
            return false;
        }
        return true;
    }
};

typedef std::unordered_map<ndi_rif_id_t, ndi_rif_entry_db_t*> ndi_rif_id_db_t;
typedef std::pair<ndi_rif_id_t, ndi_rif_entry_db_t*> ndi_rif_id_db_pair_t;

typedef std::unordered_map<ndi_rif_entry_db_t, ndi_rif_id_t,
                           ndi_rif_entry_key_hash, ndi_rif_entry_key_equal> ndi_rif_db_t;
typedef std::pair<ndi_rif_entry_db_t, ndi_rif_id_t> ndi_rif_entry_db_pair_t;


extern "C" {
#endif

typedef enum _ndi_rif_utl_op_t {
    NDI_RIF_REF_OP_INC = 1,
    NDI_RIF_REF_OP_DEC = 2,
    NDI_RIF_REF_OP_GET = 3
} ndi_rif_utl_op_t;


t_std_error ndi_rif_utl_create_rif (ndi_rif_entry_t *rif_entry);
t_std_error ndi_rif_utl_delete_rif(ndi_rif_id_t rif_id);
t_std_error ndi_rif_utl_get_rif_id(ndi_rif_entry_t *rif_entry, ndi_rif_id_t *rif_id);
t_std_error ndi_rif_utl_rif_ref (ndi_rif_id_t rif_id, ndi_rif_utl_op_t op, uint32_t *ref_cnt);

#ifdef __cplusplus
}
#endif

#endif
