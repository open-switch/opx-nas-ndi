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

#include "nas_ndi_event_logs.h"
#include "ds_common_types.h"
#include "nas_ndi_obj_cache.h"
#include "nas_ndi_obj_cache_utils.h"
#include "std_rw_lock.h"

#include <map>
#include <mutex>
#include <memory>


class ndi_virtual_obj_cache {

private:

    std_rw_lock_t rw_lock;
    using ndi_shared_virtual_obj = std::shared_ptr<ndi_virtual_obj_t>;


    std::map<hal_vlan_id_t, ndi_shared_virtual_obj> _vid_to_obj_map;
    std::map<sai_object_id_t, ndi_shared_virtual_obj> _obj_to_vid_map;

public:
    ndi_virtual_obj_cache() {

        std_rw_lock_create_default(&rw_lock);
    }

    /*
     * Add both the mappings vid to obj id and obj id to vid
     */
    bool add_virtual_obj(ndi_virtual_obj_t * obj);

    /*
     * remove mapping from both the map user needs to pass obj id
     */
    bool remove_virtual_obj(ndi_virtual_obj_t * obj);

    /*
     * depending of obj id is passed or vid other part of object information
     *  would be returned
     */
    bool get_virtual_obj(ndi_virtual_obj_t * obj, ndi_virtual_obj_query_type_t qtype);

};

/*
 * Global instance to maintain all virtual obj mapping
 */
static auto _ndi_virt_obj_cache = new ndi_virtual_obj_cache;


class ndi_brport_slave_cache {

private:

    std_rw_lock_t rw_lock;

    std::map<sai_object_id_t , brport_slave_list > _brport_slave_list;

public:

    ndi_brport_slave_cache() {

        std_rw_lock_create_default(&rw_lock);
    }

    /*
     * add a bridge slave port
     */
    bool add_brport_slave(ndi_brport_slave_obj_t *obj);

    /*
     * remove bridge slave port
     */
    bool remove_brport_slave(ndi_brport_slave_obj_t *obj);

    /*
     * get the list of bridge port slaves for a given bridge port
     */
    bool get_brport_slave_list (sai_object_id_t obj_id, brport_slave_list & list);

    /*
     * call given function for each of the bridge port slaves for a given
     * bridge port
     */
    void brport_slave_callback(sai_object_id_t obj_id, brport_slave_fn fn);

};

class ndi_brport_cache {

private:

    std_rw_lock_t rw_lock;
    using brport_shared_ptr = std::shared_ptr<ndi_brport_obj_t> ;

    typedef struct ndi_bridge_pv {
        sai_object_id_t port_id;
        hal_vlan_id_t vid;

        bool operator < (ndi_bridge_pv const & rhs) const{
            return port_id < rhs.port_id || (port_id == rhs.port_id && vid < rhs.vid);
        }

    }ndi_bridge_pv_t;
    /* Only for  .1Q port to bridge port mapping.
     * Where a port is associated with one bridge port
    */
    std::map<sai_object_id_t, brport_shared_ptr> _port_to_brport_blk;
    std::map<sai_object_id_t, brport_shared_ptr> _brport_to_port_blk;

    /* valid for SUBPORT type bridgeport */
    std::map<ndi_bridge_pv_t, brport_shared_ptr> _pv_to_brport_blk;

    std::map<sai_object_id_t, brport_list> _port_to_brport_list;

public:

    ndi_brport_cache () {
        std_rw_lock_create_default(&rw_lock);
    }

    /*
     * Store the bridge port struct to maintain all types of mapping needed
     * based on struct attributes set
     */
    bool add_bridge_port(ndi_brport_obj_t * obj);

    /*
     * remove the bridge port from all the mappings,brport_obj_id is
     * mandatory
     */
    bool remove_bridge_port(ndi_brport_obj_t * obj);

    /*
     * Get a bridge port struct based the query type and information passed in
     * the struct
     */
    bool get_brport_block(ndi_brport_obj_t * obj, ndi_brport_query_type_t qtype);

    /*
     * Get the list of all bridge port from a given port id
     */
    bool get_brport_list (sai_object_id_t obj_id, brport_list & list);

};

bool ndi_brport_cache::get_brport_block(ndi_brport_obj_t * obj, ndi_brport_query_type_t qtype) {

    if (obj == nullptr) return false;
    std_rw_lock_read_guard lg(&rw_lock);

    switch(qtype) {
        case ndi_brport_query_type_FROM_BRPORT:
            {
                auto it = _brport_to_port_blk.find(obj->brport_obj_id);
                if ( it == _brport_to_port_blk.end()) return false;
                memcpy(obj, &(*(it->second)), sizeof(ndi_brport_obj_t));
                break;
            }

        case ndi_brport_query_type_FROM_PORT_VLAN:
            {
                /* Use for subport type only */
                ndi_bridge_pv_t pv_obj;
                pv_obj.port_id = obj->port_obj_id;
                pv_obj.vid = obj->vlan_id;
                auto pv_it = _pv_to_brport_blk.find(pv_obj);
                if ( pv_it == _pv_to_brport_blk.end()) return false;
                memcpy(obj, &(*(pv_it->second)), sizeof(ndi_brport_obj_t));
                break;
            }
        /* This is only for .1q ports */
        case ndi_brport_query_type_FROM_PORT:
            {
                /* use for physical/lag port  */
                auto blk_it = _port_to_brport_blk.find(obj->port_obj_id);
                if ( blk_it == _port_to_brport_blk.end())  return false;
                memcpy(obj, &(*(blk_it->second)), sizeof(ndi_brport_obj_t));
                break;
            }
        default:
            return false;

    }
    return true;
}


