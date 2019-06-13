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
 * filename: nas_ndi_ipmc_utl.h
 */

#ifndef __NAS_NDI_IPMC_UTL_H__
#define __NAS_NDI_IPMC_UTL_H__

#include "std_error_codes.h"
#include "nas_types.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_mcast.h"
#include "nas_ndi_ipmc.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <set>
#include <mutex>
#include <functional>

namespace std
{
    template<>
    struct less<ndi_sw_port_t>
    {
        bool operator()(const ndi_sw_port_t& p1, const ndi_sw_port_t& p2)
        {
            if (p1.port_type != p2.port_type) {
                return p1.port_type < p2.port_type;
            }
            switch(p1.port_type) {
            case NDI_SW_PORT_NPU_PORT:
                if (p1.u.npu_port.npu_id != p2.u.npu_port.npu_id) {
                    return p1.u.npu_port.npu_id < p2.u.npu_port.npu_id;
                }
                return p1.u.npu_port.npu_port < p2.u.npu_port.npu_port;
            case NDI_SW_PORT_LAG:
                return p1.u.lag < p2.u.lag;
            default:
                return false;
            }
        }
    };
}

static inline bool operator==(const ndi_sw_port_t& p1, const ndi_sw_port_t& p2)
{
    if (p1.port_type != p2.port_type) {
        return false;
    }
    switch(p1.port_type) {
    case NDI_SW_PORT_NPU_PORT:
        return (p1.u.npu_port.npu_id == p2.u.npu_port.npu_id) &&
               (p1.u.npu_port.npu_port == p2.u.npu_port.npu_port);
    case NDI_SW_PORT_LAG:
        return p1.u.lag == p2.u.lag;
    default:
        return false;
    }
}

struct ndi_cache_grp_mbr_t
{
    ndi_obj_id_t mbr_id;
    ndi_rif_id_t rif_id;
    std::set<ndi_sw_port_t> port_list;

    void update_port_list(const ndi_sw_port_list_t& plist)
    {
        port_list.clear();
        for (size_t idx = 0; idx < plist.port_count; idx ++) {
            port_list.insert(plist.list[idx]);
        }
    }

    ndi_cache_grp_mbr_t(ndi_obj_id_t grp_mbr_id, ndi_rif_id_t rif_id, const ndi_sw_port_list_t& plist) :
        mbr_id(grp_mbr_id), rif_id(rif_id)
    {
        update_port_list(plist);
    }

    ndi_cache_grp_mbr_t()
    {
        mbr_id = 0;
        rif_id = 0;
    }

    operator std::string() const;
};

template<typename T>
inline void convert_to_af_ip(T& dst_ip, const hal_ip_addr_t& src_ip)
{
    throw std::bad_function_call{"Not supported"};
}

template<>
inline void convert_to_af_ip<dn_ipv4_addr_t>(dn_ipv4_addr_t& dst_ip, const hal_ip_addr_t& src_ip)
{
    dst_ip.s_addr = src_ip.u.ipv4.s_addr;
}

template<>
inline void convert_to_af_ip<dn_ipv6_addr_t>(dn_ipv6_addr_t& dst_ip, const hal_ip_addr_t& src_ip)
{
    memcpy(dst_ip.s6_addr, src_ip.u.ipv6.s6_addr, sizeof(dst_ip.s6_addr));
}

template<typename T>
inline void convert_to_common_ip(hal_ip_addr_t& dst_ip, const T& src_ip)
{
    throw std::bad_function_call{"Not supported"};
}

template<>
inline void convert_to_common_ip<dn_ipv4_addr_t>(hal_ip_addr_t& dst_ip, const dn_ipv4_addr_t& src_ip)
{
    dst_ip.u.ipv4.s_addr = src_ip.s_addr;
    dst_ip.af_index = AF_INET;
}

template<>
inline void convert_to_common_ip<dn_ipv6_addr_t>(hal_ip_addr_t& dst_ip, const dn_ipv6_addr_t& src_ip)
{
    memcpy(dst_ip.u.ipv6.s6_addr, src_ip.s6_addr, sizeof(src_ip.s6_addr));
    dst_ip.af_index = AF_INET6;
}

