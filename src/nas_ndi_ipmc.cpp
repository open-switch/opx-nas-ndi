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
 * filename: nas_ndi_ipmc.cpp
 */

#include "saiipmcgroupextensions.h"
#include "sairpfgroupextensions.h"
#include "nas_ndi_ipmc_utl.h"
#include "nas_ndi_event_logs.h"
#include "nas_ndi_bridge_port.h"

#include <functional>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <inttypes.h>

ndi_cache_grp_mbr_t::operator std::string() const
{
    std::ostringstream ss;
    ss << "rif-" << std::hex << std::showbase << rif_id << " port-(";
    ss << std::dec << std::noshowbase;
    for (auto& port: port_list) {
        if (port.port_type == NDI_SW_PORT_NPU_PORT) {
            ss << "P:" << port.u.npu_port.npu_port;
        } else {
            ss << "L:" << port.u.lag;
        }
        ss << ",";
    }
    ss << ")";
    return ss.str();
}

ndi_cache_repl_grp_t::operator std::string() const
{
    std::ostringstream ss;
    ss << "[IIF: ";
    for (auto& rpf_mbr: rpf_mbr_list) {
        ss << std::string(rpf_mbr.second) << " ";
    }
    ss << " OIF: ";
    for (auto& ipmc_mbr: ipmc_mbr_list) {
        ss << std::string(ipmc_mbr.second) << " ";
    }
    if (!ipmc_ipv4_entry_list.empty()) {
        ss << "IPv4 ent: " << ipmc_ipv4_entry_list.size() << " ";
    }
    if (!ipmc_ipv6_entry_list.empty()) {
        ss << "IPv6 ent: " << ipmc_ipv6_entry_list.size() << " ";
    }
    ss << "]";
    return ss.str();
}

template<typename T>
cached_ipmc_af_entry<T>::operator std::string() const
{
    std::ostringstream ss;
    ss << std::hex << std::showbase << "[VRF " << vrf_id();
    hal_ip_addr_t ip_addr;
    convert_to_common_ip(ip_addr, _dst_ip);
    ss << " GRP " << ndi_ipmc_ip_to_string(ip_addr);
    if (entry_type() == NAS_NDI_IPMC_ENTRY_TYPE_SG) {
        convert_to_common_ip(ip_addr, _src_ip);
        ss << " SRC " << ndi_ipmc_ip_to_string(ip_addr);
    } else {
        ss << " SRC *";
    }
    if (copy_to_cpu()) {
        ss << " TO_CPU";
    }
    ss << "]";
    return ss.str();
}

template<typename T>
cached_ipmc_entry_set<T>::operator std::string() const
{
    std::ostringstream ss;
    for (auto& entry: _entry_list) {
        ss << "NPU-" << entry.first << " Entry-" << std::string(entry.second) << std::endl;
    }
    return ss.str();
}

template<typename T>
cached_ipmc_entry_map<T>::operator std::string() const
{
    std::ostringstream ss;
    for (auto& entry: _entry_list) {
        ss << "NPU-" << entry.first.first << " Entry-" << std::string(entry.first.second);
        ss << std::hex << std::showbase;
        ss << " Repl_Group-" << entry.second->repl_grp_id << std::endl;
        ss << std::dec << std::noshowbase;
    }
    return ss.str();
}

bool repl_group_cache::add_repl_group(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                                      ndi_obj_id_t rpf_grp_id, ndi_obj_id_t ipmc_grp_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_repl_grp_list.find(std::make_pair(npu_id, repl_grp_id)) != _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld already exists in cache", repl_grp_id);
        return false;
    }
    _repl_grp_list.insert({std::make_pair(npu_id, repl_grp_id),
                           std::make_shared<ndi_cache_repl_grp_t>(repl_grp_id, rpf_grp_id, ipmc_grp_id)});
    return true;
}

bool repl_group_cache::add_rpf_group_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id, ndi_obj_id_t mbr_id,
                                            const ndi_mc_grp_mbr_t& grp_mbr)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return false;
    }
    if (!_repl_grp_list[grp_key]->rpf_mbr_list.empty()) {
        NDI_MCAST_LOG_ERROR("Could not add multiple RPF group to replication group");
        return false;
    }

    _repl_grp_list[grp_key]->rpf_mbr_list.emplace(grp_mbr.rif_id, ndi_cache_grp_mbr_t{mbr_id, grp_mbr.rif_id, grp_mbr.port_list});
    return true;
}

bool repl_group_cache::add_ipmc_group_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id, ndi_obj_id_t mbr_id,
                                             const ndi_mc_grp_mbr_t& grp_mbr)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return false;
    }
    if (_repl_grp_list[grp_key]->ipmc_mbr_list.find(grp_mbr.rif_id) != _repl_grp_list[grp_key]->ipmc_mbr_list.end()) {
        NDI_MCAST_LOG_ERROR("IPMC group with RIF ID %ld alreay exists in replication group %ld", grp_mbr.rif_id, repl_grp_id);
        return false;
    }
    _repl_grp_list[grp_key]->ipmc_mbr_list.emplace(grp_mbr.rif_id, ndi_cache_grp_mbr_t{mbr_id, grp_mbr.rif_id, grp_mbr.port_list});
    return true;
}

