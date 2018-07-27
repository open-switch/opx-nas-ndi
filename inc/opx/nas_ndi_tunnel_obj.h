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
 * filename: nas_ndi_tunnel_obj.h
 */

#ifndef _NAS_NDI_TUNNEL_OBJ_H
#define _NAS_NDI_TUNNEL_OBJ_H

#include "stdint.h"
#include "sai.h"
#include "saitypes.h"
#include "saitunnel.h"
#include "nas_ndi_int.h"
#include "ds_common_types.h"
#include "std_error_codes.h"
#include "stdint.h"

#include <iostream>
#include <map>
#include <string>
#include <stdlib.h>


typedef struct {
     uint32_t vni;
     sai_object_id_t oid;
} vni_s_oid_map_t;


/** VNI ID to SAI Object ID Map */
typedef std::map<sai_object_id_t, vni_s_oid_map_t>           _vni_sai_obj_map_t;

class TunnelObj {

    private:
        sai_object_id_t     tun_oid;
        sai_object_id_t     encap_map_oid;
        sai_object_id_t     decap_map_oid;
        sai_object_id_t     term_oid;
        hal_ip_addr_t       local_src_ip;
        hal_ip_addr_t       remote_src_ip;
        _vni_sai_obj_map_t  encap_map;
        _vni_sai_obj_map_t  decap_map;
        _vni_sai_obj_map_t  tunnel_bridge_ports;

        /** Method to insert an element into a given map */
        bool _insert(_vni_sai_obj_map_t *map, sai_object_id_t bridge_oid, uint32_t vni, sai_object_id_t sai_oid);

        /** Method to remove an element from a given map */
        bool _remove(_vni_sai_obj_map_t *map, sai_object_id_t br_oid);

        /** Method to get an element from a given map */
        bool  _get(_vni_sai_obj_map_t *map, sai_object_id_t br_oid, vni_s_oid_map_t *map_val);

    public:
        TunnelObj() {}
        TunnelObj( sai_object_id_t tun_oid,
                  sai_object_id_t encap_map_oid,
                  sai_object_id_t decap_map_oid,
                  sai_object_id_t term_oid,
                  hal_ip_addr_t local_src_ip,
                  hal_ip_addr_t remote_src_ip) : tun_oid(tun_oid), encap_map_oid(encap_map_oid),
                                               decap_map_oid(decap_map_oid), term_oid(term_oid),
                                               local_src_ip(local_src_ip), remote_src_ip(remote_src_ip) {}

        /** getter for sai tunnel oid */
        sai_object_id_t get_tun_oid();

        /** getter for sai tunnel encap map oid */
        sai_object_id_t get_encap_map_oid();

        /** getter for sai tunnel decap map oid */
        sai_object_id_t get_decap_map_oid();

        /** getter for sai tunnel termination oid */
        sai_object_id_t get_term_oid();

        /** getter for local source ip address */
        bool get_local_src_ip(hal_ip_addr_t *local_ip);

        /** getter for local remote ip address */
        bool get_remote_src_ip(hal_ip_addr_t *remote_ip);

        /** Method to add an entry to tunnel encap map */
        bool insert_encap_map_entry(sai_object_id_t br_oid, uint32_t vni, sai_object_id_t encap_map_entry_oid);

        /** Method to add an entry to tunnel decap map */
        bool insert_decap_map_entry(sai_object_id_t br_oid, uint32_t vni, sai_object_id_t decap_map_entry_oid);

        /** Method to add a bridge port entry to tunnel */
        bool insert_tunnel_bridge_port(sai_object_id_t br_oid, uint32_t vni, sai_object_id_t bridge_port_oid);

        /** Method to remove an entry from encap map */
        bool remove_encap_map_entry(sai_object_id_t br_oid);

        /** Method to remove an entry from decap map */
        bool remove_decap_map_entry(sai_object_id_t br_oid);

        /** Method to remove a bridge port entry from tunnel */
        bool remove_tunnel_bridge_port(sai_object_id_t br_oid);

        /** Method to get an encap map entry */
        bool get_encap_map_entry(sai_object_id_t br_oid, vni_s_oid_map_t *map_val);

        /** Method to get a decap map entry */
        bool get_decap_map_entry(sai_object_id_t br_oid, vni_s_oid_map_t *map_val);

        /** Method to get a bridge port entry */
        bool get_bridge_port(sai_object_id_t br_oid, vni_s_oid_map_t *map_val);

        /** Method to get a bridge port map size */
        int get_tunnel_bridge_ports_size();

        void print();
};

#endif /* _NAS_NDI_TUNNEL_OBJ_H */