class cached_ipmc_entry
{
public:
    cached_ipmc_entry(const ndi_ipmc_entry_t& ndi_entry) :
        _vrf_id(ndi_entry.vrf_id), _type(ndi_entry.type),
        _iif_rif_id(ndi_entry.iif_rif_id),
        _repl_group_id(ndi_entry.repl_group_id),
        _copy_to_cpu(ndi_entry.copy_to_cpu) {}

    size_t get_entry_hash() const
    {
        size_t h_val = std::hash<unsigned long>()(_vrf_id);
        h_val ^= std::hash<int>()(static_cast<int>(_type)) << 1;
        h_val ^= std::hash<unsigned long>()(_iif_rif_id) << 1;
        return h_val;
    }

    bool operator==(const cached_ipmc_entry& entry) const
    {
        return _vrf_id == entry._vrf_id && _type == entry._type && _iif_rif_id == entry._iif_rif_id;
    }

    ndi_vrf_id_t vrf_id() const {return _vrf_id;}
    ndi_ipmc_entry_type_t entry_type() const {return _type;}
    ndi_rif_id_t iif_rif_id() const {return _iif_rif_id;}
    ndi_obj_id_t repl_group_id() const {return _repl_group_id;}
    bool copy_to_cpu() const {return _copy_to_cpu;}

    void set_repl_group(ndi_obj_id_t grp_id) const
    {
        _repl_group_id = grp_id;
    }
    void set_copy_to_cpu(bool copy_to_cpu) const
    {
        _copy_to_cpu = copy_to_cpu;
    }

    void conv_to_ndi_entry(ndi_ipmc_entry_t& ndi_entry) const
    {
        ndi_entry.vrf_id = _vrf_id;
        ndi_entry.type = _type;
        ndi_entry.iif_rif_id = _iif_rif_id;
        ndi_entry.repl_group_id = _repl_group_id;
        ndi_entry.copy_to_cpu = _copy_to_cpu;
    }
private:
    ndi_vrf_id_t _vrf_id;
    ndi_ipmc_entry_type_t _type;
    ndi_rif_id_t _iif_rif_id;
    mutable ndi_obj_id_t _repl_group_id;
    mutable bool _copy_to_cpu;

protected:
    virtual void set_ip_addr(const ndi_ipmc_entry_t& ndi_entry) = 0;
};

template<typename T>
class cached_ipmc_af_entry : public cached_ipmc_entry
{
public:
    cached_ipmc_af_entry(const ndi_ipmc_entry_t& ndi_entry) :
        cached_ipmc_entry(ndi_entry)
    {
        set_ip_addr(ndi_entry);
    }
    const T& group_ip_addr() const {return _dst_ip;}
    const T& source_ip_addr() const {return _src_ip;}
    const ndi_ipmc_entry_t& to_ndi_entry(ndi_ipmc_entry_t& ndi_entry) const;
    operator std::string() const;
protected:
    virtual void set_ip_addr(const ndi_ipmc_entry_t& ndi_entry);
private:
    T _dst_ip;
    T _src_ip;
};

template<typename T>
void cached_ipmc_af_entry<T>::set_ip_addr(const ndi_ipmc_entry_t& ndi_entry)
{
    convert_to_af_ip(_dst_ip, ndi_entry.dst_ip);
    if (ndi_entry.type == NAS_NDI_IPMC_ENTRY_TYPE_SG) {
        convert_to_af_ip(_src_ip, ndi_entry.src_ip);
    }
}

template<typename T>
inline const ndi_ipmc_entry_t& cached_ipmc_af_entry<T>::to_ndi_entry(ndi_ipmc_entry_t& ndi_entry) const
{
    return ndi_entry;
}

template<>
inline const ndi_ipmc_entry_t& cached_ipmc_af_entry<dn_ipv4_addr_t>::to_ndi_entry(ndi_ipmc_entry_t& ndi_entry) const
{
    conv_to_ndi_entry(ndi_entry);
    ndi_entry.dst_ip.af_index = HAL_INET4_FAMILY;
    ndi_entry.dst_ip.u.v4_addr = _dst_ip.s_addr;
    if (entry_type() == NAS_NDI_IPMC_ENTRY_TYPE_SG) {
        ndi_entry.src_ip.af_index = HAL_INET4_FAMILY;
        ndi_entry.src_ip.u.v4_addr = _src_ip.s_addr;
    }
    return ndi_entry;
}