bool repl_group_cache::update_group_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id, ndi_mc_grp_mbr_type_t mbr_type,
                                           ndi_rif_id_t rif_id, const ndi_sw_port_list_t& port_list)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return false;
    }
    auto repl_grp = _repl_grp_list[grp_key];
    ndi_cache_repl_grp_t::mbr_list_t* p_mbr_list;
    switch(mbr_type) {
    case NDI_RPF_GRP_MBR:
        p_mbr_list = &repl_grp->rpf_mbr_list;
        break;
    case NDI_IPMC_GRP_MBR:
        p_mbr_list = &repl_grp->ipmc_mbr_list;
        break;
    default:
        NDI_MCAST_LOG_ERROR("Invalid group member type");
        return false;
    }

    if (p_mbr_list->find(rif_id) == p_mbr_list->end()) {
        NDI_MCAST_LOG_ERROR("RIF ID %ld not found in group member list", rif_id);
        return false;
    }
    p_mbr_list->at(rif_id).update_port_list(port_list);

    return true;
}

bool repl_group_cache::del_group_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                                ndi_mc_grp_mbr_type_t mbr_type, ndi_rif_id_t rif_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return false;
    }
    auto repl_grp = _repl_grp_list[grp_key];
    ndi_cache_repl_grp_t::mbr_list_t* p_mbr_list;
    switch(mbr_type) {
    case NDI_RPF_GRP_MBR:
        p_mbr_list = &repl_grp->rpf_mbr_list;
        break;
    case NDI_IPMC_GRP_MBR:
        p_mbr_list = &repl_grp->ipmc_mbr_list;
        break;
    default:
        NDI_MCAST_LOG_ERROR("Invalid group member type");
        return false;
    }

    if (p_mbr_list->find(rif_id) == p_mbr_list->end()) {
        NDI_MCAST_LOG_ERROR("RIF ID %ld not found in group member list", rif_id);
        return false;
    }
    p_mbr_list->erase(rif_id);

    return true;
}

bool repl_group_cache::del_repl_group(npu_id_t npu_id, ndi_obj_id_t repl_grp_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return false;
    }
    _repl_grp_list.erase(grp_key);

    return true;
}

bool repl_group_cache::is_repl_group_used(npu_id_t npu_id, ndi_obj_id_t repl_grp_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return false;
    }

    auto repl_grp = _repl_grp_list[grp_key];
    return !(repl_grp->ipmc_ipv4_entry_list.empty() && repl_grp->ipmc_ipv6_entry_list.empty());
}

bool repl_group_cache::get_sub_group_id(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                                        ndi_obj_id_t& rpf_grp_id, ndi_obj_id_t& ipmc_grp_id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return false;
    }
    rpf_grp_id = _repl_grp_list.at(grp_key)->rpf_grp_id;
    ipmc_grp_id = _repl_grp_list.at(grp_key)->ipmc_grp_id;

    return true;
}

std::vector<ndi_obj_id_t> repl_group_cache::get_group_member_list(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                                                                  ndi_mc_grp_mbr_type_t mbr_type) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return std::vector<ndi_obj_id_t>{};
    }

    auto& repl_grp = _repl_grp_list.at(grp_key);
    std::vector<ndi_obj_id_t> mbr_id_list{};
    auto conv_func = [](const std::pair<ndi_rif_id_t, ndi_cache_grp_mbr_t>& mbr)->ndi_obj_id_t{
        return mbr.second.mbr_id;};
    switch(mbr_type) {
    case NDI_RPF_GRP_MBR:
        std::transform(repl_grp->rpf_mbr_list.begin(), repl_grp->rpf_mbr_list.end(),
                       std::back_inserter(mbr_id_list), conv_func);
        break;
    case NDI_IPMC_GRP_MBR:
        std::transform(repl_grp->ipmc_mbr_list.begin(), repl_grp->ipmc_mbr_list.end(),
                       std::back_inserter(mbr_id_list), conv_func);
        break;
    default:
        NDI_MCAST_LOG_ERROR("Invalid group member type");
        break;
    }

    return mbr_id_list;
}

bool repl_group_cache::get_group_rif_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                                            ndi_mc_grp_mbr_type_t mbr_type, ndi_rif_id_t rif_id,
                                            ndi_obj_id_t& mbr_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, repl_grp_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group of ID %ld not found in cache", repl_grp_id);
        return false;
    }
    auto repl_grp = _repl_grp_list[grp_key];
    ndi_cache_repl_grp_t::mbr_list_t* p_mbr_list;
    switch(mbr_type) {
    case NDI_RPF_GRP_MBR:
        p_mbr_list = &repl_grp->rpf_mbr_list;
        break;
    case NDI_IPMC_GRP_MBR:
        p_mbr_list = &repl_grp->ipmc_mbr_list;
        break;
    default:
        NDI_MCAST_LOG_ERROR("Invalid group member type");
        return false;
    }

    if (p_mbr_list->find(rif_id) == p_mbr_list->end()) {
        NDI_MCAST_LOG_ERROR("RIF ID %ld not found in group member list", rif_id);
        return false;
    }
    mbr_id = p_mbr_list->at(rif_id).mbr_id;

    return true;
}

bool repl_group_cache::add_ipmc_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto grp_key = std::make_pair(npu_id, ipmc_entry.repl_group_id);
    if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
        NDI_MCAST_LOG_ERROR("Replication group %ld in entry does not exist",
                            ipmc_entry.repl_group_id);
        return false;
    }
    auto& repl_grp = _repl_grp_list[grp_key];
    auto& grp_entry_list = repl_grp->get_cached_entry_list(ipmc_entry.dst_ip.af_index);
    if (!grp_entry_list.add_entry(npu_id, ipmc_entry)) {
        return false;
    }
    auto& entry_list = get_cached_entry_list(ipmc_entry.dst_ip.af_index);
    if (!entry_list.add_entry(npu_id, ipmc_entry, repl_grp)) {
        return false;
    }

    return true;
}

