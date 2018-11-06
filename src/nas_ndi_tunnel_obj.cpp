/*
 * Copyright (c) 2018 Dell Inc.
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
 * filename: nas_ndi_tunnel_obj.cpp
 */



#include "nas_ndi_tunnel_obj.h"
#include "std_ip_utils.h"

bool TunnelObj::_insert(_vni_sai_obj_map_t *map, sai_object_id_t bridge_oid, uint32_t vni, sai_object_id_t oid)
{
    vni_s_oid_map_t vni_sai_oid;
    vni_sai_oid.vni = vni;
    vni_sai_oid.oid = oid;

    auto it = map->find(bridge_oid);

    if (it != map->end()) {
        return false;
    }
    auto ret = map->insert({bridge_oid, vni_sai_oid});

    if (ret.second == false) {
        return false;
    }
    return true;
}

bool TunnelObj::_remove(_vni_sai_obj_map_t *map, sai_object_id_t br_oid)
{
    auto it = map->find(br_oid);

    if (it == map->end()) {
        return false;
    }
    map->erase(it);
    return true;
}


bool TunnelObj::_get(_vni_sai_obj_map_t *map, sai_object_id_t br_oid, vni_s_oid_map_t *map_val)
{
    auto it = map->find(br_oid);

    if (it == map->end()) {
        return false;
    }
    memcpy(map_val, &(it->second), sizeof(vni_s_oid_map_t));
    return true;
}

sai_object_id_t TunnelObj::get_tun_oid()
{
    return tun_oid;
}

sai_object_id_t TunnelObj::get_encap_map_oid()
{
    return encap_map_oid;
}

sai_object_id_t TunnelObj::get_decap_map_oid()
{
    return decap_map_oid;
}

sai_object_id_t TunnelObj::get_term_oid()
{
    return term_oid;
}

bool TunnelObj::get_local_src_ip(hal_ip_addr_t *loc_ip)
{
    memcpy(loc_ip, &local_src_ip, sizeof(hal_ip_addr_t));
    return true;
}

bool TunnelObj::get_remote_src_ip( hal_ip_addr_t *rem_ip)
{
    memcpy(rem_ip, &remote_src_ip, sizeof(hal_ip_addr_t));
    return true;
}

bool TunnelObj::insert_encap_map_entry(sai_object_id_t br_oid, uint32_t vni, sai_object_id_t encap_map_entry_oid)
{
    return _insert(&encap_map, br_oid , vni, encap_map_entry_oid);
}

bool TunnelObj::remove_encap_map_entry(sai_object_id_t br_oid)
{
    return _remove(&encap_map, br_oid );
}

bool TunnelObj::get_encap_map_entry(sai_object_id_t br_oid, vni_s_oid_map_t *map_val)
{
    return _get(&encap_map, br_oid, map_val);
}

bool TunnelObj::insert_decap_map_entry(sai_object_id_t br_oid, uint32_t vni, sai_object_id_t decap_map_entry_oid)
{
    return _insert(&decap_map, br_oid, vni, decap_map_entry_oid);
}

bool TunnelObj::remove_decap_map_entry(sai_object_id_t br_oid)
{
    return _remove(&decap_map, br_oid);
}

bool TunnelObj::get_decap_map_entry(sai_object_id_t br_oid, vni_s_oid_map_t *map_val)
{
    return _get(&decap_map, br_oid, map_val);
}

bool TunnelObj::insert_tunnel_bridge_port(sai_object_id_t br_oid, uint32_t vni, sai_object_id_t bridge_port_oid)
{
    return _insert(&tunnel_bridge_ports, br_oid, vni, bridge_port_oid);
}

bool TunnelObj::remove_tunnel_bridge_port(sai_object_id_t br_oid)
{
    return _remove(&tunnel_bridge_ports, br_oid);
}

bool TunnelObj::get_bridge_port(sai_object_id_t br_oid, vni_s_oid_map_t *map_val)
{
    return _get(&tunnel_bridge_ports, br_oid, map_val);
}

int TunnelObj::get_tunnel_bridge_ports_size()
{
    return tunnel_bridge_ports.size();
}

void TunnelObj::print()
{
    std::cout << "\n Tunnel OID: " << tun_oid;
    std::cout << "\n Encap Map Tunnel OID: " << encap_map_oid;
    std::cout << "\n Decap Map Tunnel OID: " << decap_map_oid;

    char                    ip_str [HAL_INET6_TEXT_LEN] = {0};

    std_ip_to_string (&local_src_ip, ip_str,sizeof (ip_str));

    std::cout << "\n Local Source IP: " << ip_str;

    std_ip_to_string (&remote_src_ip, ip_str,sizeof (ip_str));
    std::cout << "\n Remote Source IP: " << ip_str;

    std::cout << "\n Encap Map Entries: \n";
    for(_vni_sai_obj_map_t::const_iterator it = encap_map.begin();
        it != encap_map.end(); ++it)
    {
        std::cout << it->first << " vni " << it->second.vni << " sai id " << it->second.oid << "\n";
    }

    std::cout << "\n Decap Map Entries: \n";
    for(_vni_sai_obj_map_t::const_iterator it = decap_map.begin();
        it != decap_map.end(); ++it)
    {
        std::cout << it->first << " vni " << it->second.vni << " sai id " << it->second.oid << "\n";
    }

    std::cout << "\n Tunnel Bridge Port Entries: \n";
    for(_vni_sai_obj_map_t::const_iterator it = tunnel_bridge_ports.begin();
        it != tunnel_bridge_ports.end(); ++it)
    {
        std::cout << it->first << " vni " << it->second.vni << " sai id " << it->second.oid << "\n";
    }
    std::cout << "\n";
}