template<>
inline const ndi_ipmc_entry_t& cached_ipmc_af_entry<dn_ipv6_addr_t>::to_ndi_entry(ndi_ipmc_entry_t& ndi_entry) const
{
    conv_to_ndi_entry(ndi_entry);
    ndi_entry.dst_ip.af_index = HAL_INET6_FAMILY;
    memcpy(ndi_entry.dst_ip.u.v6_addr, _dst_ip.s6_addr, sizeof(_dst_ip.s6_addr));
    if (entry_type() == NAS_NDI_IPMC_ENTRY_TYPE_SG) {
        ndi_entry.src_ip.af_index = HAL_INET6_FAMILY;
        memcpy(ndi_entry.src_ip.u.v6_addr, _src_ip.s6_addr, sizeof(_src_ip.s6_addr));
    }
    return ndi_entry;
}

static inline bool operator==(const dn_ipv4_addr_t& ip1, const dn_ipv4_addr_t& ip2)
{
    return ip1.s_addr == ip2.s_addr;
}

static inline bool operator==(const dn_ipv6_addr_t& ip1, const dn_ipv6_addr_t& ip2)
{
    return memcmp(ip1.s6_addr, ip2.s6_addr, sizeof(ip1.s6_addr)) == 0;
}

template<typename T>
bool operator==(const cached_ipmc_af_entry<T>& d1, const cached_ipmc_af_entry<T>& d2)
{
    if (!(static_cast<const cached_ipmc_entry&>(d1) == static_cast<const cached_ipmc_entry&>(d2))) {
        return false;
    }
    if (!(d1.group_ip_addr() == d2.group_ip_addr())) {
        return false;
    }
    if (d1.entry_type() == NAS_NDI_IPMC_ENTRY_TYPE_SG) {
        return d1.source_ip_addr() == d2.source_ip_addr();
    }

    return true;
}

namespace std
{
    template<>
    struct hash<dn_ipv4_addr_t>
    {
        size_t operator()(const dn_ipv4_addr_t& ip) const
        {
            return hash<int>()(static_cast<int>(ip.s_addr));
        }
    };

    template<>
    struct hash<dn_ipv6_addr_t>
    {
        size_t operator()(const dn_ipv6_addr_t& ip) const
        {
            return hash<string>()(string(begin(ip.s6_addr), end(ip.s6_addr) - 1));
        }
    };

    template<typename T>
    struct hash<cached_ipmc_af_entry<T>>
    {
        size_t operator()(const cached_ipmc_af_entry<T>& data) const
        {
            size_t h_val = data.get_entry_hash();
            h_val <<= 1;
            h_val ^= hash<T>()(data.group_ip_addr());
            if (data.entry_type() == NAS_NDI_IPMC_ENTRY_TYPE_SG) {
                h_val <<= 1;
                h_val ^= hash<T>()(data.source_ip_addr());
            }
            return h_val;
        }
    };

    template<>
    struct hash<pair<npu_id_t, ndi_obj_id_t>>
    {
        size_t operator()(const pair<npu_id_t, ndi_obj_id_t>& data) const
        {
            return hash<unsigned int>()(data.first) ^
                   (hash<unsigned long>()(data.second) << 1);
        }
    };

    template<typename T>
    struct hash<pair<npu_id_t, cached_ipmc_af_entry<T>>>
    {
        size_t operator()(const pair<npu_id_t, cached_ipmc_af_entry<T>>& data) const
        {
            return hash<unsigned int>()(data.first) ^
                   (hash<cached_ipmc_af_entry<T>>()(data.second) << 1);
        }
    };
}

class base_ipmc_entry_set
{
public:
    virtual bool add_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry) = 0;
    virtual bool delete_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry) = 0;
    virtual bool update_entry_copy_to_cpu(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry) = 0;
};

template<typename T>
class cached_ipmc_entry_set : public base_ipmc_entry_set
{
public:
    using key_type = std::pair<npu_id_t, cached_ipmc_af_entry<T>>;