/* may be have only 1D bridge members in this list */
bool ndi_brport_cache::get_brport_list (sai_object_id_t port_oid, brport_list & list)  {

    std_rw_lock_read_guard lg(&rw_lock);
    auto it = _port_to_brport_list.find(port_oid);
    if ( it == _port_to_brport_list.end()) return false;
    list = it->second;
    return true;

}

bool ndi_brport_cache::add_bridge_port(ndi_brport_obj_t *obj) {
    if (obj == nullptr) return false;
    auto var = std::make_shared<ndi_brport_obj_t>(*obj);

    std_rw_lock_write_guard lg(&rw_lock);
    /* Added for only .IQ entries */
    if  (obj->brport_type == ndi_brport_type_PORT) {
      _port_to_brport_blk[obj->port_obj_id] = var;
    }

    _brport_to_port_blk[obj->brport_obj_id] = var;

    if  (obj->brport_type == ndi_brport_type_SUBPORT_UNTAG || obj->brport_type == ndi_brport_type_SUBPORT_TAG) {
        ndi_bridge_pv_t ndi_bridge_pv_val;
        ndi_bridge_pv_val.port_id = obj->port_obj_id;
        ndi_bridge_pv_val.vid = obj->vlan_id;
        _pv_to_brport_blk[ndi_bridge_pv_val] = var;
    }
    /* TUNNEL AND SUBPORT will have lists . Tunnel oid is associated to various bridges */
    _port_to_brport_list[obj->port_obj_id].push_back(obj->brport_obj_id);
    return true;
}
/* needs bridge port id and bridge port type. If type is subport needs  vlan_id also */
bool ndi_brport_cache::remove_bridge_port(ndi_brport_obj_t * obj) {

    bool found = false;
    if (obj == nullptr) return false;
    std_rw_lock_write_guard lg(&rw_lock);

    auto it = _brport_to_port_blk.find(obj->brport_obj_id);
    if ( it == _brport_to_port_blk.end()) return false;
    obj->port_obj_id = it->second->port_obj_id;
    _brport_to_port_blk.erase(it);

    for(auto it_po = _port_to_brport_list[obj->port_obj_id].begin();
               it_po != _port_to_brport_list[obj->port_obj_id].end(); it_po++) {
        if (*it_po == obj->brport_obj_id) {
            _port_to_brport_list[obj->port_obj_id].erase(it_po);
            found = true;
            break;

        }
    }
    if (!found) {
        return false;
    }
    /* This is the last bridge port associated with a port , then delete PORT  :
     * check may be explicit call is better
     */
    if (_port_to_brport_list[obj->port_obj_id].empty() == true) {
        _port_to_brport_list.erase(obj->port_obj_id);
    }
    if (obj->brport_type == ndi_brport_type_SUBPORT_TAG || obj->brport_type == ndi_brport_type_SUBPORT_UNTAG) {
        ndi_bridge_pv_t ndi_bridge_pv_val;
        ndi_bridge_pv_val.port_id = obj->port_obj_id;
        ndi_bridge_pv_val.vid = obj->vlan_id;
        auto it_pv = _pv_to_brport_blk.find(ndi_bridge_pv_val);
        if ( it_pv == _pv_to_brport_blk.end()) return false;
        _pv_to_brport_blk.erase(it_pv);
    }

    if  (obj->brport_type == ndi_brport_type_PORT) {
        auto br_it = _port_to_brport_blk.find(obj->port_obj_id);
        if ( br_it == _port_to_brport_blk.end()) return false;
        _port_to_brport_blk.erase(br_it);
    }
    return true;
}

/*
 * Global instance to maintain all bridge port cache mapping
 */
static auto _ndi_brport_cache = new ndi_brport_cache;

bool nas_ndi_add_bridge_port_obj(ndi_brport_obj_t * obj){
    return _ndi_brport_cache->add_bridge_port(obj);
}



bool nas_ndi_remove_bridge_port_obj(ndi_brport_obj_t * obj){
    return _ndi_brport_cache->remove_bridge_port(obj);
}


bool nas_ndi_get_bridge_port_obj(ndi_brport_obj_t * obj, ndi_brport_query_type_t qtype){
    return _ndi_brport_cache->get_brport_block(obj,qtype);
}

bool nas_ndi_get_bridge_port_obj_list(sai_object_id_t obj_id, brport_list & list){
    return _ndi_brport_cache->get_brport_list(obj_id,list);
}


