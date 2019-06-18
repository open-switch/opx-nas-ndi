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
 * filename: nas_ndi_router_interface_utl.cpp
 */

#include "nas_ndi_event_logs.h"
#include "nas_ndi_router_interface_utl.h"
#include <iostream>
#include <ostream>
#include <iomanip>

static auto &ndi_rif_id_db = *(new ndi_rif_id_db_t);
static auto &ndi_rif_entry_db = *(new ndi_rif_db_t);

static char ndi_rif_scratch_buf [256];

const char *ndi_rif_entry_key_str(ndi_rif_entry_t *rif_entry)
{
    uint32_t len = 0;
    char mac_str[18] ={0};

    len = snprintf (ndi_rif_scratch_buf, 256,
            "VR OID:0x%lx, RIF_type:%d, Virtual:%d",
            rif_entry->vrf_id, rif_entry->rif_type,
            (rif_entry->flags & NDI_RIF_ATTR_VIRTUAL));

    if (rif_entry->flags & NDI_RIF_ATTR_VIRTUAL) {
        len += snprintf (ndi_rif_scratch_buf+len, 256-len,
                "MAC:%s",
                std_mac_to_string(&rif_entry->src_mac, mac_str,sizeof(mac_str)));
    }

    if (rif_entry->rif_type == NDI_RIF_TYPE_PORT) {
        len += snprintf (ndi_rif_scratch_buf+len, 256-len,
                "Port:%d, ", rif_entry->attachment.port_id.npu_port);
    } else if (rif_entry->rif_type == NDI_RIF_TYPE_LAG) {
        len += snprintf (ndi_rif_scratch_buf+len, 256-len,
                "LAG OID:0x%lx, ", rif_entry->attachment.lag_id);
    } else if (rif_entry->rif_type == NDI_RIF_TYPE_VLAN) {
        len += snprintf (ndi_rif_scratch_buf+len, 256-len,
                "VLAN:%d, ", rif_entry->attachment.vlan_id);
    } else if (rif_entry->rif_type == NDI_RIF_TYPE_DOT1D_BRIDGE) {
        len += snprintf (ndi_rif_scratch_buf+len, 256-len,
                "1DBridge OID:0x%lx, ", rif_entry->attachment.bridge_id);
    }
    return ndi_rif_scratch_buf;
}

bool ndi_rif_entry_db_key_gen (ndi_rif_entry_t *rif_entry, ndi_rif_entry_db_t&key)
{
    key.npu_id = rif_entry->npu_id;
    key.vrf_oid = rif_entry->vrf_id;
    key.rif_type = rif_entry->rif_type;

    key.is_virtual = (rif_entry->flags & NDI_RIF_ATTR_VIRTUAL);
    memcpy (key.src_mac, rif_entry->src_mac, sizeof (hal_mac_addr_t));

    if (key.rif_type == NDI_RIF_TYPE_PORT) {
        key.attachment.port_id.npu_id = rif_entry->attachment.port_id.npu_id;
        key.attachment.port_id.npu_port = rif_entry->attachment.port_id.npu_port;
    } else if (key.rif_type == NDI_RIF_TYPE_LAG) {
        key.attachment.lag_id = rif_entry->attachment.lag_id;
    } else if (key.rif_type == NDI_RIF_TYPE_VLAN) {
        key.attachment.vlan_id = rif_entry->attachment.vlan_id;
    } else if (key.rif_type == NDI_RIF_TYPE_DOT1D_BRIDGE) {
        key.attachment.bridge_id = rif_entry->attachment.bridge_id;
    }

    return true;
}