    virtual bool add_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        if (_entry_list.find(key) != _entry_list.end()) {
            return false;
        }
        _entry_list.insert(key);
        return true;
    }
    virtual bool delete_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        if (_entry_list.find(key) == _entry_list.end()) {
            return false;
        }
        _entry_list.erase(key);
        return true;
    }
    virtual bool update_entry_copy_to_cpu(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        auto itor = _entry_list.find(key);
        if (itor == _entry_list.end()) {
            return false;
        }
        itor->second.set_copy_to_cpu(ndi_entry.copy_to_cpu);
        return true;
    }
    bool empty() const
    {
        return _entry_list.empty();
    }
    size_t size() const
    {
        return _entry_list.size();
    }
    typename std::unordered_set<key_type>::iterator begin()
    {
        return _entry_list.begin();
    }
    typename std::unordered_set<key_type>::const_iterator begin() const
    {
        return _entry_list.begin();
    }

    typename std::unordered_set<key_type>::iterator end()
    {
        return _entry_list.end();
    }
    typename std::unordered_set<key_type>::const_iterator end() const
    {
        return _entry_list.end();
    }

    typename std::unordered_set<key_type>::iterator find(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        return _entry_list.find(key);
    }
    typename std::unordered_set<key_type>::const_iterator find(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry) const
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        return _entry_list.find(key);
    }

    operator std::string() const;

private:
    std::unordered_set<key_type> _entry_list;
};

struct ndi_cache_repl_grp_t
{
    using mbr_list_t = std::unordered_map<ndi_rif_id_t, ndi_cache_grp_mbr_t, std::hash<unsigned long>>;

    ndi_obj_id_t repl_grp_id;
    ndi_obj_id_t rpf_grp_id;
    mbr_list_t rpf_mbr_list;
    ndi_obj_id_t ipmc_grp_id;
    mbr_list_t ipmc_mbr_list;

    cached_ipmc_entry_set<dn_ipv4_addr_t> ipmc_ipv4_entry_list;
    cached_ipmc_entry_set<dn_ipv6_addr_t> ipmc_ipv6_entry_list;

    ndi_cache_repl_grp_t(ndi_obj_id_t repl_id, ndi_obj_id_t rpf_id, ndi_obj_id_t ipmc_id) :
        repl_grp_id(repl_id), rpf_grp_id(rpf_id), ipmc_grp_id(ipmc_id) {}

    base_ipmc_entry_set& get_cached_entry_list(int af_index)
    {
        if (af_index == HAL_INET4_FAMILY) {
            return ipmc_ipv4_entry_list;
        } else {
            return ipmc_ipv6_entry_list;
        }
    }

    operator std::string() const;
};

class base_ipmc_entry_map
{
public:
    using value_type = std::shared_ptr<ndi_cache_repl_grp_t>;

    virtual bool add_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry, const value_type& obj) = 0;
    virtual bool delete_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry) = 0;
    virtual bool update_entry_repl_grp(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry, const value_type& obj) = 0;
    virtual bool update_entry_copy_to_cpu(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry) = 0;
    virtual bool get_entry(npu_id_t npu_id, ndi_ipmc_entry_t& ndi_entry) = 0;

    virtual const value_type& get_repl_grp_obj(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry) const = 0;
};

template<typename T>
class cached_ipmc_entry_map : public base_ipmc_entry_map
{
public:
    using key_type = std::pair<npu_id_t, cached_ipmc_af_entry<T>>;

    virtual bool add_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry, const value_type& obj)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        if (_entry_list.find(key) != _entry_list.end()) {
            return false;
        }
        _entry_list[key] = obj;
        return true;
    }
    virtual bool delete_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        if (_entry_list.find(key) == _entry_list.end()) {
            return false;
        }
        _entry_list.erase(key);
        return true;
    }
    virtual bool update_entry_repl_grp(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry, const value_type& obj)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        auto itor = _entry_list.find(key);
        if (itor == _entry_list.end()) {
            return false;
        }
        itor->first.second.set_repl_group(ndi_entry.repl_group_id);
        itor->second = obj;
        return true;
    }
    virtual bool update_entry_copy_to_cpu(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        auto itor = _entry_list.find(key);
        if (itor == _entry_list.end()) {
            return false;
        }
        itor->first.second.set_copy_to_cpu(ndi_entry.copy_to_cpu);
        return true;
    }
    virtual bool get_entry(npu_id_t npu_id, ndi_ipmc_entry_t& ndi_entry)
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        auto itor = _entry_list.find(key);
        if (itor == _entry_list.end()) {
            return false;
        }
        ndi_entry.repl_group_id = itor->first.second.repl_group_id();
        ndi_entry.copy_to_cpu = itor->first.second.copy_to_cpu();
        return true;
    }

    virtual const value_type& get_repl_grp_obj(npu_id_t npu_id, const ndi_ipmc_entry_t& ndi_entry) const
    {
        auto key = std::make_pair(npu_id, cached_ipmc_af_entry<T>{ndi_entry});
        return _entry_list.at(key);
    }

    operator std::string() const;

