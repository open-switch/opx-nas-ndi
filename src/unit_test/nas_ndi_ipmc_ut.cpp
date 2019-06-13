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
 * filename: nas_ndi_ipmc_ut.cpp
 */

#include "std_ip_utils.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_ipmc.h"
#include "nas_ndi_ipmc_utl.h"

#include <gtest/gtest.h>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unordered_map>
#include <tuple>

enum obj_type_t {
    REPL_GRP,
    RPF_GRP,
    IPMC_GRP,
    RPF_GRP_MBR,
    IPMC_GRP_MBR,
    IPMC_ENTRY
};

static std::string get_obj_type_name(obj_type_t obj_type, int* id_base = nullptr)
{
    std::unordered_map<obj_type_t, std::pair<std::string, int>> type_map {
        {REPL_GRP, {"Replication Group", 1000}},
        {RPF_GRP, {"RPF Group", 2000}},
        {IPMC_GRP, {"IPMC Group", 3000}},
        {RPF_GRP_MBR, {"RPF Group Member", 4000}},
        {IPMC_GRP_MBR, {"IPMC Group Member", 5000}}
    };
    if (type_map.find(obj_type) == type_map.end()) {
        return "";
    }

    if (id_base != nullptr) {
        *id_base = type_map[obj_type].second;
    }
    return type_map[obj_type].first;
}

template<obj_type_t OBJ>
sai_status_t create_ipmc_object(sai_object_id_t *obj_id, sai_object_id_t switch_id,
                                uint32_t attr_count, const sai_attribute_t *attr_list)
{
    int id_base;
    static int id_offset = 0;
    std::cout << "IPMC_UT: Create " << get_obj_type_name(OBJ, &id_base);
    std::cout << std::hex << std::showbase;
    std::cout << " ATTR_ID=";
    for (uint32_t idx = 0; idx < attr_count; idx ++) {
        std::cout << attr_list[idx].id << ",";
    }
    *obj_id = id_base + id_offset;
    std::cout << " ID=" << *obj_id << std::endl;
    id_offset ++;
    std::cout << std::dec << std::noshowbase;
    return SAI_STATUS_SUCCESS;
}

template<obj_type_t OBJ>
sai_status_t remove_ipmc_object(sai_object_id_t obj_id)
{
    std::cout << "IPMC_UT: Remove " << get_obj_type_name(OBJ) << " ID=" <<  obj_id << std::endl;
    return SAI_STATUS_SUCCESS;
}

template<obj_type_t OBJ>
sai_status_t set_ipmc_object_attr(sai_object_id_t obj_id, const sai_attribute_t *attr)
{
    std::cout << std::hex << std::showbase;
    std::cout << "IPMC_UT: Set " << get_obj_type_name(OBJ) << " attribute ID=" <<  obj_id
              << " ATTR_ID=" << attr->id << std::endl;
    std::cout << std::dec << std::noshowbase;
    return SAI_STATUS_SUCCESS;
}

template<obj_type_t OBJ>
sai_status_t get_ipmc_object_attr(sai_object_id_t obj_id, uint32_t attr_count,
                                         sai_attribute_t *attr_list)
{
    std::cout << std::hex << std::showbase;
    std::cout << "IPMC_UT: Get " << get_obj_type_name(OBJ) << " attribute ID=" <<  obj_id << " "
              << "ATTR_ID=";
    for (uint32_t idx = 0; idx < attr_count; idx ++) {
        std::cout << attr_list[idx].id << ",";
    }
    std::cout << std::endl;
    std::cout << std::dec << std::noshowbase;
    return SAI_STATUS_SUCCESS;
}

sai_ipmc_repl_group_api_t repl_group_api {
        create_ipmc_object<REPL_GRP>,
        remove_ipmc_object<REPL_GRP>,
        set_ipmc_object_attr<REPL_GRP>,
        get_ipmc_object_attr<REPL_GRP>
};