t_std_error ndi_rif_utl_create_rif (ndi_rif_entry_t *rif_entry)
{
    ndi_rif_entry_db_t key;

    memset(&key, 0, sizeof(key));

    if (ndi_rif_entry_db_key_gen (rif_entry, key) == false) {
        NDI_LOG_TRACE("NDI-ROUTE-RIF", "RIF entry key get "
                "failed for NPU-id:%d RIF info:%s",
                rif_entry->npu_id,
                ndi_rif_entry_key_str(rif_entry));

        return STD_ERR(ROUTE, FAIL, 0);
    }

    auto it = ndi_rif_entry_db.find(key);
    if (it != ndi_rif_entry_db.end()) {
        NDI_LOG_TRACE("NDI-ROUTE-RIF", "RIF entry is already present, "
                "increment reference count "
                "failed for NPU-id:%d RIF info:%s",
                rif_entry->npu_id,
                ndi_rif_entry_key_str(rif_entry));

        auto id_db_it = ndi_rif_id_db.find(it->second);
        ndi_rif_entry_db_t *rif_id_inst = (ndi_rif_entry_db_t*) id_db_it->second;

        rif_id_inst->ref_cnt++;

        return STD_ERR_OK;
    }
    ndi_rif_entry_db_t *rif_inst(new ndi_rif_entry_db_t);

    memset(rif_inst, 0, sizeof(ndi_rif_entry_db_t));

    rif_inst->npu_id = rif_entry->npu_id;
    rif_inst->vrf_oid = rif_entry->vrf_id;
    rif_inst->rif_type = rif_entry->rif_type;
    rif_inst->is_virtual = (rif_entry->flags & NDI_RIF_ATTR_VIRTUAL);
    memcpy (rif_inst->src_mac, rif_entry->src_mac, sizeof (hal_mac_addr_t));

    if (key.rif_type == NDI_RIF_TYPE_PORT) {
        rif_inst->attachment.port_id.npu_id = rif_entry->attachment.port_id.npu_id;
        rif_inst->attachment.port_id.npu_port = rif_entry->attachment.port_id.npu_port;
    } else if (key.rif_type == NDI_RIF_TYPE_LAG) {
        rif_inst->attachment.lag_id = rif_entry->attachment.lag_id;
    } else if (key.rif_type == NDI_RIF_TYPE_VLAN) {
        rif_inst->attachment.vlan_id = rif_entry->attachment.vlan_id;
    } else if (key.rif_type == NDI_RIF_TYPE_DOT1D_BRIDGE) {
        rif_inst->attachment.bridge_id = rif_entry->attachment.bridge_id;
    }
    rif_inst->ref_cnt = 1;

    ndi_rif_entry_db.insert(ndi_rif_entry_db_pair_t(*rif_inst, rif_entry->rif_id));

    auto id_db_it = ndi_rif_id_db.find(rif_entry->rif_id);
    if (id_db_it != ndi_rif_id_db.end()) {
        ndi_rif_id_db.erase(rif_entry->rif_id);
    }
    ndi_rif_id_db.insert(ndi_rif_id_db_pair_t(rif_entry->rif_id, rif_inst));

    return STD_ERR_OK;
}

t_std_error ndi_rif_utl_delete_rif(ndi_rif_id_t rif_id)
{
    auto it = ndi_rif_id_db.find(rif_id);
    if (it == ndi_rif_id_db.end()) {
        NDI_LOG_TRACE("NDI-ROUTE-RIF", "RIF entry is NOT present, "
                "in local cache for RIF:0x%lx", rif_id);

        return STD_ERR(ROUTE, FAIL, 0);
    }
    ndi_rif_entry_db_t *rif_inst = it->second;

    if (rif_inst != NULL) {
        rif_inst->ref_cnt--;

        if (!rif_inst->ref_cnt) {
            ndi_rif_id_db.erase(rif_id);

            ndi_rif_entry_db.erase(*rif_inst);
            delete rif_inst;
        }
    }

    return STD_ERR_OK;
}

t_std_error ndi_rif_utl_get_rif_id(ndi_rif_entry_t *rif_entry, ndi_rif_id_t *rif_id)
{
    ndi_rif_entry_db_t key;

    memset(&key, 0, sizeof(key));

    if (ndi_rif_entry_db_key_gen (rif_entry, key) == false) {
        NDI_LOG_TRACE("NDI-ROUTE-RIF", "RIF ID entry key get "
                "failed for NPU-id:%d RIF info:%s",
                rif_entry->npu_id,
                ndi_rif_entry_key_str(rif_entry));

        return STD_ERR(ROUTE, FAIL, 0);
    }

    auto it = ndi_rif_entry_db.find(key);
    if (it == ndi_rif_entry_db.end()) {
        NDI_LOG_TRACE("NDI-ROUTE-RIF", "RIF entry key NOT found "
                "in local cache, for NPU-id:%d RIF info:%s",
                rif_entry->npu_id,
                ndi_rif_entry_key_str(rif_entry));

        return STD_ERR(ROUTE, FAIL, 0);
    }
    *rif_id = (ndi_rif_id_t) it->second;

    return STD_ERR_OK;
}

