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

/*
 * filename: nas_ndi_fc_init.c
 */

#include "sai.h"
#include "saifcport.h"
#include "saifcswitch.h"
#include "nas_ndi_fc_init.h"
#include "nas_ndi_event_logs.h"
#include "std_error_codes.h"
#include "std_assert.h"
#include "ds_common_types.h"
#include <unistd.h>
#include <inttypes.h>
#include <nas_switch.h>

static ndi_sai_fc_api_tbl_t g_ndi_fc_api_tbl;
static sai_object_id_t switch_oid;

static inline ndi_sai_fc_api_tbl_t *get_fc_api_tbl_ptr(void)
{
    return &g_ndi_fc_api_tbl;
}
sai_fc_switch_api_t *ndi_get_fc_switch_api(void) {
    return g_ndi_fc_api_tbl.n_sai_fc_switch_api;
}

sai_fc_port_api_t *ndi_get_fc_port_api(void) {
    return g_ndi_fc_api_tbl.n_sai_fc_port_api;
}

t_std_error ndi_sai_fc_apis_init(void)
{
    ndi_sai_fc_api_tbl_t *fc_api_tbl_ptr = NULL;;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    if (!nas_switch_get_fc_supported()) {
        NDI_INIT_LOG_TRACE("FC not supported");
        return STD_ERR_OK;
    }
    fc_api_tbl_ptr =  get_fc_api_tbl_ptr();
    do {
        sai_ret = sai_api_query(SAI_API_FC_SWITCH, (void *)&(fc_api_tbl_ptr->n_sai_fc_switch_api));
        if (sai_ret != SAI_STATUS_SUCCESS) {
            break;
        }
        sai_ret = sai_api_query(SAI_API_FC_PORT, (void *)&(fc_api_tbl_ptr->n_sai_fc_port_api));
        if (sai_ret != SAI_STATUS_SUCCESS) {
            break;
        }

    } while(0);
    if (sai_ret != SAI_STATUS_SUCCESS) {
       return (STD_ERR(NPU, CFG, sai_ret));
    }
    nas_fc_lock_init();
    return STD_ERR_OK;
}

void  ndi_sai_fc_port_state_change_cb (uint32_t count, sai_fc_port_oper_status_notification_t *data) {
    NDI_INIT_LOG_TRACE( " received port state event notification");
}

t_std_error ndi_sai_fc_switch_init(void) {

    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    sai_attribute_t sai_attr;

    /*  init FC switch */
    sai_attr.id =  SAI_FC_SWITCH_ATTR_INIT_SWITCH;
    sai_attr.value.booldata = true;

    if (!nas_switch_get_fc_supported()) {
        NDI_INIT_LOG_TRACE("FC not supported");
        return STD_ERR_OK;
    }
    if ((sai_ret = ndi_get_fc_switch_api()->create_fc_switch(&switch_oid, 1, &sai_attr)) != SAI_STATUS_SUCCESS) {
        NDI_INIT_LOG_ERROR("SAI FC switch init failure");
        return (STD_ERR(NPU, CFG, sai_ret));
    }

    /*  Set port state event call back */
    sai_attr.id =  SAI_FC_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY;
    //sai_attr.value.u32 = (sai_uint32_t)ndi_sai_fc_port_state_change_cb; // TODO check the type of function

    if ((sai_ret = ndi_get_fc_switch_api()->set_fc_switch_attribute(switch_oid,  &sai_attr)) != SAI_STATUS_SUCCESS) {
        NDI_INIT_LOG_ERROR("SAI FC switch init failure");
        return (STD_ERR(NPU, CFG, sai_ret));
    }

    return STD_ERR_OK;
    // TODO Add switch and port event notification function
}