sai_ipmc_group_api_t ipmc_group_api {
        create_ipmc_object<IPMC_GRP>,
        remove_ipmc_object<IPMC_GRP>,
        set_ipmc_object_attr<IPMC_GRP>,
        get_ipmc_object_attr<IPMC_GRP>,
        create_ipmc_object<IPMC_GRP_MBR>,
        remove_ipmc_object<IPMC_GRP_MBR>,
        set_ipmc_object_attr<IPMC_GRP_MBR>,
        get_ipmc_object_attr<IPMC_GRP_MBR>
};

sai_rpf_group_api_t rpf_group_api {
        create_ipmc_object<RPF_GRP>,
        remove_ipmc_object<RPF_GRP>,
        set_ipmc_object_attr<RPF_GRP>,
        get_ipmc_object_attr<RPF_GRP>,
        create_ipmc_object<RPF_GRP_MBR>,
        remove_ipmc_object<RPF_GRP_MBR>,
        set_ipmc_object_attr<RPF_GRP_MBR>,
        get_ipmc_object_attr<RPF_GRP_MBR>
};

static void convert_sai_to_std_ip_addr (const sai_ip_address_t *p_ip_addr,
                                        hal_ip_addr_t *p_std_ip_addr)
{
    if (p_ip_addr->addr_family == SAI_IP_ADDR_FAMILY_IPV4) {

        p_std_ip_addr->af_index = AF_INET;
        memcpy ((uint8_t *) &p_std_ip_addr->u.v4_addr,
                (uint8_t *) &p_ip_addr->addr.ip4,
                sizeof (p_std_ip_addr->u.v4_addr));
    } else {

        p_std_ip_addr->af_index = AF_INET6;
        memcpy ((uint8_t *) &p_std_ip_addr->u.v6_addr,
                (uint8_t *) &p_ip_addr->addr.ip6,
                sizeof (p_std_ip_addr->u.v6_addr));
    }
}

