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
 * filename: nas_ndi_tunnel_map.cpp
 */




#include "nas_ndi_tunnel_map.h"
#include "std_rw_lock.h"
#include "std_ip_utils.h"

typedef struct {
  hal_ip_addr_t remote_ip; /* may need source_ip as key too */
  hal_ip_addr_t local_ip;
} br_ip_key_t;

typedef struct ndi_br_ip_addr_comp_less {
     bool operator()(const br_ip_key_t& a, const br_ip_key_t& b)
     {
         if (memcmp(&a.remote_ip.u, &b.remote_ip.u, sizeof(in6_addr)) != 0) {
             return (memcmp(&a.remote_ip.u, &b.remote_ip.u, sizeof(in6_addr)) <0);
         } else {
             return (memcmp(&a.local_ip.u, &b.local_ip.u, sizeof(in6_addr)) <0 );
         }
     }
} ndi_br_ip_addr_comp_less_t;

using _tunnel_obj_map_t = std::map <br_ip_key_t, TunnelObj *, ndi_br_ip_addr_comp_less_t>;

static _tunnel_obj_map_t nas_ndi_tunnel_obj_map;



void print_tunnel_map()
{
    char                    ip_str [HAL_INET6_TEXT_LEN] = {0};
    std::cout << "\n Tunnel Objects Entries: ";
    for(auto it = nas_ndi_tunnel_obj_map.begin();
        it != nas_ndi_tunnel_obj_map.end(); ++it)

    {
        std_ip_to_string (&it->first.remote_ip, ip_str,sizeof (ip_str));

        std::cout << "Remote Source IP: " << ip_str << "\n";
        (*(it->second)).print();
    }
}

bool insert_tunnel_obj(const hal_ip_addr_t *src_ip, const hal_ip_addr_t *loc_ip, TunnelObj *tunnel_obj)
{
    br_ip_key_t key;

    memcpy(&key.remote_ip, src_ip, sizeof(hal_ip_addr_t));
    memcpy(&key.local_ip, loc_ip, sizeof(hal_ip_addr_t));

    auto it = nas_ndi_tunnel_obj_map.find(key);

    if (it != nas_ndi_tunnel_obj_map.end()) {
        return false;
    }

    nas_ndi_tunnel_obj_map.insert(std::make_pair(key, tunnel_obj));
    return true;
}

bool remove_tunnel_obj(const hal_ip_addr_t *src_ip, const hal_ip_addr_t *loc_ip)
{
    br_ip_key_t key;
    memcpy(&key.remote_ip, src_ip, sizeof(hal_ip_addr_t));
    memcpy(&key.local_ip, loc_ip, sizeof(hal_ip_addr_t));

    auto it = nas_ndi_tunnel_obj_map.find(key);

    if (it == nas_ndi_tunnel_obj_map.end()) {
        return false;
    }
    TunnelObj *obj = it->second;
    nas_ndi_tunnel_obj_map.erase(it);
    if (obj != nullptr) {
        delete obj;
        return false;
    }
    return true;
}

TunnelObj *get_tunnel_obj(const hal_ip_addr_t *src_ip, const hal_ip_addr_t *loc_ip)
{
    br_ip_key_t key;

    memcpy(&key.remote_ip, src_ip, sizeof(hal_ip_addr_t));
    memcpy(&key.local_ip, loc_ip, sizeof(hal_ip_addr_t));

    auto it = nas_ndi_tunnel_obj_map.find(key);

    if (it == nas_ndi_tunnel_obj_map.end()) {
        return NULL;
    }
    return (it->second);
}

bool has_tunnel_obj(const hal_ip_addr_t *src_ip,const hal_ip_addr_t *loc_ip)
{
    br_ip_key_t key;

    memcpy(&key.remote_ip, src_ip, sizeof(hal_ip_addr_t));
    memcpy(&key.local_ip, loc_ip, sizeof(hal_ip_addr_t));

    auto it = nas_ndi_tunnel_obj_map.find(key);

    if (it == nas_ndi_tunnel_obj_map.end()) {
        return false;
    }
    return true;
}