bool repl_group_cache::del_ipmc_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto& entry_list = get_cached_entry_list(ipmc_entry.dst_ip.af_index);
    try {
        auto& repl_grp = entry_list.get_repl_grp_obj(npu_id, ipmc_entry);
        auto& grp_entry_list = repl_grp->get_cached_entry_list(ipmc_entry.dst_ip.af_index);
        if (!grp_entry_list.delete_entry(npu_id, ipmc_entry)) {
            return false;
        }
    } catch (std::out_of_range& ex) {
        return false;
    }

    if (!entry_list.delete_entry(npu_id, ipmc_entry)) {
        return false;
    }

    return true;
}

bool repl_group_cache::update_ipmc_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry,
                                         ndi_ipmc_update_type_t upd_type)
{
    std::lock_guard<std::mutex> lock(_mutex);
    switch(upd_type) {
    case NAS_NDI_IPMC_UPD_REPL_GRP:
    {

        auto grp_key = std::make_pair(npu_id, ipmc_entry.repl_group_id);
        if (_repl_grp_list.find(grp_key) == _repl_grp_list.end()) {
            NDI_MCAST_LOG_ERROR("Group to be replaced not exists");
            return false;
        }
        auto new_grp = _repl_grp_list[grp_key];
        auto& entry_list = get_cached_entry_list(ipmc_entry.dst_ip.af_index);
        try {
            auto& orig_grp = entry_list.get_repl_grp_obj(npu_id, ipmc_entry);
            auto& grp_entry_list = orig_grp->get_cached_entry_list(ipmc_entry.dst_ip.af_index);
            if (!grp_entry_list.delete_entry(npu_id, ipmc_entry)) {
                return false;
            }
        } catch (std::out_of_range& ex) {
            return false;
        }
        auto& grp_entry_list = new_grp->get_cached_entry_list(ipmc_entry.dst_ip.af_index);
        if (!grp_entry_list.add_entry(npu_id, ipmc_entry)) {
            return false;
        }

        if (!entry_list.update_entry_repl_grp(npu_id, ipmc_entry, new_grp)) {
            return false;
        }
        break;
    }
    case NAS_NDI_IPMC_UPD_COPY_TO_CPU:
    {
        auto& entry_list = get_cached_entry_list(ipmc_entry.dst_ip.af_index);
        try {
            auto& repl_grp = entry_list.get_repl_grp_obj(npu_id, ipmc_entry);
            auto& grp_entry_list = repl_grp->get_cached_entry_list(ipmc_entry.dst_ip.af_index);
            if (!grp_entry_list.update_entry_copy_to_cpu(npu_id, ipmc_entry)) {
                return false;
            }
        } catch (std::out_of_range& ex) {
            return false;
        }

        if (!entry_list.update_entry_copy_to_cpu(npu_id, ipmc_entry)) {
            return false;
        }
        break;
    }
    default:
        return false;
    }

    return true;
}

bool repl_group_cache::get_ipmc_entry(npu_id_t npu_id, ndi_ipmc_entry_t& ipmc_entry)
{
    std::lock_guard<std::mutex> lock(_mutex);
    return get_cached_entry_list(ipmc_entry.dst_ip.af_index).get_entry(npu_id, ipmc_entry);
}

void repl_group_cache::dump_ipmc_group() const
{
    std::cout << "==========================" << std::endl;
    std::cout << "  Replication Groups" << std::endl;
    std::cout << "==========================" << std::endl;
    for (auto& group: _repl_grp_list) {
        std::cout << "NPU-" << group.first.first << " "
                  << "ID-" << std::hex << std::showbase
                  << group.first.second << " "
                  << std::dec << std::noshowbase
                  << std::string(*group.second) << std::endl;
    }
}

void repl_group_cache::dump_ipmc_entry(int af_index) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (af_index == 0 || af_index == AF_INET) {
        std::cout << "==========================" << std::endl;
        std::cout << "  IPv4 multicast entries" << std::endl;
        std::cout << "==========================" << std::endl;
        std::cout << std::string(_ipmc_ipv4_entry_list) << std::endl;
    }
    if (af_index == 0 || af_index == AF_INET6) {
        std::cout << "==========================" << std::endl;
        std::cout << "  IPv6 multicast entries" << std::endl;
        std::cout << "==========================" << std::endl;
        std::cout << std::string(_ipmc_ipv6_entry_list) << std::endl;
    }
}

static repl_group_cache& ipmc_cache = *new repl_group_cache{};

static inline sai_ipmc_repl_group_api_t *ndi_ipmc_repl_group_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return ndi_db_ptr->ndi_sai_api_tbl.n_sai_ipmc_repl_grp_api_tbl;
}

static inline sai_ipmc_group_api_t *ndi_ipmc_group_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return ndi_db_ptr->ndi_sai_api_tbl.n_sai_ipmc_grp_api_tbl;
}

static inline sai_rpf_group_api_t *ndi_rpf_group_api_get(nas_ndi_db_t *ndi_db_ptr)
{
    return ndi_db_ptr->ndi_sai_api_tbl.n_sai_rpf_grp_api_tbl;
}