std::ostream& operator<<(std::ostream& os, const hal_ip_addr_t& ip_addr)
{
    static char ip_buf[HAL_INET6_TEXT_LEN + 1];
    const char *ip_str = std_ip_to_string(&ip_addr, ip_buf, sizeof(ip_buf));
    if (ip_str != nullptr) {
        os << ip_str;
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const ndi_ipmc_entry_t& entry)
{
    os << "[VRF ";
    os << std::hex << std::showbase;
    os << entry.vrf_id;
    os << std::dec << std::noshowbase;
    os << " SRC ";
    if (entry.type == NAS_NDI_IPMC_ENTRY_TYPE_XG) {
        os << "*";
    } else {
        os << entry.src_ip;
    }
    os << " DST " << entry.dst_ip;
    os << " GRP " << entry.repl_group_id;
    os << " TO_CPU " << entry.copy_to_cpu << "]";
    return os;
}

std::ostream& operator<<(std::ostream& os, const sai_ip_address_t& ip_addr)
{
    hal_ip_addr_t std_ip_addr;
    convert_sai_to_std_ip_addr(&ip_addr, &std_ip_addr);
    os << std_ip_addr;
    return os;
}

std::ostream& operator<<(std::ostream& os, const sai_ipmc_entry_t& entry)
{
    os << "[VRF ";
    os << std::hex << std::showbase;
    os << entry.vr_id;
    os << std::dec << std::noshowbase;
    os << " SRC ";
    if (entry.type == SAI_IPMC_ENTRY_TYPE_XG) {
        os << "*";
    } else {
        os << entry.source;
    }
    os << " DST " << entry.destination << "]";
    return os;
}

static sai_status_t create_ipmc_entry(const sai_ipmc_entry_t *entry, uint32_t attr_count,
                                      const sai_attribute_t *attr)
{
    std::cout << "IPMC_UT: Create IPMC Entry " << *entry << std::endl;
    std::cout << std::hex << std::showbase;
    std::cout << " ATTR_ID=";
    for (uint32_t idx = 0; idx < attr_count; idx ++) {
        std::cout << attr[idx].id << ",";
    }
    std::cout << std::endl;
    std::cout << std::dec << std::noshowbase;
    return SAI_STATUS_SUCCESS;
}

static sai_status_t remove_ipmc_entry(const sai_ipmc_entry_t *entry)
{
    std::cout << "IPMC_UT: Remove IPMC Entry " << *entry << std::endl;
    return SAI_STATUS_SUCCESS;
}

static sai_status_t set_ipmc_entry_attr(const sai_ipmc_entry_t *entry, const sai_attribute_t *attr)
{
    std::cout << "IPMC_UT: Set IPMC Entry " << *entry << std::endl;
    std::cout << std::hex << std::showbase;
    std::cout << " ATTR_ID=" << attr->id << std::endl;
    std::cout << std::dec << std::noshowbase;
    return SAI_STATUS_SUCCESS;
}

static sai_status_t get_ipmc_entry_attr(const sai_ipmc_entry_t *entry, uint32_t attr_count,
                                        sai_attribute_t *attr_list)
{
    std::cout << "IPMC_UT: Get IPMC Entry " << *entry << std::endl;
    std::cout << std::hex << std::showbase;
    std::cout << "ATTR_ID=";
    for (uint32_t idx = 0; idx < attr_count; idx ++) {
        std::cout << attr_list[idx].id << ",";
    }
    std::cout << std::endl;
    std::cout << std::dec << std::noshowbase;
    return SAI_STATUS_SUCCESS;
}

sai_ipmc_api_t ipmc_entry_api {
        create_ipmc_entry,
        remove_ipmc_entry,
        set_ipmc_entry_attr,
        get_ipmc_entry_attr
};

static t_std_error nas_ndi_sai_api_table_init(ndi_sai_api_tbl_t *n_sai_api_tbl)
{
    n_sai_api_tbl->n_sai_ipmc_repl_grp_api_tbl = &repl_group_api;
    n_sai_api_tbl->n_sai_ipmc_grp_api_tbl = &ipmc_group_api;
    n_sai_api_tbl->n_sai_rpf_grp_api_tbl = &rpf_group_api;
    n_sai_api_tbl->n_sai_ipmc_api_tbl = &ipmc_entry_api;

    return STD_ERR_OK;
}

static bool operator==(const ndi_ipmc_entry_t& e1, const ndi_ipmc_entry_t& e2)
{
    return true;
}

TEST(nas_ndi_ipmc_test, ndi_api_init)
{
    ASSERT_EQ(ndi_db_global_tbl_alloc(1), STD_ERR_OK);
    auto ndi_db_ptr = ndi_db_ptr_get(0);
    ASSERT_EQ(nas_ndi_sai_api_table_init(&ndi_db_ptr->ndi_sai_api_tbl), STD_ERR_OK);
}

static void generate_port_list(ndi_sw_port_list_t& port_list)
{
    std::srand(std::time(nullptr));
    for (size_t idx = 0; idx < port_list.port_count; idx ++) {
        int rand_val = std::rand() % 2;
        if (rand_val == 0) {
            port_list.list[idx].port_type = NDI_SW_PORT_NPU_PORT;
            port_list.list[idx].u.npu_port.npu_id = 0;
            port_list.list[idx].u.npu_port.npu_port = idx;
        } else {
            port_list.list[idx].port_type = NDI_SW_PORT_LAG;
            port_list.list[idx].u.lag = 1000 + idx;
        }
    }
}

size_t rpf_port_num = 10;
size_t ipmc_mbr_cnt = 5;
size_t ipmc_port_num = 20;

ndi_rif_id_t rpf_rif_id = 100;
ndi_rif_id_t ipmc_rif_id = 200;

std::vector<std::tuple<ndi_obj_id_t, std::pair<ndi_rif_id_t, std::set<ndi_sw_port_t>>,
                       std::unordered_map<ndi_rif_id_t, std::set<ndi_sw_port_t>>>> saved_repl_grp{};

TEST(nas_ndi_ipmc_test, create_repl_group)
{
    std::unique_ptr<ndi_sw_port_t[]> port_list_ptr{new ndi_sw_port_t[rpf_port_num]};
    ndi_sw_port_list_t rpf_port_list{rpf_port_num, port_list_ptr.get()};
    generate_port_list(rpf_port_list);
    std::set<ndi_sw_port_t> rpf_port_set{};
    for (size_t idx = 0; idx < rpf_port_list.port_count; idx ++) {
        rpf_port_set.insert(rpf_port_list.list[idx]);
    }
    ndi_mc_grp_mbr_t rpf_mbr{rpf_rif_id, rpf_port_list};

    std::unordered_map<ndi_rif_id_t, std::set<ndi_sw_port_t>> ipmc_rif_list{};
    std::vector<std::unique_ptr<ndi_sw_port_t[]>> mbr_plist_ptr(ipmc_mbr_cnt);
    std::unique_ptr<ndi_mc_grp_mbr_t[]> mbr_list_ptr{new ndi_mc_grp_mbr_t[ipmc_mbr_cnt]};
    for (size_t idx = 0; idx < ipmc_mbr_cnt; idx ++) {
        mbr_list_ptr[idx].rif_id = ipmc_rif_id + idx;
        mbr_plist_ptr[idx] = std::unique_ptr<ndi_sw_port_t[]>{new ndi_sw_port_t[ipmc_port_num]};
        mbr_list_ptr[idx].port_list.port_count = ipmc_port_num;
        mbr_list_ptr[idx].port_list.list = mbr_plist_ptr[idx].get();
        generate_port_list(mbr_list_ptr[idx].port_list);
        std::set<ndi_sw_port_t> ipmc_port_set{};
        for (size_t pt_idx = 0; pt_idx < mbr_list_ptr[idx].port_list.port_count; pt_idx ++) {
            ipmc_port_set.insert(mbr_list_ptr[idx].port_list.list[pt_idx]);
        }
        ipmc_rif_list[mbr_list_ptr[idx].rif_id] = ipmc_port_set;
    }

    ndi_obj_id_t repl_grp_id;
    ASSERT_EQ(ndi_create_repl_group(0, REPL_GROUP_OWNER_IPMC, &rpf_mbr, ipmc_mbr_cnt, mbr_list_ptr.get(),
                                    &repl_grp_id), STD_ERR_OK);
    saved_repl_grp.push_back(std::make_tuple(repl_grp_id, std::make_pair(rpf_rif_id, rpf_port_set), ipmc_rif_list));

    generate_port_list(rpf_port_list);
    rpf_port_set.clear();
    for (size_t idx = 0; idx < rpf_port_list.port_count; idx ++) {
        rpf_port_set.insert(rpf_port_list.list[idx]);
    }
    ndi_mc_grp_mbr_t rpf_mbr_1{rpf_rif_id + 1, rpf_port_list};

    ipmc_rif_list.clear();
    for (size_t idx = 0; idx < ipmc_mbr_cnt; idx ++) {
        mbr_list_ptr[idx].rif_id = ipmc_rif_id * 2 + idx;
        mbr_list_ptr[idx].port_list.port_count = ipmc_port_num;
        mbr_list_ptr[idx].port_list.list = mbr_plist_ptr[idx].get();
        generate_port_list(mbr_list_ptr[idx].port_list);
        std::set<ndi_sw_port_t> ipmc_port_set{};
        for (size_t pt_idx = 0; pt_idx < mbr_list_ptr[idx].port_list.port_count; pt_idx ++) {
            ipmc_port_set.insert(mbr_list_ptr[idx].port_list.list[pt_idx]);
        }
        ipmc_rif_list[mbr_list_ptr[idx].rif_id] = ipmc_port_set;
    }
    ASSERT_EQ(ndi_create_repl_group(0, REPL_GROUP_OWNER_IPMC, &rpf_mbr_1, ipmc_mbr_cnt, mbr_list_ptr.get(),
                                    &repl_grp_id), STD_ERR_OK);
    saved_repl_grp.push_back(std::make_tuple(repl_grp_id, std::make_pair(rpf_rif_id + 1, rpf_port_set), ipmc_rif_list));
}

static void ipmc_ut_check_repl_grp()
{
    for (auto grp_info: saved_repl_grp) {
        auto repl_grp = ndi_ipmc_cache_get_repl_grp(0, std::get<0>(grp_info));
        ASSERT_EQ(repl_grp.rpf_mbr_list.size(), 1);
        auto rpf_rif_id = std::get<1>(grp_info).first;
        ASSERT_TRUE(repl_grp.rpf_mbr_list.find(rpf_rif_id) != repl_grp.rpf_mbr_list.end());
        ASSERT_TRUE(std::get<1>(grp_info).second == repl_grp.rpf_mbr_list[rpf_rif_id].port_list);
        ASSERT_EQ(repl_grp.ipmc_mbr_list.size(), std::get<2>(grp_info).size());
        auto& ipmc_mbr_list = std::get<2>(grp_info);
        for (auto itor = ipmc_mbr_list.begin(); itor != ipmc_mbr_list.end(); itor ++) {
            ASSERT_TRUE(repl_grp.ipmc_mbr_list.find(itor->first) != repl_grp.ipmc_mbr_list.end());
            ASSERT_TRUE(repl_grp.ipmc_mbr_list[itor->first].port_list == itor->second);
        }
    }
}

TEST(nas_ndi_ipmc_test, check_repl_group)
{
    ipmc_ut_check_repl_grp();
}

TEST(nas_ndi_ipmc_test, update_repl_group)
{
    ndi_rif_id_t rpf_rif_id = 500, ipmc_rif_id = 600;
    size_t rpf_port_cnt = 20, ipmc_port_cnt = 30;

    for (auto& grp_info: saved_repl_grp) {
        auto repl_grp_id = std::get<0>(grp_info);

        // RPF group member update
        auto& rpf_info = std::get<1>(grp_info);
        ndi_mc_grp_mbr_t rpf_mbr{rpf_info.first};
        ASSERT_EQ(ndi_update_repl_group(0, repl_grp_id, DEL_GRP_MBR, NDI_RPF_GRP_MBR, &rpf_mbr), STD_ERR_OK);
        rpf_mbr.rif_id = rpf_rif_id;
        std::unique_ptr<ndi_sw_port_t[]> port_list{new ndi_sw_port_t[rpf_info.second.size()]};
        size_t idx = 0;
        for (auto& sw_port: rpf_info.second) {
            port_list[idx ++] = sw_port;
        }
        rpf_mbr.port_list = ndi_sw_port_list_t{rpf_info.second.size(), port_list.get()};
        ASSERT_EQ(ndi_update_repl_group(0, repl_grp_id, ADD_GRP_MBR, NDI_RPF_GRP_MBR, &rpf_mbr), STD_ERR_OK);
        port_list.reset(new ndi_sw_port_t[rpf_port_cnt]);
        rpf_mbr.port_list = ndi_sw_port_list_t{rpf_port_cnt, port_list.get()};
        generate_port_list(rpf_mbr.port_list);
        ASSERT_EQ(ndi_update_repl_group(0, repl_grp_id, UPDATE_GRP_MBR, NDI_RPF_GRP_MBR, &rpf_mbr), STD_ERR_OK);
        rpf_info.first = rpf_rif_id;
        rpf_info.second.clear();
        for (size_t idx = 0; idx < rpf_mbr.port_list.port_count; idx ++) {
            rpf_info.second.insert(rpf_mbr.port_list.list[idx]);
        }
        rpf_rif_id ++;

        // IPMC group member update
        auto& ipmc_info = std::get<2>(grp_info);
        ASSERT_FALSE(ipmc_info.empty());
        auto itor = ipmc_info.begin();
        ndi_mc_grp_mbr_t ipmc_mbr{itor->first};
        ASSERT_EQ(ndi_update_repl_group(0, repl_grp_id, DEL_GRP_MBR, NDI_IPMC_GRP_MBR, &ipmc_mbr), STD_ERR_OK);
        ipmc_mbr.rif_id = ipmc_rif_id;
        port_list.reset(new ndi_sw_port_t[itor->second.size()]);
        idx = 0;
        for (auto& sw_port: itor->second) {
            port_list[idx ++] = sw_port;
        }
        ipmc_mbr.port_list = ndi_sw_port_list_t{itor->second.size(), port_list.get()};
        ASSERT_EQ(ndi_update_repl_group(0, repl_grp_id, ADD_GRP_MBR, NDI_IPMC_GRP_MBR, &ipmc_mbr), STD_ERR_OK);
        auto mbr_port_list = itor->second;
        ipmc_info.erase(itor);
        ipmc_info.insert(std::make_pair(ipmc_rif_id, mbr_port_list));
        port_list.reset(new ndi_sw_port_t[ipmc_port_cnt]);
        ipmc_mbr.rif_id = ipmc_rif_id;
        ipmc_mbr.port_list = ndi_sw_port_list_t{ipmc_port_cnt, port_list.get()};
        generate_port_list(ipmc_mbr.port_list);
        ASSERT_EQ(ndi_update_repl_group(0, repl_grp_id, UPDATE_GRP_MBR, NDI_IPMC_GRP_MBR, &ipmc_mbr), STD_ERR_OK);
        ipmc_info[ipmc_rif_id].clear();
        for (size_t idx = 0; idx < ipmc_mbr.port_list.port_count; idx ++) {
            ipmc_info[ipmc_rif_id].insert(ipmc_mbr.port_list.list[idx]);
        }
        ipmc_rif_id ++;
    }
}

TEST(nas_ndi_ipmc_test, check_repl_group_1)
{
    ipmc_ut_check_repl_grp();
}


std::unordered_map<ndi_obj_id_t, std::vector<ndi_ipmc_entry_t>> saved_ipmc_entry{};
ndi_vrf_id_t test_vrf_id = 100;

static void increment_ip_addr(hal_ip_addr_t& ip_addr, size_t offset = 1)
{
    if (ip_addr.af_index == HAL_INET4_FAMILY) {
        ip_addr.u.v4_addr += (offset << 24);
    } else {
        ip_addr.u.v6_addr[15] += offset;
    }
}

TEST(nas_ndi_ipmc_test, create_ipmc_entry)
{
    const char *ipv4_grp_addr_1 = "230.1.1.1";
    const char *ipv4_grp_addr_2 = "232.5.5.5";
    const char *ipv4_src_addr = "2.2.2.2";
    const char *ipv6_grp_addr_1 = "ff0e::1111";
    const char *ipv6_grp_addr_2 = "ff10::6666";
    const char *ipv6_src_addr = "8888::8888";


    size_t ip_off = 0;
    for (auto& grp_info: saved_repl_grp) {
        auto repl_grp_id = std::get<0>(grp_info);
        ASSERT_TRUE(saved_ipmc_entry.find(repl_grp_id) == saved_ipmc_entry.end());
        saved_ipmc_entry[repl_grp_id] = std::vector<ndi_ipmc_entry_t>{};
        hal_ip_addr_t grp_ip;
        hal_ip_addr_t src_ip;

        // IPv4 (*, G) entry create
        ASSERT_TRUE(std_str_to_ip(ipv4_grp_addr_1, &grp_ip));
        increment_ip_addr(grp_ip, ip_off);
        ndi_ipmc_entry_t xg_v4_entry{test_vrf_id, NAS_NDI_IPMC_ENTRY_TYPE_XG, grp_ip, {}, repl_grp_id, true};
        ASSERT_EQ(ndi_ipmc_entry_create(0, &xg_v4_entry), STD_ERR_OK);
        saved_ipmc_entry[repl_grp_id].push_back(xg_v4_entry);

        // IPv4 (S, G) entry create
        ASSERT_TRUE(std_str_to_ip(ipv4_grp_addr_2, &grp_ip));
        increment_ip_addr(grp_ip, ip_off);
        ASSERT_TRUE(std_str_to_ip(ipv4_src_addr, &src_ip));
        ndi_ipmc_entry_t sg_v4_entry{test_vrf_id, NAS_NDI_IPMC_ENTRY_TYPE_SG, grp_ip, src_ip, repl_grp_id, false};
        ASSERT_EQ(ndi_ipmc_entry_create(0, &sg_v4_entry), STD_ERR_OK);
        saved_ipmc_entry[repl_grp_id].push_back(sg_v4_entry);

        // IPv6 (*, G) entry create
        ASSERT_TRUE(std_str_to_ip(ipv6_grp_addr_1, &grp_ip));
        increment_ip_addr(grp_ip, ip_off);
        ndi_ipmc_entry_t xg_v6_entry{test_vrf_id, NAS_NDI_IPMC_ENTRY_TYPE_XG, grp_ip, {}, repl_grp_id, true};
        ASSERT_EQ(ndi_ipmc_entry_create(0, &xg_v6_entry), STD_ERR_OK);
        saved_ipmc_entry[repl_grp_id].push_back(xg_v6_entry);

        // IPv4 (S, G) entry create
        ASSERT_TRUE(std_str_to_ip(ipv6_grp_addr_2, &grp_ip));
        increment_ip_addr(grp_ip, ip_off);
        ASSERT_TRUE(std_str_to_ip(ipv6_src_addr, &src_ip));
        ndi_ipmc_entry_t sg_v6_entry{test_vrf_id, NAS_NDI_IPMC_ENTRY_TYPE_SG, grp_ip, src_ip, repl_grp_id, false};
        ASSERT_EQ(ndi_ipmc_entry_create(0, &sg_v6_entry), STD_ERR_OK);
        saved_ipmc_entry[repl_grp_id].push_back(sg_v6_entry);

        ip_off ++;
    }
}

static std::ostream& operator<<(std::ostream& os, const ndi_cache_grp_mbr_t& grp_mbr)
{
    os << "<RIF " << grp_mbr.rif_id << " PORT ";
    for (auto& port: grp_mbr.port_list) {
        if (port.port_type == NDI_SW_PORT_NPU_PORT) {
            os << "P_" << port.u.npu_port.npu_port;
        } else {
            os << "L_" << port.u.lag;
        }
        os << ",";
    }
    os << ">";
    return os;
}

static void ipmc_ut_dump_repl_group()
{
    for (auto& grp_info: saved_repl_grp) {
        auto repl_grp = ndi_ipmc_cache_get_repl_grp(0, std::get<0>(grp_info));
        ASSERT_EQ(repl_grp.rpf_mbr_list.size(), 1);
        std::cout << "ID " << repl_grp.repl_grp_id << " IIF ";
        std::cout << repl_grp.rpf_mbr_list.begin()->second << " OIF ";
        for (auto& ipmc_mbr: repl_grp.ipmc_mbr_list) {
            std::cout << ipmc_mbr.second << ",";
        }
        std::cout << std::endl;
        for (auto& entry: repl_grp.ipmc_ipv4_entry_list) {
            ndi_ipmc_entry_t ndi_entry{};
            std::cout << "  " << entry.second.to_ndi_entry(ndi_entry) << std::endl;
        }
        for (auto& entry: repl_grp.ipmc_ipv6_entry_list) {
            ndi_ipmc_entry_t ndi_entry{};
            std::cout << "  " << entry.second.to_ndi_entry(ndi_entry) << std::endl;
        }
    }
}

TEST(nas_ndi_ipmc_test, dump_all_groups)
{
    ipmc_ut_dump_repl_group();
}

static void ipmc_ut_check_ipmc_entry()
{
    for (auto& entry_info: saved_ipmc_entry) {
        try {
            auto repl_grp = ndi_ipmc_cache_get_repl_grp(0, entry_info.first);
            ASSERT_EQ(repl_grp.ipmc_ipv4_entry_list.size() + repl_grp.ipmc_ipv6_entry_list.size(),
                      entry_info.second.size());
            for (auto& entry: entry_info.second) {
                if (entry.dst_ip.af_index == HAL_INET4_FAMILY) {
                    auto itor = repl_grp.ipmc_ipv4_entry_list.find(0, entry);
                    ASSERT_TRUE(itor != repl_grp.ipmc_ipv4_entry_list.end());
                    ASSERT_EQ(itor->second.copy_to_cpu(), entry.copy_to_cpu);
                    ASSERT_EQ(itor->second.repl_group_id(), entry.repl_group_id);
                } else {
                    auto itor = repl_grp.ipmc_ipv6_entry_list.find(0, entry);
                    ASSERT_TRUE(itor != repl_grp.ipmc_ipv6_entry_list.end());
                    ASSERT_EQ(itor->second.copy_to_cpu(), entry.copy_to_cpu);
                    ASSERT_EQ(itor->second.repl_group_id(), entry.repl_group_id);
                }
            }
        } catch (std::out_of_range& ex) {
            ASSERT_TRUE(false);
        }
    }
}

TEST(nas_ndi_ipmc_test, check_ipmc_entry)
{
    ipmc_ut_check_ipmc_entry();
}

TEST(nas_ndi_ipmc_test, update_ipmc_entry)
{
    auto itor = saved_repl_grp.begin();
    auto repl_grp_id = std::get<0>(*itor);

    std::vector<ndi_ipmc_entry_t> upd_entry_list{};

    for (auto& saved_info: saved_ipmc_entry) {
        for (auto& entry: saved_info.second) {
            upd_entry_list.push_back(entry);
        }
    }

    for (auto& entry: upd_entry_list) {
        entry.copy_to_cpu = !entry.copy_to_cpu;
        ASSERT_EQ(ndi_ipmc_entry_update(0, &entry, NAS_NDI_IPMC_UPD_COPY_TO_CPU), STD_ERR_OK);
    }

    for (auto& entry: upd_entry_list) {
        if (entry.repl_group_id != repl_grp_id) {
            entry.repl_group_id = repl_grp_id;
            ASSERT_EQ(ndi_ipmc_entry_update(0, &entry, NAS_NDI_IPMC_UPD_REPL_GRP), STD_ERR_OK);
        }
    }

    saved_ipmc_entry.clear();
    for (auto& entry: upd_entry_list) {
        if (saved_ipmc_entry.find(entry.repl_group_id) == saved_ipmc_entry.end()) {
            saved_ipmc_entry[entry.repl_group_id] = std::vector<ndi_ipmc_entry_t>{};
        }
        saved_ipmc_entry[entry.repl_group_id].push_back(entry);
    }
}

TEST(nas_ndi_ipmc_test, get_ipmc_entry)
{
    for (auto& saved_info: saved_ipmc_entry) {
        for (auto& entry: saved_info.second) {
            ndi_ipmc_entry_t rd_entry{entry};
            ASSERT_EQ(ndi_ipmc_entry_get(0, &rd_entry), STD_ERR_OK);
            ASSERT_EQ(entry, rd_entry);
        }
    }
}

TEST(nas_ndi_ipmc_test, dump_all_groups_1)
{
    ipmc_ut_dump_repl_group();
}

TEST(nas_ndi_ipmc_test, check_ipmc_entry_1)
{
    ipmc_ut_check_ipmc_entry();
}

TEST(nas_ndi_ipmc_test, delete_ipmc_entry)
{
    for (auto& saved_info: saved_ipmc_entry) {
        auto repl_grp = ndi_ipmc_cache_get_repl_grp(0, std::get<0>(saved_info));
        for (auto& entry: repl_grp.ipmc_ipv4_entry_list) {
            ndi_ipmc_entry_t ndi_entry{};
            ASSERT_EQ(ndi_ipmc_entry_delete(0, &entry.second.to_ndi_entry(ndi_entry)), STD_ERR_OK);
        }
        for (auto& entry: repl_grp.ipmc_ipv6_entry_list) {
            ndi_ipmc_entry_t ndi_entry{};
            ASSERT_EQ(ndi_ipmc_entry_delete(0, &entry.second.to_ndi_entry(ndi_entry)), STD_ERR_OK);
        }
    }
    for (auto& saved_info: saved_ipmc_entry) {
        auto repl_grp = ndi_ipmc_cache_get_repl_grp(0, std::get<0>(saved_info));
        ASSERT_TRUE(repl_grp.ipmc_ipv4_entry_list.empty());
        ASSERT_TRUE(repl_grp.ipmc_ipv6_entry_list.empty());
    }
}

TEST(nas_ndi_ipmc_test, delete_repl_group)
{
    for (auto grp_info: saved_repl_grp) {
        ASSERT_EQ(ndi_delete_repl_group(0, std::get<0>(grp_info)), STD_ERR_OK);
    }
    for (auto& grp_info: saved_repl_grp) {
        try {
            auto repl_grp = ndi_ipmc_cache_get_repl_grp(0, std::get<0>(grp_info));
            ASSERT_TRUE(false);
        } catch(std::out_of_range& ex) {
            continue;
        }
    }
    saved_repl_grp.clear();
}

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