/*
 * Global instance to maintain all bridge port slave cache
 */
static auto _ndi_brport_slave_cache = new ndi_brport_slave_cache;


bool ndi_virtual_obj_cache::add_virtual_obj(ndi_virtual_obj_t * obj){

    if (obj == nullptr) return false;
    auto v_obj = std::make_shared<ndi_virtual_obj_t>(*obj);
    std_rw_lock_write_guard lg(&rw_lock);
    _vid_to_obj_map[v_obj->vid] = v_obj;
    _obj_to_vid_map[v_obj->oid] = v_obj;
    return true;


}

bool ndi_virtual_obj_cache::remove_virtual_obj(ndi_virtual_obj_t *obj){
    if (obj == nullptr) return false;
    std_rw_lock_write_guard lg(&rw_lock);
    auto it = _obj_to_vid_map.find(obj->oid);
    if ( it == _obj_to_vid_map.end()) return false;
    _vid_to_obj_map.erase(it->second->vid);
    _obj_to_vid_map.erase(it);
    return true;

}

bool ndi_virtual_obj_cache::get_virtual_obj(ndi_virtual_obj_t * obj,ndi_virtual_obj_query_type_t qtype){
    if (obj == nullptr) return false;

    std_rw_lock_read_guard lg(&rw_lock);
    if (qtype == ndi_virtual_obj_query_type_FROM_OBJ){
        auto it = _obj_to_vid_map.find(obj->oid);
        if(it == _obj_to_vid_map.end()){
            return false;
        }
        obj->vid = it->second->vid;
    }else if (qtype == ndi_virtual_obj_query_type_FROM_VLAN ){
        auto it = _vid_to_obj_map.find(obj->vid);
        if(it == _vid_to_obj_map.end()){
            return false;
        }
        obj->oid = it->second->oid;
    }else{
        return false;
    }

    return true;
}


bool nas_ndi_add_virtual_obj(ndi_virtual_obj_t * obj){
    return _ndi_virt_obj_cache->add_virtual_obj(obj);
};


bool nas_ndi_remove_virtual_obj(ndi_virtual_obj_t * obj){
    return _ndi_virt_obj_cache->remove_virtual_obj(obj);
}


bool nas_ndi_get_virtual_obj(ndi_virtual_obj_t * obj,ndi_virtual_obj_query_type_t qtype){
    return _ndi_virt_obj_cache->get_virtual_obj(obj,qtype);
}


bool ndi_brport_slave_cache::add_brport_slave (ndi_brport_slave_obj_t * obj){
    if (obj == nullptr) return false;
    std_rw_lock_write_guard lg(&rw_lock);
    auto it = _brport_slave_list.find(obj->master_oid);
    if(it == _brport_slave_list.end()){
        brport_slave_list l;
        l.push_back(obj->slave);
        _brport_slave_list[obj->master_oid] = l;
    }else{
        it->second.push_back(obj->slave);
    }

    return true;
}


bool ndi_brport_slave_cache::remove_brport_slave(ndi_brport_slave_obj_t * obj){
    if (obj == nullptr) return false;
    std_rw_lock_write_guard lg(&rw_lock);
    auto it = _brport_slave_list.find(obj->master_oid);
    if(it == _brport_slave_list.end()){
        return false;
    }

    for(auto slave_it = it->second.begin() ; slave_it != it->second.end(); ++slave_it){
        if(slave_it->slave_oid == obj->slave.slave_oid){
            it->second.erase(slave_it);
            if(it->second.size() == 0){
                _brport_slave_list.erase(it);
            }
            return true;
        }
    }

    return false;

}

bool ndi_brport_slave_cache::get_brport_slave_list(sai_object_id_t oid, brport_slave_list & list){

    std_rw_lock_read_guard lg(&rw_lock);
    auto it = _brport_slave_list.find(oid);
    if(it == _brport_slave_list.end()){
        return false;
    }

    list = it->second;
    return true;

}

void ndi_brport_slave_cache::brport_slave_callback(sai_object_id_t oid, brport_slave_fn fn){

    std_rw_lock_read_guard lg(&rw_lock);
    auto it = _brport_slave_list.find(oid);
    if(it == _brport_slave_list.end()){
        return ;
    }

    for(auto slave_it : it->second){
        fn(slave_it);
    }

}


bool nas_ndi_add_brport_slave(ndi_brport_slave_obj_t * obj){
    return _ndi_brport_slave_cache->add_brport_slave(obj);
}


bool nas_ndi_remove_brport_slave(ndi_brport_slave_obj_t * obj){
    return _ndi_brport_slave_cache->remove_brport_slave(obj);
}


bool nas_ndi_get_bridge_port_slave_list(sai_object_id_t brport_id, brport_slave_list & list){
    return _ndi_brport_slave_cache->get_brport_slave_list(brport_id,list);
}


void nas_ndi_bridge_port_slave_callback(sai_object_id_t brport_id, brport_slave_fn fn){
    _ndi_brport_slave_cache->brport_slave_callback(brport_id, fn);
}