private:
    std::unordered_map<key_type, value_type> _entry_list;
};

class repl_group_cache
{
public:
    bool add_repl_group(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                        ndi_obj_id_t rpf_grp_id, ndi_obj_id_t ipmc_grp_id);
    bool add_rpf_group_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id, ndi_obj_id_t mbr_id,
                              const ndi_mc_grp_mbr_t& grp_mbr);
    bool add_ipmc_group_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id, ndi_obj_id_t mbr_id,
                               const ndi_mc_grp_mbr_t& grp_mbr);
    bool del_group_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id, ndi_mc_grp_mbr_type_t mbr_type,
                          ndi_rif_id_t rif_id);
    bool update_group_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id, ndi_mc_grp_mbr_type_t mbr_type,
                             ndi_rif_id_t rif_id, const ndi_sw_port_list_t& port_list);
    bool del_repl_group(npu_id_t npu_id, ndi_obj_id_t repl_grp_id);
    const ndi_cache_repl_grp_t& get_repl_group(npu_id_t npu_id, ndi_obj_id_t repl_grp_id) const
    {
        return *_repl_grp_list.at(std::make_pair(npu_id, repl_grp_id));
    }
    bool is_repl_group_used(npu_id_t npu_id, ndi_obj_id_t repl_grp_id);

    bool get_sub_group_id(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                          ndi_obj_id_t& rpf_grp_id, ndi_obj_id_t& ipmc_grp_id) const;
    std::vector<ndi_obj_id_t> get_group_member_list(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                                                    ndi_mc_grp_mbr_type_t mbr_type) const;
    bool get_group_rif_member(npu_id_t npu_id, ndi_obj_id_t repl_grp_id, ndi_mc_grp_mbr_type_t mbr_type,
                              ndi_rif_id_t rif_id, ndi_obj_id_t& mbr_id);
    bool add_ipmc_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry);
    bool del_ipmc_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry);
    bool update_ipmc_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry,
                           ndi_ipmc_update_type_t upd_type);
    bool get_ipmc_entry(npu_id_t npu_id, ndi_ipmc_entry_t& ipmc_entry);

    base_ipmc_entry_map& get_cached_entry_list(int af_index)
    {
        if (af_index == HAL_INET4_FAMILY) {
            return _ipmc_ipv4_entry_list;
        } else {
            return _ipmc_ipv6_entry_list;
        }
    }

    void dump_ipmc_group() const;
    void dump_ipmc_entry(int af_index) const;
private:
    std::unordered_map<std::pair<npu_id_t, ndi_obj_id_t>, std::shared_ptr<ndi_cache_repl_grp_t>>
            _repl_grp_list;
    cached_ipmc_entry_map<dn_ipv4_addr_t> _ipmc_ipv4_entry_list;
    cached_ipmc_entry_map<dn_ipv6_addr_t> _ipmc_ipv6_entry_list;

    mutable std::mutex _mutex;
};

t_std_error ndi_ipmc_get_repl_subgrp_id(npu_id_t npu_id, ndi_obj_id_t repl_grp_id,
                            ndi_obj_id_t& rpf_grp_id, ndi_obj_id_t& ipmc_grp_id);

t_std_error ndi_ipmc_cache_add_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry);
t_std_error ndi_ipmc_cache_del_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry);
t_std_error ndi_ipmc_cache_update_entry(npu_id_t npu_id, const ndi_ipmc_entry_t& ipmc_entry,
                                        ndi_ipmc_update_type_t upd_type);
t_std_error ndi_ipmc_cache_get_entry(npu_id_t npu_id, ndi_ipmc_entry_t& ipmc_entry);
const ndi_cache_repl_grp_t& ndi_ipmc_cache_get_repl_grp(npu_id_t npu_id, ndi_obj_id_t repl_grp_id);

std::string ndi_ipmc_ip_to_string(const hal_ip_addr_t& ip_addr);
#endif
