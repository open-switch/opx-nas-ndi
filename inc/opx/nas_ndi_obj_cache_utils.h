/*
 * Copyright (c) 2017 Dell Inc.
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

#ifndef NAS_NDI_INC_NAS_NDI_OBJ_CACHE_UTILS_H_
#define NAS_NDI_INC_NAS_NDI_OBJ_CACHE_UTILS_H_

#include "saitypes.h"
#include <list>
#include <functional>
#include "nas_ndi_obj_cache.h"

using brport_list = std::list<sai_object_id_t>;

using brport_slave_list = std::list<ndi_brport_slave_t>;

using brport_slave_fn = std::function< void (ndi_brport_slave_t) >;

bool nas_ndi_get_bridge_port_obj_list(sai_object_id_t port_id,brport_list & list );

bool nas_ndi_get_bridge_port_slave_list(sai_object_id_t brport_id, brport_slave_list & list);

void nas_ndi_bridge_port_slave_callback(sai_object_id_t brport_id, brport_slave_fn fn);

#endif /* NAS_NDI_INC_NAS_NDI_OBJ_CACHE_UTILS_H_ */