static t_std_error ndi_ipmc_ndi2sai_repl_owner_type(ndi_repl_grp_owner_type_t ndi_owner_type,
                                                    sai_attribute_t* sai_attr_p)
{
    static const auto ndi_to_sai_repl_owner =
        new std::unordered_map<ndi_repl_grp_owner_type_t, sai_ipmc_repl_group_owner_t, std::hash<int>> {
        {REPL_GROUP_OWNER_L2MC, SAI_IPMC_REPLICATION_GROUP_OWNER_L2MC},
        {REPL_GROUP_OWNER_IPMC, SAI_IPMC_REPLICATION_GROUP_OWNER_IPMC},
        {REPL_GROUP_OWNER_VXLAN, SAI_IPMC_REPLICATION_GROUP_OWNER_VXLAN},
        {REPL_GROUP_OWNER_NONE, SAI_IPMC_REPLICATION_GROUP_OWNER_NONE}
    };
    auto itor = ndi_to_sai_repl_owner->find(ndi_owner_type);
    if (itor == ndi_to_sai_repl_owner->end()) {
        NDI_MCAST_LOG_ERROR("Invalid replication group owner type: %d", static_cast<int>(ndi_owner_type));
        return STD_ERR(MCAST, PARAM, 0);
    }
    sai_attr_p->id = SAI_IPMC_REPL_GROUP_ATTR_REPL_OWNER;
    sai_attr_p->value.s32 = static_cast<uint32_t>(itor->second);

    return STD_ERR_OK;
}

static t_std_error ndi_create_ipmc_group(npu_id_t npu_id, ndi_obj_id_t repl_group_id,
                                         ndi_obj_id_t *ipmc_group_id_p)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_IPMC_GROUP_EXTENSION_ATTR_IPMC_REPL_GROUP_ID;
    sai_attr.value.oid = static_cast<sai_object_id_t>(repl_group_id);
    sai_object_id_t sai_grp_id;
    sai_status_t sai_ret = ndi_ipmc_group_api_get(ndi_db_ptr)->create_ipmc_group(&sai_grp_id,
                                    ndi_switch_id_get(), 1, &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to call SAI API to create IPMC group, ret=%d", sai_ret);
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    *ipmc_group_id_p = static_cast<ndi_obj_id_t>(sai_grp_id);

    return STD_ERR_OK;
}

