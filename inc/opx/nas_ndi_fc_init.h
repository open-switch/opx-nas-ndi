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
 * filename: nas_ndi_fc_init.h
 */

#ifndef _NAS_NDI_FC_INIT_H_
#define _NAS_NDI_FC_INIT_H_

#include "sai.h"
#include "saifcport.h"
#include "saifcswitch.h"
#include "saitypes.h"
#include "nas_ndi_int.h"
#include "ds_common_types.h"
#include "std_error_codes.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef struct _ndi_sai_fc_api_tbl_t
{
    sai_fc_switch_api_t                *n_sai_fc_switch_api;
    sai_fc_port_api_t                *n_sai_fc_port_api;
 } ndi_sai_fc_api_tbl_t;


t_std_error ndi_sai_fc_apis_init(void);
void nas_fc_lock_init();
t_std_error ndi_sai_fc_switch_init(nas_ndi_db_t *ndi_db_ptr);
sai_fc_switch_api_t *ndi_get_fc_switch_api(void);
sai_fc_port_api_t *ndi_get_fc_port_api(void);
t_std_error ndi_sai_fcport_id_get (npu_id_t npu_id, port_t port,
                                     sai_object_id_t *sai_port);


#ifdef __cplusplus
}
#endif
#endif /* _NAS_NDI_FC_INIT_H_ */
