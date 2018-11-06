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
 * nas_ndi_tunnel_map.h
 */


#ifndef _NAS_NDI_TUNNEL_MAP_H
#define _NAS_NDI_TUNNEL_MAP_H

#include "stdint.h"
#include "nas_ndi_tunnel_obj.h"
#include "sai.h"
#include "saitypes.h"
#include "saitunnel.h"
#include "nas_ndi_int.h"
#include "ds_common_types.h"
#include "std_error_codes.h"

#include <map>
#include <string>
#include <stdlib.h>



void print_tunnel_map();
TunnelObj *get_tunnel_obj(const hal_ip_addr_t *src_ip, const hal_ip_addr_t *loc_ip);
bool insert_tunnel_obj(const hal_ip_addr_t *src_ip, const hal_ip_addr_t *loc_ip, TunnelObj *tunnel_obj);
bool remove_tunnel_obj(const hal_ip_addr_t *src_ip, const hal_ip_addr_t *loc_ip);
bool has_tunnel_obj(const hal_ip_addr_t *src_ip, const hal_ip_addr_t *loc_ip);

#endif /* _NAS_NDI_TUNNEL_MAP_H */