static t_std_error ndi_delete_ipmc_group(npu_id_t npu_id, ndi_obj_id_t ipmc_group_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_status_t sai_ret = ndi_ipmc_group_api_get(ndi_db_ptr)->remove_ipmc_group(
                                    static_cast<sai_object_id_t>(ipmc_group_id));
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to call SAI API to delete IPMC group, ret=%d", sai_ret);
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

static t_std_error ndi_create_rpf_group(npu_id_t npu_id, ndi_obj_id_t repl_group_id,
                                        ndi_obj_id_t *rpf_group_id_p)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_RPF_GROUP_EXTENSION_ATTR_IPMC_REPL_GROUP_ID;
    sai_attr.value.oid = static_cast<sai_object_id_t>(repl_group_id);
    sai_object_id_t sai_grp_id;
    sai_status_t sai_ret = ndi_rpf_group_api_get(ndi_db_ptr)->create_rpf_group(&sai_grp_id,
                                    ndi_switch_id_get(), 1, &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to call SAI API to create RPF group, ret=%d", sai_ret);
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    *rpf_group_id_p = static_cast<ndi_obj_id_t>(sai_grp_id);

    return STD_ERR_OK;
}

static t_std_error ndi_delete_rpf_group(npu_id_t npu_id, ndi_obj_id_t rpf_group_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_status_t sai_ret = ndi_rpf_group_api_get(ndi_db_ptr)->remove_rpf_group(
                                    static_cast<sai_object_id_t>(rpf_group_id));
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to call SAI API to delete RPF group, ret=%d", sai_ret);
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

static std::vector<sai_object_id_t> get_member_bridge_port(const ndi_sw_port_list_t& sw_port_list)
{
    std::vector<sai_object_id_t> sai_port_list{};
    for (size_t idx = 0; idx < sw_port_list.port_count; idx ++) {
        sai_object_id_t sai_port_id = 0;
        switch(sw_port_list.list[idx].port_type) {
        case NDI_SW_PORT_NPU_PORT:
            if (ndi_sai_port_id_get(sw_port_list.list[idx].u.npu_port.npu_id,
                                    sw_port_list.list[idx].u.npu_port.npu_port,
                                    &sai_port_id) != STD_ERR_OK) {
                continue;
            }
            break;
        case NDI_SW_PORT_LAG:
            sai_port_id = static_cast<sai_object_id_t>(sw_port_list.list[idx].u.lag);
            break;
        default:
            continue;
        }
        sai_object_id_t bridge_port;
        if (!ndi_get_1q_bridge_port(&bridge_port, sai_port_id)) {
            NDI_MCAST_LOG_ERROR("Could not find bridge port ID for SAI port 0x%" PRIx64,
                                 sai_port_id);
            continue;
        }
        sai_port_list.push_back(bridge_port);
    }
    return sai_port_list;
}

static t_std_error ndi_add_group_member(npu_id_t npu_id, ndi_obj_id_t group_id, ndi_rif_id_t rif_id,
                                        ndi_sw_port_list_t *port_list, ndi_mc_grp_mbr_type_t mbr_type,
                                        ndi_obj_id_t *group_mbr_id_p)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    std::function<sai_status_t(sai_object_id_t*, sai_object_id_t, uint32_t, const sai_attribute_t*)> mbr_func;
    sai_attr_id_t grp_attr_id, rif_attr_id, portlist_attr_id;
    switch(mbr_type) {
    case NDI_IPMC_GRP_MBR:
        mbr_func = ndi_ipmc_group_api_get(ndi_db_ptr)->create_ipmc_group_member;
        grp_attr_id = SAI_IPMC_GROUP_MEMBER_ATTR_IPMC_GROUP_ID;
        rif_attr_id = SAI_IPMC_GROUP_MEMBER_ATTR_IPMC_OUTPUT_ID;
        portlist_attr_id = SAI_IPMC_GROUP_MEMBER_EXTENSION_ATTR_IPMC_PORT_LIST;
        break;
    case NDI_RPF_GRP_MBR:
        mbr_func = ndi_rpf_group_api_get(ndi_db_ptr)->create_rpf_group_member;
        grp_attr_id = SAI_RPF_GROUP_MEMBER_ATTR_RPF_GROUP_ID;
        rif_attr_id = SAI_RPF_GROUP_MEMBER_ATTR_RPF_INTERFACE_ID;
        portlist_attr_id = SAI_RPF_GROUP_MEMBER_EXTENSION_ATTR_IPMC_PORT_LIST;
        break;
    default:
        NDI_MCAST_LOG_ERROR("Unknown group member type given");
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_attribute_t sai_attr[3];
    uint32_t attr_count = 2;

    sai_attr[0].id = grp_attr_id;
    sai_attr[0].value.oid = static_cast<sai_object_id_t>(group_id);
    sai_attr[1].id = rif_attr_id;
    sai_attr[1].value.oid = static_cast<sai_object_id_t>(rif_id);
    /* Add bridge port list attr and value only if count is more than 0 */
    auto bridge_port_list = get_member_bridge_port(*port_list);
    if (port_list->port_count > 0) {
        sai_attr[2].id = portlist_attr_id;
        sai_attr[2].value.objlist.count = bridge_port_list.size();
        sai_attr[2].value.objlist.list = bridge_port_list.data();
        attr_count += 1;
    }
    sai_object_id_t sai_mbr_id;
    auto sai_ret = mbr_func(&sai_mbr_id, ndi_switch_id_get(), attr_count, sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to call SAI API to add group member, ret=%d", sai_ret);
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    *group_mbr_id_p = static_cast<ndi_obj_id_t>(sai_mbr_id);
    return STD_ERR_OK;
}

static t_std_error ndi_delete_group_member(npu_id_t npu_id, ndi_mc_grp_mbr_type_t mbr_type,
                                           ndi_obj_id_t group_mbr_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    std::function<sai_status_t(sai_object_id_t)> mbr_func;
    switch(mbr_type) {
    case NDI_IPMC_GRP_MBR:
        mbr_func = ndi_ipmc_group_api_get(ndi_db_ptr)->remove_ipmc_group_member;
        break;
    case NDI_RPF_GRP_MBR:
        mbr_func = ndi_rpf_group_api_get(ndi_db_ptr)->remove_rpf_group_member;
        break;
    default:
        return STD_ERR(MCAST, PARAM, 0);
    }

    auto sai_ret = mbr_func(static_cast<sai_object_id_t>(group_mbr_id));
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to call SAI API to delete group member, ret=%d", sai_ret);

        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

static t_std_error ndi_update_group_member(npu_id_t npu_id, ndi_mc_grp_mbr_type_t mbr_type,
                                           ndi_obj_id_t group_mbr_id,
                                           ndi_rif_id_t rif_id,
                                           ndi_sw_port_list_t *port_list)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    std::function<sai_status_t(sai_object_id_t, const sai_attribute_t*)> mbr_func;
    sai_attr_id_t portlist_attr_id;
    switch(mbr_type) {
    case NDI_IPMC_GRP_MBR:
        mbr_func = ndi_ipmc_group_api_get(ndi_db_ptr)->set_ipmc_group_member_attribute;
        portlist_attr_id = SAI_IPMC_GROUP_MEMBER_EXTENSION_ATTR_IPMC_PORT_LIST;
        break;
    case NDI_RPF_GRP_MBR:
        mbr_func = ndi_rpf_group_api_get(ndi_db_ptr)->set_rpf_group_member_attribute;
        portlist_attr_id = SAI_RPF_GROUP_MEMBER_EXTENSION_ATTR_IPMC_PORT_LIST;
        break;
    default:
        return STD_ERR(MCAST, PARAM, 0);
    }

    auto bridge_port_list = get_member_bridge_port(*port_list);
    sai_attribute_t sai_attr;
    sai_attr.id = portlist_attr_id;
    sai_attr.value.objlist.count = bridge_port_list.size();
    sai_attr.value.objlist.list = bridge_port_list.data();

    auto sai_ret = mbr_func(static_cast<sai_object_id_t>(group_mbr_id), &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to call SAI API to set group member attribute, ret=%d", sai_ret);
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}

t_std_error ndi_ipmc_get_repl_subgrp_id(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                                        ndi_obj_id_t& rpf_grp_id, ndi_obj_id_t& ipmc_grp_id)
{
    if (!ipmc_cache.get_sub_group_id(npu_id, repl_grp_id, rpf_grp_id, ipmc_grp_id)) {
        NDI_MCAST_LOG_ERROR("Failed to IPMC and RPF group ID from replication group ID %ld",
                            repl_grp_id);
        return STD_ERR(MCAST, FAIL, 0);
    }
    return STD_ERR_OK;
}

static std::string dump_grp_mbr_info(const ndi_mc_grp_mbr_t& grp_mbr)
{
    std::ostringstream ss;
    ss << "RIF-" << grp_mbr.rif_id << " ";
    ss << "PORT-";
    for (size_t idx = 0; idx < grp_mbr.port_list.port_count; idx ++) {
        switch(grp_mbr.port_list.list[idx].port_type) {
            case NDI_SW_PORT_NPU_PORT:
                ss << "P(" << grp_mbr.port_list.list[idx].u.npu_port.npu_id << "," <<
                              grp_mbr.port_list.list[idx].u.npu_port.npu_port << "),";
                break;
            case NDI_SW_PORT_LAG:
                ss << "L(" << grp_mbr.port_list.list[idx].u.lag << "),";
                break;
            default:
                continue;
        }
    }
    return ss.str();
}

t_std_error ndi_create_repl_group(npu_id_t npu_id, ndi_repl_grp_owner_type_t owner,
                                  ndi_mc_grp_mbr_t *rpf_grp_mbr,
                                  size_t ipmc_grp_mbr_cnt, ndi_mc_grp_mbr_t *ipmc_grp_mbr,
                                  ndi_obj_id_t *repl_group_id_p)
{
    NDI_MCAST_LOG_INFO("Create replication group");
    NDI_MCAST_LOG_INFO("RPF group:");
    NDI_MCAST_LOG_INFO("  %s", dump_grp_mbr_info(*rpf_grp_mbr).c_str());
    NDI_MCAST_LOG_INFO("IPMC group:");
    for (size_t idx = 0; idx < ipmc_grp_mbr_cnt; idx ++) {
        NDI_MCAST_LOG_INFO("  %s", dump_grp_mbr_info(ipmc_grp_mbr[idx]).c_str());
    }

    if (repl_group_id_p == nullptr) {
        NDI_MCAST_LOG_ERROR("NULL pointer is not accepted for returning replication group ID");
        return STD_ERR(MCAST, PARAM, 0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_attribute_t sai_attr;
    if (ndi_ipmc_ndi2sai_repl_owner_type(owner, &sai_attr) != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Failed to convert NDI replication owner type to SAI attribute");
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_object_id_t sai_grp_id;
    sai_status_t sai_ret = ndi_ipmc_repl_group_api_get(ndi_db_ptr)->create_ipmc_repl_group(&sai_grp_id,
                                    ndi_switch_id_get(), 1, &sai_attr);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to create replication group");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    *repl_group_id_p = static_cast<ndi_obj_id_t>(sai_grp_id);
    NDI_MCAST_LOG_INFO("Replication group created, ID=0x%" PRIx64, *repl_group_id_p);

    ndi_obj_id_t rpf_group_id, ipmc_group_id;
    ipmc_group_id = 0;
    if (ipmc_grp_mbr_cnt > 0) {
       if (ndi_create_ipmc_group(npu_id, *repl_group_id_p, &ipmc_group_id) != STD_ERR_OK) {
           NDI_MCAST_LOG_ERROR("Failed to create IPMC group");
           return STD_ERR(MCAST, FAIL, 0);
       }
    }
    NDI_MCAST_LOG_INFO("IPMC group created, ID=0x%" PRIx64, ipmc_group_id);

    if (ndi_create_rpf_group(npu_id, *repl_group_id_p, &rpf_group_id) != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Failed to create RPF group");
        return STD_ERR(MCAST, FAIL, 0);
    }
    NDI_MCAST_LOG_INFO("RPF group created, ID=0x%" PRIx64, rpf_group_id);

    if (!ipmc_cache.add_repl_group(npu_id, *repl_group_id_p, rpf_group_id, ipmc_group_id)) {
        NDI_MCAST_LOG_ERROR("Failed to add replication group to cache, rollback group create");
        ndi_delete_ipmc_group(npu_id, ipmc_group_id);
        ndi_delete_rpf_group(npu_id, rpf_group_id);
        ndi_ipmc_repl_group_api_get(ndi_db_ptr)->remove_ipmc_repl_group(
                                        static_cast<sai_object_id_t>(*repl_group_id_p));
        return STD_ERR(MCAST, FAIL, 0);
    }

    ndi_obj_id_t rpf_mbr_id;
    if (ndi_add_group_member(npu_id, rpf_group_id, rpf_grp_mbr->rif_id, &rpf_grp_mbr->port_list,
                              NDI_RPF_GRP_MBR, &rpf_mbr_id) != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Failed to add RPF group member");
        return STD_ERR(MCAST, FAIL, 0);
    }
    NDI_MCAST_LOG_INFO("RPF group member added, ID=0x%" PRIx64, rpf_mbr_id);

    if (!ipmc_cache.add_rpf_group_member(npu_id, *repl_group_id_p, rpf_mbr_id, *rpf_grp_mbr)) {
        NDI_MCAST_LOG_ERROR("Failed to add RPF group member to cache, rollback group adding");
        ndi_delete_group_member(npu_id, NDI_RPF_GRP_MBR, rpf_mbr_id);
        return STD_ERR(MCAST, FAIL, 0);
    }

    for (size_t idx = 0; idx < ipmc_grp_mbr_cnt; idx ++) {
        ndi_obj_id_t ipmc_mbr_id;
        if (ndi_add_group_member(npu_id, ipmc_group_id, ipmc_grp_mbr[idx].rif_id, &ipmc_grp_mbr[idx].port_list,
                                 NDI_IPMC_GRP_MBR, &ipmc_mbr_id) != STD_ERR_OK) {
            NDI_MCAST_LOG_ERROR("Failed to add IPMC group member");
            continue;
        }
        NDI_MCAST_LOG_INFO("IPMC group member added, ID=0x%" PRIx64, ipmc_mbr_id);

        if (!ipmc_cache.add_ipmc_group_member(npu_id, *repl_group_id_p, ipmc_mbr_id, ipmc_grp_mbr[idx])) {
            NDI_MCAST_LOG_ERROR("Failed to add IPMC group member to cache, rollback group adding");
            ndi_delete_group_member(npu_id, NDI_IPMC_GRP_MBR, ipmc_mbr_id);
            continue;
        }
    }

    return STD_ERR_OK;
}

t_std_error ndi_delete_repl_group(npu_id_t npu_id, ndi_obj_id_t repl_group_id)
{
    ndi_obj_id_t rpf_grp_id, ipmc_grp_id;

    NDI_MCAST_LOG_INFO("Delete replication group");
    NDI_MCAST_LOG_INFO("Group ID: 0x%" PRIx64, repl_group_id);

    if (ipmc_cache.is_repl_group_used(npu_id, repl_group_id)) {
        NDI_MCAST_LOG_ERROR("Replication group is referenced by IPMC entry, delete not allowed");
        return STD_ERR(MCAST, PARAM, 0);
    }

    if (!ipmc_cache.get_sub_group_id(npu_id, repl_group_id, rpf_grp_id, ipmc_grp_id)) {
        NDI_MCAST_LOG_ERROR("Failed to get RPF and IPCM group ID from repl group, ID=0x%" PRIx64,
                            repl_group_id);
        return STD_ERR(MCAST, PARAM, 0);
    }

    auto rpf_member = ipmc_cache.get_group_member_list(npu_id, repl_group_id, NDI_RPF_GRP_MBR);
    if (rpf_member.size() != 1) {
        NDI_MCAST_LOG_ERROR("RPF group doesn't just contain one member");
        return STD_ERR(MCAST, PARAM, 0);
    }
    for (auto mbr_id: rpf_member) {
        if (ndi_delete_group_member(npu_id, NDI_RPF_GRP_MBR, mbr_id) != STD_ERR_OK) {
            NDI_MCAST_LOG_ERROR("Failed to delete RPF group member, ID=0x%" PRIx64, mbr_id);
            return STD_ERR(MCAST, FAIL, 0);
        }
    }
    if (ndi_delete_rpf_group(npu_id, rpf_grp_id) != STD_ERR_OK) {
        NDI_MCAST_LOG_ERROR("Failed to delete RPF group, ID=0x%" PRIx64, rpf_grp_id);
        return STD_ERR(MCAST, FAIL, 0);
    }

    auto ipmc_member = ipmc_cache.get_group_member_list(npu_id, repl_group_id, NDI_IPMC_GRP_MBR);
    for (auto mbr_id: ipmc_member) {
        if (ndi_delete_group_member(npu_id, NDI_IPMC_GRP_MBR, mbr_id) != STD_ERR_OK) {
            NDI_MCAST_LOG_ERROR("Failed to delete IPMC group member, ID=0x%" PRIx64, mbr_id);
            return STD_ERR(MCAST, FAIL, 0);
        }
    }
    if (ipmc_member.size() > 0) {
       if (ndi_delete_ipmc_group(npu_id, ipmc_grp_id) != STD_ERR_OK) {
           NDI_MCAST_LOG_ERROR("Failed to delete IPMC group, ID=0x%" PRIx64, ipmc_grp_id);
           return STD_ERR(MCAST, FAIL, 0);
       }
    }
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == nullptr) {
        return STD_ERR(MCAST, PARAM, 0);
    }

    sai_status_t sai_ret = ndi_ipmc_repl_group_api_get(ndi_db_ptr)->remove_ipmc_repl_group(
                                    static_cast<sai_object_id_t>(repl_group_id));
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_MCAST_LOG_ERROR("Failed to delete replication group");
        return STD_ERR(MCAST, FAIL, sai_ret);
    }

    if (!ipmc_cache.del_repl_group(npu_id, repl_group_id)) {
        NDI_MCAST_LOG_ERROR("Failed to delete replication group from cache");
        return STD_ERR(MCAST, FAIL, 0);
    }

    return STD_ERR_OK;
}

t_std_error ndi_update_repl_group(npu_id_t npu_id, ndi_obj_id_t repl_group_id,
                                  ndi_mc_grp_op_t op, ndi_mc_grp_mbr_type_t mbr_type,
                                  ndi_mc_grp_mbr_t *grp_mbr)
{
    ndi_obj_id_t mbr_id;
    bool mbr_exist = ipmc_cache.get_group_rif_member(npu_id, repl_group_id, mbr_type,
                                                     grp_mbr->rif_id, mbr_id);

    NDI_MCAST_LOG_INFO("Update replication group");
    NDI_MCAST_LOG_INFO("Operation: %s", op == ADD_GRP_MBR ? "Add" :
                                        (op == DEL_GRP_MBR ? "Delete" : "Update"));
    NDI_MCAST_LOG_INFO("Member Type: %s", mbr_type == NDI_RPF_GRP_MBR ? "RPF" : "IPMC");
    NDI_MCAST_LOG_INFO("  %s", dump_grp_mbr_info(*grp_mbr).c_str());

    switch(op) {
    case ADD_GRP_MBR:
        if (mbr_exist) {
            NDI_MCAST_LOG_ERROR("Group member with RIF ID %ld exists for adding operation",
                                grp_mbr->rif_id);
            return STD_ERR(MCAST, FAIL, 0);
        }
        ndi_obj_id_t rpf_grp_id, ipmc_grp_id;
        ipmc_cache.get_sub_group_id(npu_id, repl_group_id, rpf_grp_id, ipmc_grp_id);
        switch(mbr_type) {
        case NDI_RPF_GRP_MBR:
            if (ndi_add_group_member(npu_id, rpf_grp_id, grp_mbr->rif_id, &grp_mbr->port_list,
                                     mbr_type, &mbr_id) != STD_ERR_OK) {
                NDI_MCAST_LOG_ERROR("Failed to add RPF group member");
                return STD_ERR(MCAST, FAIL, 0);
            }
            if (!ipmc_cache.add_rpf_group_member(npu_id, repl_group_id, mbr_id, *grp_mbr)) {
                NDI_MCAST_LOG_ERROR("Failed to add RPF group member to cache");
                return STD_ERR(MCAST, FAIL, 0);
            }
            break;
        case NDI_IPMC_GRP_MBR:
            if (ndi_add_group_member(npu_id, ipmc_grp_id, grp_mbr->rif_id, &grp_mbr->port_list,
                                     mbr_type, &mbr_id) != STD_ERR_OK) {
                NDI_MCAST_LOG_ERROR("Failed to add IPMC group member");
                return STD_ERR(MCAST, FAIL, 0);
            }
            if (!ipmc_cache.add_ipmc_group_member(npu_id, repl_group_id, mbr_id, *grp_mbr)) {
                NDI_MCAST_LOG_ERROR("Failed to add IPMC group member to cache");
                return STD_ERR(MCAST, FAIL, 0);
            }
            break;
        default:
            NDI_MCAST_LOG_ERROR("Invalid member type");
            return STD_ERR(MCAST, FAIL, 0);
        }
        break;
    case DEL_GRP_MBR:
        if (!mbr_exist) {
            return STD_ERR(MCAST, FAIL, 0);
        }
        if (ndi_delete_group_member(npu_id, mbr_type, mbr_id) != STD_ERR_OK) {
            NDI_MCAST_LOG_ERROR("Failed to delete %s group member",
                                 mbr_type == NDI_RPF_GRP_MBR ? "RPF" : "IPMC");
            return STD_ERR(MCAST, FAIL, 0);
        }
        if (!ipmc_cache.del_group_member(npu_id, repl_group_id, mbr_type, grp_mbr->rif_id)) {
            NDI_MCAST_LOG_ERROR("Failed to delete %s group member from cache",
                                mbr_type == NDI_RPF_GRP_MBR ? "RPF" : "IPMC");
            return STD_ERR(MCAST, FAIL, 0);
        }
        break;
    case UPDATE_GRP_MBR:
        if (!mbr_exist) {
            return STD_ERR(MCAST, FAIL, 0);
        }
        if (ndi_update_group_member(npu_id, mbr_type, mbr_id, grp_mbr->rif_id,
                                    &grp_mbr->port_list) != STD_ERR_OK) {
            NDI_MCAST_LOG_ERROR("Failed to update %s group member",
                                 mbr_type == NDI_RPF_GRP_MBR ? "RPF" : "IPMC");
            return STD_ERR(MCAST, FAIL, 0);
        }
        if (!ipmc_cache.update_group_member(npu_id, repl_group_id, mbr_type, grp_mbr->rif_id,
                                            grp_mbr->port_list)) {
            NDI_MCAST_LOG_ERROR("Failed to update %s group member to cache",
                                 mbr_type == NDI_RPF_GRP_MBR ? "RPF" : "IPMC");
            return STD_ERR(MCAST, FAIL, 0);
        }
        break;
    default:
        NDI_MCAST_LOG_ERROR("Unknown operation type");
        return STD_ERR(MCAST, PARAM, 0);
    }

    return STD_ERR_OK;
}

t_std_error ndi_ipmc_cache_add_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry)
{
    if (!ipmc_cache.add_ipmc_entry(npu_id, ipmc_entry)) {
        NDI_MCAST_LOG_ERROR("Failed to add IPMC entry to cache");
        return STD_ERR(MCAST, FAIL, 0);
    }

    return STD_ERR_OK;
}

t_std_error ndi_ipmc_cache_del_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry)
{
    if (!ipmc_cache.del_ipmc_entry(npu_id, ipmc_entry)) {
        NDI_MCAST_LOG_ERROR("Failed to delete IPMC entry from cache");
        return STD_ERR(MCAST, FAIL, 0);
    }

    return STD_ERR_OK;
}

t_std_error ndi_ipmc_cache_update_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry,
                                        ndi_ipmc_update_type_t upd_type)
{
    if (!ipmc_cache.update_ipmc_entry(npu_id, ipmc_entry, upd_type)) {
        NDI_MCAST_LOG_ERROR("Failed to update IPMC entry in cache");
        return STD_ERR(MCAST, FAIL, 0);
    }

    return STD_ERR_OK;
}

t_std_error ndi_ipmc_cache_get_entry(npu_id_t npu_id, ndi_ipmc_entry_t& ipmc_entry)
{
    if (!ipmc_cache.get_ipmc_entry(npu_id, ipmc_entry)) {
        NDI_MCAST_LOG_ERROR("Failed to get IPMC entry from cache");
        return STD_ERR(MCAST, FAIL, 0);
    }

    return STD_ERR_OK;
}

const ndi_cache_repl_grp_t& ndi_ipmc_cache_get_repl_grp(npu_id_t npu_id, ndi_obj_id_t repl_grp_id)
{
    return ipmc_cache.get_repl_group(npu_id, repl_grp_id);
}

void ndi_ipmc_cache_dump_group()
{
    ipmc_cache.dump_ipmc_group();
}

void ndi_ipmc_cache_dump_entry(int af_index)
{
    ipmc_cache.dump_ipmc_entry(af_index);
}