t_std_error ndi_rif_utl_rif_ref (ndi_rif_id_t rif_id, ndi_rif_utl_op_t op, uint32_t *ref_cnt)
{
    auto it = ndi_rif_id_db.find(rif_id);
    if (it == ndi_rif_id_db.end()) {
        NDI_LOG_TRACE("NDI-ROUTE-RIF", "RIF entry is NOT present, "
                "in local cache for RIF:0x%lx", rif_id);

        return STD_ERR(ROUTE, FAIL, 0);
    }
    ndi_rif_entry_db_t *rif_inst = it->second;
    switch (op) {
        case NDI_RIF_REF_OP_INC:
            rif_inst->ref_cnt++;
            break;
        case NDI_RIF_REF_OP_DEC:
            rif_inst->ref_cnt--;
            break;
        case NDI_RIF_REF_OP_GET:
            break;
    }
    *ref_cnt = rif_inst->ref_cnt;

    return STD_ERR_OK;
}


void ndi_rif_utl_rif_cache_dump ()
{
    char mac_str[18] ={0};

    std::cout << "ndi_rif_id_db database dump: \n";

    auto it = ndi_rif_id_db.begin();
    for (; it != ndi_rif_id_db.end(); ++it) {
        ndi_obj_id_t rif_id = it->first;
        ndi_rif_entry_db_t *rif_inst = it->second;
        std::cout << "RIF-Id       : " << std::hex << rif_id << std::endl;
        std::cout << "    NPU      : " << std::dec << rif_inst->npu_id << std::endl;
        std::cout << "    VRF      : " << std::hex << rif_inst->vrf_oid << std::endl;

        std_mac_to_string(&rif_inst->src_mac, mac_str, sizeof(mac_str));
        std::cout << "    MAC      : " << mac_str << std::endl;

        std::cout << "    Virtual RIF: " << std::dec << rif_inst->is_virtual << std::endl;
        std::cout << "    ref_cnt  : " << std::dec << rif_inst->ref_cnt << std::endl;
        if (rif_inst->rif_type == NDI_RIF_TYPE_PORT) {
            std::cout << "    Type     : Port" << std::endl;
            std::cout << "    Port-id  : " << std::dec << rif_inst->attachment.port_id.npu_port << std::endl;
        } else if (rif_inst->rif_type == NDI_RIF_TYPE_LAG) {
            std::cout << "    Type     : LAG" << std::endl;
            std::cout << "    LAG-id   : " << std::hex << rif_inst->attachment.lag_id << std::endl;
        } else if (rif_inst->rif_type == NDI_RIF_TYPE_VLAN) {
            std::cout << "    Type     : VLAN" << std::endl;
            std::cout << "    VLAN-id  : " << std::dec << rif_inst->attachment.vlan_id << std::endl;
        } else if (rif_inst->rif_type == NDI_RIF_TYPE_DOT1D_BRIDGE) {
            std::cout << "    Type     : 1D BRIDGE" << std::endl;
            std::cout << "    Bridge-id  : " << std::dec << rif_inst->attachment.bridge_id << std::endl;
        }
    }

    std::cout << "\n\nndi_rif_entry_db database dump: \n";

    auto entry_it = ndi_rif_entry_db.begin();
    for (; entry_it != ndi_rif_entry_db.end(); ++entry_it) {
        ndi_obj_id_t rif_id = entry_it->second;
        std::cout << "RIF-Id       : " << std::hex << rif_id << std::endl;
    }
    std::cout << std::resetiosflags(std::ios_base::basefield) << '\n' << std::endl;
}
