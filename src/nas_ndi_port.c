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

/*
 * filename: nas_ndi_port.c
 */

#include "std_error_codes.h"
#include "std_assert.h"
#include "ds_common_types.h"
#include "dell-base-platform-common.h"

#include "nas_ndi_event_logs.h"
#include "nas_ndi_int.h"
#include "nas_ndi_utils.h"
#include "nas_ndi_port.h"
#include "nas_ndi_port_utils.h"
#include "sai.h"
#include "saiport.h"
#include "saistatus.h"
#include "saitypes.h"
#include "nas_ndi_vlan.h"
#include "nas_ndi_stg_util.h"
#include "nas_ndi_bridge_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>


/*  NDI Port specific APIs  */

static inline  sai_port_api_t *ndi_sai_port_api_tbl_get(nas_ndi_db_t *ndi_db_ptr)
{
    return(ndi_db_ptr->ndi_sai_api_tbl.n_sai_port_api_tbl);
}

typedef enum  {
    SAI_SG_ACT_SET,
    SAI_SG_ACT_GET
} SAI_SET_OR_GET_ACTION_t;

static t_std_error _sai_port_attr_set_or_get(npu_id_t npu, port_t port, SAI_SET_OR_GET_ACTION_t set,
        sai_attribute_t *attr, size_t count) {
    STD_ASSERT(attr != NULL);

    if (count==0 || ((set==SAI_SG_ACT_SET) && (count >1))) {
        return STD_ERR(NPU, PARAM, 0);
    }

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu);
    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_object_id_t sai_port;
    t_std_error ret_code = STD_ERR_OK;

    if ((ret_code = ndi_sai_port_id_get(npu, port, &sai_port)) != STD_ERR_OK) {
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_status_t sai_ret = SAI_STATUS_SUCCESS;
    if (set==SAI_SG_ACT_SET) {
        sai_ret = ndi_sai_port_api_tbl_get(ndi_db_ptr)->set_port_attribute(sai_port,attr);
        if (sai_ret != SAI_STATUS_SUCCESS) {
            NDI_PORT_LOG_ERROR("Error in setting attr %d on npu %d and port %d",attr->id,npu,port);
        } else {
            NDI_PORT_LOG_TRACE("Successful in setting attr %d on npu %d and port %d",attr->id,npu,port);
        }
    } else {
        sai_ret = ndi_sai_port_api_tbl_get(ndi_db_ptr)->get_port_attribute(sai_port, count, attr);
    }

    if (sai_ret == SAI_STATUS_SUCCESS) {
        return STD_ERR_OK;
    } else if (sai_ret == SAI_STATUS_NOT_SUPPORTED) {
        return STD_ERR(NPU, NOTSUPPORTED, 0);
    }

    return STD_ERR(NPU, CFG, sai_ret);
}

t_std_error ndi_port_oper_state_notify_register(ndi_port_oper_status_change_fn reg_fn)
{
    t_std_error ret_code = STD_ERR_OK;
    npu_id_t npu_id = ndi_npu_id_get();
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        return STD_ERR(NPU, PARAM, 0);
    }
    STD_ASSERT(reg_fn != NULL);

    ndi_db_ptr->switch_notification->port_oper_status_change_cb = reg_fn;

    return ret_code;
}

/*  public function for getting list of breakout mode supported. */
t_std_error ndi_port_supported_breakout_mode_get(npu_id_t npu_id, npu_port_t ndi_port,
        int *mode_count, BASE_IF_PHY_BREAKOUT_MODE_t *mode_list) {

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_SUPPORTED_BREAKOUT_MODE_TYPE;
    sai_attr.value.s32list.count = SAI_PORT_BREAKOUT_MODE_TYPE_MAX;
    int32_t modes[SAI_PORT_BREAKOUT_MODE_TYPE_MAX];
    sai_attr.value.s32list.list = modes;
    t_std_error rc;
    if ((rc = _sai_port_attr_set_or_get(npu_id,ndi_port,SAI_SG_ACT_GET,&sai_attr,1))!=STD_ERR_OK) {
        return rc;
    }
    size_t ix = 0;
    size_t mx = (*mode_count > sai_attr.value.s32list.count) ?
            sai_attr.value.s32list.count : *mode_count;
    for ( ; ix < mx ; ++ix ) {
        mode_list[ix] = sai_break_to_ndi_break(sai_attr.value.s32list.list[ix]);
    }
    *mode_count = mx;
    return(STD_ERR_OK);
}

t_std_error ndi_port_admin_state_set(npu_id_t npu_id, npu_port_t port_id,
        bool admin_state) {

    sai_attribute_t sai_attr;
    sai_attr.value.booldata = admin_state;
    sai_attr.id = SAI_PORT_ATTR_ADMIN_STATE;

    EV_LOGGING(NDI,DEBUG,"PORT-STAT","Port admin state for npu %d, port %d, value %d \n", npu_id, port_id, admin_state);
    return _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1);
}

t_std_error ndi_port_admin_state_get(npu_id_t npu_id, npu_port_t port_id, IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_t *admin_state)
{
    STD_ASSERT(admin_state != NULL);

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_ADMIN_STATE;
    t_std_error rc = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1);
    if (rc==STD_ERR_OK) {
        *admin_state  = (sai_attr.value.booldata == true) ? IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_UP : IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_DOWN;
    }
    return rc;
}

t_std_error ndi_port_link_state_get(npu_id_t npu_id, npu_port_t port_id,
        ndi_intf_link_state_t *link_state)
{
    STD_ASSERT(link_state != NULL);

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_OPER_STATUS;
    t_std_error ret_code = _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_GET, &sai_attr, 1);
    if (ret_code != STD_ERR_OK) {
        return ret_code;
    }

    sai_port_oper_status_t sai_port_status = (sai_port_oper_status_t) sai_attr.value.s32;
    ret_code = ndi_sai_oper_state_to_link_state_get(sai_port_status, &(link_state->oper_status));

    return ret_code;
}

t_std_error ndi_sai_oper_state_to_link_state_get(sai_port_oper_status_t sai_port_state,
                       ndi_port_oper_status_t *p_state)
{

    switch(sai_port_state) {
    case SAI_PORT_OPER_STATUS_UNKNOWN:
        *p_state = ndi_port_OPER_FAIL;
            NDI_INIT_LOG_TRACE(" port state is unknown\n");
        break;
    case SAI_PORT_OPER_STATUS_UP:
        *p_state = ndi_port_OPER_UP;
        break;
    case SAI_PORT_OPER_STATUS_DOWN:
        *p_state = ndi_port_OPER_DOWN;
        break;
    case SAI_PORT_OPER_STATUS_TESTING:
        *p_state = ndi_port_OPER_TESTING;
        NDI_INIT_LOG_TRACE(" port is under test\n");
        break;
    case SAI_PORT_OPER_STATUS_NOT_PRESENT:
        *p_state = ndi_port_OPER_FAIL;
        break;
    default:
        NDI_INIT_LOG_TRACE(" unknown port state is return\n");
        return (STD_ERR(NPU,FAIL,0));
    }

    return STD_ERR_OK;
}

t_std_error ndi_port_breakout_mode_get(npu_id_t npu, npu_port_t port,
        BASE_IF_PHY_BREAKOUT_MODE_t *mode) {

    sai_attribute_t sai_attr;
    memset(&sai_attr,0,sizeof(sai_attr));
    sai_attr.id = SAI_PORT_ATTR_CURRENT_BREAKOUT_MODE_TYPE;

    t_std_error rc ;
    if ((rc=_sai_port_attr_set_or_get(npu,port,SAI_SG_ACT_GET,&sai_attr,1))==STD_ERR_OK) {
        *mode = sai_break_to_ndi_break(sai_attr.value.s32);
        return STD_ERR_OK;
    }
    return rc;
}

/*  public function for getting list of supported speed. */
t_std_error ndi_port_supported_speed_get(npu_id_t npu_id, npu_port_t ndi_port,
        size_t *speed_count, BASE_IF_SPEED_t *speed_list) {

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_SUPPORTED_SPEED;
    sai_attr.value.s32list.count = NDI_PORT_SUPPORTED_SPEED_MAX;
    int32_t speeds[NDI_PORT_SUPPORTED_SPEED_MAX];
    sai_attr.value.s32list.list = speeds;
    t_std_error rc;
    if ((rc = _sai_port_attr_set_or_get(npu_id,ndi_port,SAI_SG_ACT_GET,&sai_attr,1))!=STD_ERR_OK) {
        return rc;
    }
    size_t ix = 0;
    size_t mx = (*speed_count > sai_attr.value.s32list.count) ?
            sai_attr.value.s32list.count : *speed_count;
    BASE_IF_SPEED_t *speed = speed_list;
    size_t count = 0;
    for ( ; ix < mx ; ++ix ) {
        if (!ndi_port_get_ndi_speed(sai_attr.value.s32list.list[ix], speed)) {
            NDI_PORT_LOG_TRACE("unsupported Speed  returned from SAI%d", sai_attr.value.s32list.list[ix]);
            continue;
        }
        count++;
        speed++;
    }
    *speed_count = count;
    return(STD_ERR_OK);
}
t_std_error ndi_port_speed_set(npu_id_t npu_id, npu_port_t port_id, BASE_IF_SPEED_t speed) {
    t_std_error ret_code = STD_ERR_OK;
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_SPEED;
    sai_attribute_t sai_adv_speed_attr;
    sai_adv_speed_attr.id = SAI_PORT_ATTR_ADVERTISED_SPEED;
    if (speed == BASE_IF_SPEED_AUTO)  {
        /*  speed==AUTO is not supported at BASE level
         *  TODO just return ok for the time being until it is supported at application layer
         */
        NDI_PORT_LOG_TRACE("Speed AUTO is not supported at BASE level");
        return STD_ERR_OK;
    }
    uint32_t speed_val;
    if (!ndi_port_get_sai_speed(speed, &speed_val)){
        NDI_PORT_LOG_ERROR("unsupported Speed %d", (uint32_t)speed);
        return STD_ERR(NPU, PARAM, 0);
    }
    sai_attr.value.u32 = speed_val;
    sai_adv_speed_attr.value.u32list.count = 1;
    sai_adv_speed_attr.value.u32list.list = &speed_val;
    NDI_PORT_LOG_TRACE("Setting %d speed on npu %d and port %d", (uint32_t)speed,npu_id,port_id);
    if((ret_code = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_adv_speed_attr,1))!=STD_ERR_OK){
        NDI_PORT_LOG_TRACE("Speed advertisement set error for sai attribute %d", ret_code);
    }
    return _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1);
}

t_std_error ndi_port_mtu_get(npu_id_t npu_id, npu_port_t port_id, uint_t *mtu) {
    STD_ASSERT(mtu!=NULL);

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_MTU;

    t_std_error rc = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1);
    if (rc==STD_ERR_OK) {
        *mtu = sai_attr.value.u32;
    }
    return rc;
}

t_std_error ndi_port_mtu_set(npu_id_t npu_id, npu_port_t port_id, uint_t mtu) {
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_MTU;
    sai_attr.value.u32 = mtu;

    return _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1);
}

t_std_error ndi_port_eee_set(npu_id_t npu_id, npu_port_t port_id, uint_t state)
{
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_EEE_ENABLE;
    sai_attr.value.booldata = state;

    return _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1);
}

t_std_error ndi_port_eee_get_wake_time (npu_id_t npu_id, npu_port_t port_id,
                                        uint16_t *wake_time)
{
    sai_attribute_t sai_attr;
    t_std_error     ret_code;

    sai_attr.id = SAI_PORT_ATTR_EEE_WAKE_TIME;

    ret_code =  _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_GET,
                                          &sai_attr, 1);

    *wake_time = sai_attr.value.u16;

    return ret_code;
}

t_std_error ndi_port_eee_get_idle_time (npu_id_t npu_id, npu_port_t port_id,
                                        uint16_t *idle_time)
{
    sai_attribute_t sai_attr;
    t_std_error     ret_code;

    sai_attr.id = SAI_PORT_ATTR_EEE_IDLE_TIME;

    ret_code =  _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_GET,
                                          &sai_attr, 1);

    *idle_time = sai_attr.value.u16;

    return ret_code;
}

t_std_error ndi_port_eee_get(npu_id_t npu_id, npu_port_t port_id, uint_t *state)
{
    sai_attribute_t sai_attr;
    t_std_error     ret_code;

    sai_attr.id = SAI_PORT_ATTR_EEE_ENABLE;

    ret_code =  _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_GET,
                                          &sai_attr, 1);

    *state = sai_attr.value.booldata;

    return ret_code;
}

t_std_error ndi_port_clear_eee_stats (npu_id_t npu_id, npu_port_t port_id)
{
    sai_object_id_t sai_port;
    sai_port_stat_t sai_port_stats_id;

    t_std_error ret_code = STD_ERR_OK;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_PORT_LOG_TRACE("Invalid NPU Id %d", npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    if ((ret_code = ndi_sai_port_id_get(npu_id, port_id, &sai_port)) != STD_ERR_OK) {
        return ret_code;
    }

    sai_port_stats_id = SAI_PORT_STAT_EEE_TX_EVENT_COUNT;

    if ((sai_ret = ndi_sai_port_api_tbl_get(ndi_db_ptr)->clear_port_stats(sai_port,1,
                   &sai_port_stats_id))
                   != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_TRACE("Port stats Get failed for npu %d, port %d, ret %d \n", npu_id, port_id, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return ret_code;
}

t_std_error ndi_port_loopback_get(npu_id_t npu_id, npu_port_t port_id, BASE_CMN_LOOPBACK_TYPE_t *loopback) {
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE;

    t_std_error rc =  _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1);
    if (rc==STD_ERR_OK) {
        *loopback = ndi_port_get_ndi_loopback_mode(sai_attr.value.s32);
    }
    return rc;
}

t_std_error ndi_port_loopback_set(npu_id_t npu_id, npu_port_t port_id, BASE_CMN_LOOPBACK_TYPE_t loopback) {
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE;
    sai_attr.value.s32 = ndi_port_get_sai_loopback_mode(loopback);

    return _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1);
}

static inline sai_port_media_type_t ndi_sai_port_media_type_translate (PLATFORM_MEDIA_TYPE_t hal_media_type)
{
    sai_port_media_type_t sal_media_type;
    switch (hal_media_type) {

        case PLATFORM_MEDIA_CABLE_TYPE_UNKNOWN:
           sal_media_type = SAI_PORT_MEDIA_TYPE_UNKNOWN;
            break;

        case PLATFORM_MEDIA_CABLE_TYPE_AOC:
        case PLATFORM_MEDIA_CABLE_TYPE_ACC:
        case PLATFORM_MEDIA_CABLE_TYPE_FIBER:
            sal_media_type = SAI_PORT_MEDIA_TYPE_FIBER;
            break;

        case PLATFORM_MEDIA_CABLE_TYPE_RJ45:
        case PLATFORM_MEDIA_CABLE_TYPE_DAC:
            sal_media_type = SAI_PORT_MEDIA_TYPE_COPPER;
            break;

        default:
            NDI_PORT_LOG_ERROR("media type is not recognized %d \n", hal_media_type);
            sal_media_type = SAI_PORT_MEDIA_TYPE_NOT_PRESENT;
    }

    return (sal_media_type);
}

t_std_error ndi_port_media_type_set(npu_id_t npu_id, npu_port_t port_id, PLATFORM_MEDIA_TYPE_t media)
{
    sai_attribute_t sai_attr;
    sai_attr.value.s32 = ndi_sai_port_media_type_translate(media);
    sai_attr.id = SAI_PORT_ATTR_MEDIA_TYPE;

    NDI_PORT_LOG_TRACE("Setting media type %d on npu %d and port %d", sai_attr.value.s32,npu_id,port_id);
    t_std_error ret_code = STD_ERR_OK;
    if ((ret_code = _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_SET, &sai_attr, 1))
                         != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("Setting media type %d on npu %d and port %d failed", sai_attr.value.s32,npu_id,port_id);
    }

    return STD_ERR_OK;
}

t_std_error ndi_port_identification_led_set (npu_id_t npu_id, npu_port_t port_id, bool state)
{
    sai_attribute_t sai_attr;
    sai_attr.value.booldata = state;
    sai_attr.id = SAI_PORT_ATTR_LOCATION_LED;

    return _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_SET, &sai_attr, 1);
}

t_std_error ndi_port_hw_profile_set (npu_id_t npu_id, npu_port_t port_id, uint64_t hw_profile)
{
    t_std_error rc = STD_ERR_OK;
    sai_attribute_t sai_attr;
    sai_attr.value.u64 = hw_profile;
    sai_attr.id = SAI_PORT_ATTR_HW_PROFILE_ID;

    NDI_PORT_LOG_TRACE("Port hw_profile for npu %d, port %d, value %lu \n", npu_id, port_id, hw_profile);

    /* don't return error incase of failure, we want to proceed futher with other configurations */
    rc = _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_SET, &sai_attr,1);
    if (rc != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("Error in setting hw_profile for npu %d, port %d, value %lu \n", npu_id, port_id, hw_profile);
    }

    return STD_ERR_OK;
}

static t_std_error ndi_port_speed_get_int(npu_id_t npu_id, npu_port_t port_id,
                                          BASE_IF_SPEED_t *speed,
                                          bool check_link)
{
    t_std_error rc = STD_ERR_OK;
    STD_ASSERT(speed!=NULL);

    if (check_link) {
        /*  in case if link is not UP then return speed = 0 Mbps */
        ndi_intf_link_state_t link_state;
        rc = ndi_port_link_state_get(npu_id, port_id, &link_state);
        if ((rc != STD_ERR_OK) || /* unable to read link state */
            ((rc == STD_ERR_OK) && (link_state.oper_status != ndi_port_OPER_UP))) {
            /*  Link is not UP */
            *speed = BASE_IF_SPEED_0MBPS;
            return rc;
        }
    }

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_SPEED;

    rc = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1);
    if (rc==STD_ERR_OK) {
        if (!ndi_port_get_ndi_speed((uint32_t)sai_attr.value.u32, speed)) {
            return STD_ERR(NPU, PARAM, 0);
        }
    }

    return rc;
}

t_std_error ndi_port_speed_get(npu_id_t npu_id, npu_port_t port_id, BASE_IF_SPEED_t *speed)
{
    return ndi_port_speed_get_int(npu_id, port_id, speed, true);
}

t_std_error ndi_port_speed_get_nocheck(npu_id_t npu_id, npu_port_t port_id, BASE_IF_SPEED_t *speed)
{
    return ndi_port_speed_get_int(npu_id, port_id, speed, false);
}

t_std_error ndi_port_stats_get(npu_id_t npu_id, npu_port_t port_id,
                               ndi_stat_id_t *ndi_stat_ids,
                               uint64_t* stats_val, size_t len)
{
    sai_object_id_t sai_port;
    const unsigned int list_len = len;
    sai_port_stat_t sai_port_stats_ids[list_len];

    t_std_error ret_code = STD_ERR_OK;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_PORT_LOG_ERROR("Invalid NPU Id %d", npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    if ((ret_code = ndi_sai_port_id_get(npu_id, port_id, &sai_port)) != STD_ERR_OK) {
        return ret_code;
    }
    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_if_stats(ndi_stat_ids[ix],&sai_port_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_port_api_tbl_get(ndi_db_ptr)->get_port_stats(sai_port, len,
                   sai_port_stats_ids, stats_val))
                   != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_TRACE("Port stats Get failed for npu %d, port %d, ret %d \n",
                            npu_id, port_id, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return ret_code;
}


t_std_error ndi_port_stats_clear(npu_id_t npu_id, npu_port_t port_id,
                               ndi_stat_id_t *ndi_stats_counter_ids,
                               size_t len){

    sai_object_id_t sai_port;
    const unsigned int list_len = len;
    sai_port_stat_t sai_port_stats_ids[list_len];

    t_std_error ret_code = STD_ERR_OK;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_PORT_LOG_TRACE("Invalid NPU Id %d", npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    if ((ret_code = ndi_sai_port_id_get(npu_id, port_id, &sai_port)) != STD_ERR_OK) {
        return ret_code;
    }

    size_t ix = 0;
    for ( ; ix < len ; ++ix){
        if(!ndi_to_sai_if_stats(ndi_stats_counter_ids[ix],&sai_port_stats_ids[ix])){
            return STD_ERR(NPU,PARAM,0);
        }
    }

    if ((sai_ret = ndi_sai_port_api_tbl_get(ndi_db_ptr)->clear_port_stats(sai_port, len,
                   sai_port_stats_ids))
                   != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_TRACE("Port stats Get failed for npu %d, port %d, ret %d \n", npu_id, port_id, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return ret_code;

}

t_std_error ndi_port_set_untagged_port_attrib(npu_id_t npu_id,
                                              npu_port_t port_id,
                                              BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_t mode) {
    sai_attribute_t cur_modes[2];
    cur_modes[0].id = SAI_PORT_ATTR_DROP_TAGGED;
    cur_modes[1].id = SAI_PORT_ATTR_DROP_UNTAGGED;

    sai_attribute_t targ_modes[2];
    t_std_error rc= _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,cur_modes,2);
    if (rc!=STD_ERR_OK) {
        return rc;
    }
    memcpy(targ_modes,cur_modes,sizeof(cur_modes));
    targ_modes[0].value.booldata = !(mode == BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_HYBRID ||
            mode == BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_TAGGED );

    targ_modes[1].value.booldata = !(mode == BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_HYBRID ||
            mode == BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_UNTAGGED );

    rc= _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&targ_modes[0],1);
    if (rc!=STD_ERR_OK) {
        return rc;
    }

    rc= _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&targ_modes[1],1);
    if (rc!=STD_ERR_OK) {
        _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&cur_modes[0],1);
        return rc;
    }
    return rc;
}

t_std_error ndi_port_get_untagged_port_attrib(npu_id_t npu_id,
                                              npu_port_t port_id,
                                              BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_t *mode) {
    sai_attribute_t cur_modes[2];
    cur_modes[0].id = SAI_PORT_ATTR_DROP_TAGGED;
    cur_modes[1].id = SAI_PORT_ATTR_DROP_UNTAGGED;

    t_std_error rc= _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,cur_modes,2);
    if (rc!=STD_ERR_OK) {
        return rc;
    }
    if (cur_modes[0].value.booldata == false && cur_modes[1].value.booldata == false) {
        *mode = BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_HYBRID;
    }
    //if drop tagged and not untagged
    if (cur_modes[0].value.booldata == true && cur_modes[1].value.booldata == false) {
        *mode = BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_UNTAGGED;
    }

    if (cur_modes[0].value.booldata == false && cur_modes[1].value.booldata == true) {
        *mode = BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_TAGGED;
    }

    return STD_ERR_OK;
}

t_std_error ndi_set_port_vid(npu_id_t npu_id, npu_port_t port_id,
                             hal_vlan_id_t vlan_id)
{
    sai_attribute_t sai_attr;
    sai_attr.value.u16 = (sai_vlan_id_t)vlan_id;
    sai_attr.id = SAI_PORT_ATTR_PORT_VLAN_ID;

    return _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_SET, &sai_attr, 1);
}

t_std_error ndi_port_mac_learn_mode_set(npu_id_t npu_id, npu_port_t port_id,
                                        BASE_IF_PHY_MAC_LEARN_MODE_t mode){
    sai_attribute_t sai_attr;
    sai_attr.value.u32 = (sai_bridge_port_fdb_learning_mode_t )ndi_port_get_sai_mac_learn_mode(mode);
    sai_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
    sai_object_id_t sai_port;

    if (ndi_sai_port_id_get(npu_id, port_id, &sai_port) != STD_ERR_OK) {
        return STD_ERR(NPU, PARAM, 0);
    }

    return ndi_brport_attr_set_or_get_1Q(npu_id, sai_port, true, &sai_attr);
}

t_std_error ndi_port_mac_learn_mode_get(npu_id_t npu_id, npu_port_t port_id,
                                        BASE_IF_PHY_MAC_LEARN_MODE_t * mode){
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
    sai_object_id_t sai_port;

    t_std_error rc;

    if (ndi_sai_port_id_get(npu_id, port_id, &sai_port) != STD_ERR_OK) {
        return STD_ERR(NPU, PARAM, 0);
    }

    if((rc = ndi_brport_attr_set_or_get_1Q(npu_id,sai_port, false,&sai_attr)) != STD_ERR_OK){
        return rc;
    }

    *mode = (BASE_IF_PHY_MAC_LEARN_MODE_t)ndi_port_get_mac_learn_mode(sai_attr.value.u32);
    return STD_ERR_OK;
}


t_std_error ndi_port_clear_all_stat(npu_id_t npu_id, npu_port_t port_id){
    sai_object_id_t sai_port;
    sai_status_t sai_ret = SAI_STATUS_FAILURE;
    t_std_error rc;
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);

    if (ndi_db_ptr == NULL) {
        NDI_PORT_LOG_ERROR("Invalid NPU Id %d", npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    if ((rc = ndi_sai_port_id_get(npu_id, port_id, &sai_port)) != STD_ERR_OK) {
        NDI_PORT_LOG_TRACE("Failed to convert  npu %d and port %d to sai port",
                            npu_id, port_id);
        return rc;
    }

    if ((sai_ret = ndi_sai_port_api_tbl_get(ndi_db_ptr)->clear_port_all_stats(sai_port))
                   != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_TRACE("Port stats clear failed for npu %d, port %d, ret %d ",
                            npu_id, port_id, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    return STD_ERR_OK;
}


t_std_error ndi_port_set_ingress_filtering(npu_id_t npu_id, npu_port_t port_id, bool ing_filter) {
    sai_attribute_t sai_attr;
    sai_attr.value.booldata = ing_filter;
    sai_attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;

    sai_object_id_t sai_port;

    if (ndi_sai_port_id_get(npu_id, port_id, &sai_port) != STD_ERR_OK) {
        return STD_ERR(NPU, PARAM, 0);
    }

    return ndi_brport_attr_set_or_get_1Q(npu_id, sai_port, true, &sai_attr);
}

t_std_error ndi_port_auto_neg_set(npu_id_t npu_id, npu_port_t port_id,
        bool enable) {

    sai_attribute_t sai_attr;

    sai_attr.value.booldata = enable;
    sai_attr.id = SAI_PORT_ATTR_AUTO_NEG_MODE;

    NDI_PORT_LOG_TRACE("Setting autoneg %d on npu %d and port %d",
                sai_attr.value.booldata, npu_id, port_id);
    return _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1);
}

t_std_error ndi_port_auto_neg_get(npu_id_t npu_id, npu_port_t port_id, bool *enable)
{
    STD_ASSERT(enable != NULL);

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_AUTO_NEG_MODE;
    t_std_error rc = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1);
    if (rc==STD_ERR_OK) {
        *enable  = sai_attr.value.booldata;
    }
    return rc;
}
t_std_error ndi_port_duplex_set(npu_id_t npu_id, npu_port_t port_id,
        BASE_CMN_DUPLEX_TYPE_t duplex) {

    sai_attribute_t sai_attr;
   /* TODO in case of AUTO set it to FULL: TBD */
    sai_attr.value.booldata = (duplex == BASE_CMN_DUPLEX_TYPE_HALF) ? false : true;
    sai_attr.id = SAI_PORT_ATTR_FULL_DUPLEX_MODE;

    return _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1);
}

t_std_error ndi_port_duplex_get(npu_id_t npu_id, npu_port_t port_id,  BASE_CMN_DUPLEX_TYPE_t *duplex)
{
    STD_ASSERT(duplex != NULL);

    sai_attribute_t sai_attr;

    sai_attr.id = SAI_PORT_ATTR_FULL_DUPLEX_MODE;
    t_std_error rc = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1);
    if (rc==STD_ERR_OK) {
        *duplex  = (sai_attr.value.booldata == true) ?
                          BASE_CMN_DUPLEX_TYPE_FULL : BASE_CMN_DUPLEX_TYPE_HALF ;
    }
    return rc;
}

t_std_error ndi_port_fec_set(npu_id_t npu_id, npu_port_t port_id,
        BASE_CMN_FEC_TYPE_t fec_mode) {

    t_std_error ret_code = STD_ERR_OK;
    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_FEC_MODE;
    int32_t fec_val = (int32_t)ndi_port_get_sai_fec_mode(fec_mode);
    sai_attr.value.s32 = fec_val;
    sai_attribute_t sai_attr_fec_adv;
    sai_attr_fec_adv.value.s32list.count =1;
    sai_attr_fec_adv.value.s32list.list  = &fec_val;
    sai_attr_fec_adv.id = SAI_PORT_ATTR_ADVERTISED_FEC_MODE;
    NDI_PORT_LOG_TRACE("Setting FEC attribute %d to npu %d and port %d",fec_mode,npu_id,port_id);

    if((ret_code = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1)) != STD_ERR_OK){
    /*
     *Tomahawk and Maverick A0 platform doesn't support CL108.
     *SAI returns error as not supported and we are overriding FEC
     *FEC value as CL74 and so hardcoding the values.
     *fec_mode = 5 (cl108) & fec_mode = 4
   */
        if((ret_code == STD_ERR(NPU,NOTSUPPORTED,0)) && (fec_mode == BASE_CMN_FEC_TYPE_CL108_RS)){
            fec_mode = BASE_CMN_FEC_TYPE_CL74_FC;
            int32_t fec_val = (int32_t)ndi_port_get_sai_fec_mode(fec_mode);
            sai_attr.value.s32 = fec_val;
            sai_attr_fec_adv.value.s32list.count =1;
            sai_attr_fec_adv.value.s32list.list  = &fec_val;
            if((ret_code = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1)) != STD_ERR_OK){
                NDI_PORT_LOG_ERROR("FEC set error for sai attribute %d", ret_code);
                return ret_code;
            }
        }
        NDI_PORT_LOG_ERROR("FEC set error for sai attribute %d", ret_code);
        return ret_code;
    }

    if((ret_code = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr_fec_adv,1)) != STD_ERR_OK){
        NDI_PORT_LOG_ERROR("FEC set error for sai attribute fec advertisement %d", ret_code);
        return ret_code;
    }
    return ret_code;
}

static bool ndi_port_support_100g(npu_id_t npu, npu_port_t port)
{
    size_t speed_count = NDI_PORT_SUPPORTED_SPEED_MAX;
    BASE_IF_SPEED_t speed_list[NDI_PORT_SUPPORTED_SPEED_MAX];
    if (ndi_port_supported_speed_get(npu, port, &speed_count, speed_list) != STD_ERR_OK) {
        return false;
    }
    size_t idx;
    for (idx = 0; idx < speed_count; idx ++) {
        if (speed_list[idx] == BASE_IF_SPEED_100GIGE) {
            return true;
        }
    }

    return false;
}

t_std_error ndi_port_fec_get(npu_id_t npu_id, npu_port_t port_id,  BASE_CMN_FEC_TYPE_t *fec_mode)
{
    if (fec_mode == NULL) {
        NDI_PORT_LOG_ERROR("NULL pointer is not allowed as input");
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_attribute_t sai_attr;

    sai_attr.id = SAI_PORT_ATTR_FEC_MODE;
    t_std_error rc = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1);
    if (rc==STD_ERR_OK) {
        *fec_mode  = ndi_port_get_fec_mode(sai_attr.value.u32,
                                           ndi_port_support_100g(npu_id, port_id));
    }
    return rc;
}

t_std_error ndi_port_supported_fec_get(npu_id_t npu_id, npu_port_t port_id,
        int *fec_count, BASE_CMN_FEC_TYPE_t *fec_list)
{
    if (fec_list == NULL) {
        NDI_PORT_LOG_ERROR("NULL pointer is not allowed as input");
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_SUPPORTED_FEC_MODE;
    sai_attr.value.s32list.count = NDI_PORT_SUPPORTED_FEC_MAX;
    int32_t modes[NDI_PORT_SUPPORTED_FEC_MAX];
    sai_attr.value.s32list.list = modes;
    t_std_error rc = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1);
    if (rc != STD_ERR_OK) {
        return rc;
    }

    bool supp_100g = ndi_port_support_100g(npu_id, port_id);
    size_t ix = 0;
    size_t mx = (*fec_count > sai_attr.value.s32list.count) ?
            sai_attr.value.s32list.count : *fec_count;
    for (ix = 0; ix < mx ; ++ix) {
        fec_list[ix] = ndi_port_get_fec_mode(sai_attr.value.u32list.list[ix], supp_100g);
    }
    *fec_count = mx;

    return STD_ERR_OK;
}

t_std_error ndi_port_oui_set(npu_id_t npu_id, npu_port_t port_id, uint32_t oui) {
    sai_attribute_t sai_attr;
    sai_attr.value.u32 = oui;
    sai_attr.id = SAI_PORT_ATTR_ADVERTISED_OUI_CODE;

    return _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_SET,&sai_attr,1);
}

t_std_error ndi_port_vlan_filter_get (npu_id_t npu_id, npu_port_t port_id,
                                      BASE_CMN_FILTER_TYPE_t *filter_type) {
    sai_attribute_t sai_ingress_filter_attr;
    sai_attribute_t sai_egress_filter_attr;
    sai_object_id_t sai_port;
    t_std_error rc;

    if (filter_type == NULL) {
        NDI_PORT_LOG_ERROR("NDI Port Vlan Filter Get : NULL pointer is not allowed as input");
        return STD_ERR(NPU, PARAM, 0);
    }

    if (ndi_sai_port_id_get(npu_id, port_id, &sai_port) != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("NDI Port Vlan Filter Get : No Such Interface with Index %d", port_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_ingress_filter_attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
    sai_egress_filter_attr.id = SAI_BRIDGE_PORT_ATTR_EGRESS_FILTERING;

    if ((rc = ndi_brport_attr_set_or_get_1Q (npu_id, sai_port, false,
                                             &sai_ingress_filter_attr)) != STD_ERR_OK) {
        NDI_PORT_LOG_TRACE ("NDI Port Vlan Filter Get : Failed to Get Ingress "
                           "VLAN filter Type - Error Code %d", rc);
        return rc;
    }

    if ((rc = ndi_brport_attr_set_or_get_1Q (npu_id, sai_port, false,
                                             &sai_egress_filter_attr)) != STD_ERR_OK) {
        NDI_PORT_LOG_TRACE ("NDI Port Vlan Filter Get : Failed to Get Egress "
                            "VLAN filter Type - Error Code %d", rc);
        return rc;
    }

    if(sai_ingress_filter_attr.value.booldata == false) {
        if(sai_egress_filter_attr.value.booldata == false)
            *filter_type =  BASE_CMN_FILTER_TYPE_DISABLE;
        else
            *filter_type =  BASE_CMN_FILTER_TYPE_EGRESS_ENABLE;
    }
    else {
        if(sai_egress_filter_attr.value.booldata == true)
            *filter_type =  BASE_CMN_FILTER_TYPE_ENABLE;
        else
            *filter_type =  BASE_CMN_FILTER_TYPE_INGRESS_ENABLE;
    }
    return STD_ERR_OK;
}

t_std_error ndi_port_vlan_filter_set (npu_id_t npu_id, npu_port_t port_id,
                                      BASE_CMN_FILTER_TYPE_t filter) {
    sai_attribute_t sai_ingress_filter_attr;
    sai_attribute_t sai_egress_filter_attr;
    sai_object_id_t sai_port;
    t_std_error rc;

    if (ndi_sai_port_id_get(npu_id, port_id, &sai_port) != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("NDI Port VLAN Filter Set : No Such Interface with Index %d", port_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_ingress_filter_attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
    sai_egress_filter_attr.id = SAI_BRIDGE_PORT_ATTR_EGRESS_FILTERING;

    switch (filter) {
        case BASE_CMN_FILTER_TYPE_DISABLE:
            sai_ingress_filter_attr.value.booldata = false;
            sai_egress_filter_attr.value.booldata = false;
            break;
        case BASE_CMN_FILTER_TYPE_ENABLE:
            sai_ingress_filter_attr.value.booldata = true;
            sai_egress_filter_attr.value.booldata = true;
            break;
        case BASE_CMN_FILTER_TYPE_INGRESS_ENABLE:
            sai_ingress_filter_attr.value.booldata = true;
            sai_egress_filter_attr.value.booldata = false;
            break;
        case BASE_CMN_FILTER_TYPE_EGRESS_ENABLE:
            sai_ingress_filter_attr.value.booldata = false;
            sai_egress_filter_attr.value.booldata = true;
            break;
        default:
            NDI_PORT_LOG_ERROR("NDI Port Vlan Filter Set : No Such Filter Type %d", filter);
            return STD_ERR(NPU, PARAM, 0);
    }

    if ((rc = ndi_brport_attr_set_or_get_1Q (npu_id, sai_port, true,
                                             &sai_ingress_filter_attr)) != STD_ERR_OK) {
        NDI_PORT_LOG_TRACE ("NDI Port Vlan Filter Set : Failed to Set Ingress "
                            "VLAN filter Type - Error Code %d", rc);
        return rc;
    }

    if ((rc = ndi_brport_attr_set_or_get_1Q (npu_id, sai_port, true,
                                             &sai_egress_filter_attr)) != STD_ERR_OK) {
        NDI_PORT_LOG_TRACE ("NDI Port Vlan Filter Set : Failed to Set Egress "
                            "VLAN filter Type - Error Code %d", rc);
        return rc;
    }

    return STD_ERR_OK;
}

t_std_error ndi_port_oui_get(npu_id_t npu_id, npu_port_t port_id, uint32_t *oui) {
    if (oui == NULL) {
        NDI_PORT_LOG_ERROR("NULL pointer is not allowed as input");
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_attribute_t sai_attr;
    sai_attr.id = SAI_PORT_ATTR_ADVERTISED_OUI_CODE;

    t_std_error rc;
    if((rc = _sai_port_attr_set_or_get(npu_id,port_id,SAI_SG_ACT_GET,&sai_attr,1)) != STD_ERR_OK){
        return rc;
    }

    *oui = sai_attr.value.u32;
    return STD_ERR_OK;
}

#define MAX_PORT_ATTR_NUM   10
t_std_error ndi_phy_port_create(npu_id_t npu_id, BASE_IF_SPEED_t speed,
                                uint32_t *hwport_list, size_t len,
                                npu_port_t *port_id_p)
{
    t_std_error rc = STD_ERR_OK;
    sai_attribute_t port_attr_list[MAX_PORT_ATTR_NUM];
    size_t port_attr_cnt = 0;

    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        NDI_PORT_LOG_ERROR("Invalid NPU Id %d", npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    uint32_t sai_speed;
    port_attr_list[port_attr_cnt].id = SAI_PORT_ATTR_SPEED;
    if (!ndi_port_get_sai_speed(speed, &sai_speed)) {
        NDI_PORT_LOG_ERROR("Invalid port speed %d", speed);
        return STD_ERR(NPU, FAIL, 0);
    }
    port_attr_list[port_attr_cnt].value.u32 = sai_speed;
    port_attr_cnt ++;

    port_attr_list[port_attr_cnt].id = SAI_PORT_ATTR_HW_LANE_LIST;
    port_attr_list[port_attr_cnt].value.u32list.count = len;
    port_attr_list[port_attr_cnt].value.u32list.list = hwport_list;
    port_attr_cnt ++;

    sai_object_id_t sai_port;
    sai_status_t sai_ret = ndi_sai_port_api_tbl_get(ndi_db_ptr)->
                            create_port(&sai_port,ndi_switch_id_get(),port_attr_cnt, port_attr_list);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_TRACE("Physical port create failed for npu %d, ret %d ",
                            npu_id, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    npu_port_t npu_port;
    rc = ndi_port_map_sai_port_add(npu_id, sai_port, hwport_list, len, &npu_port);
    if (rc != STD_ERR_OK) {
        return rc;
    }

    if ((rc = nas_ndi_create_bridge_port_1Q(npu_id, sai_port, false)) != STD_ERR_OK) {
        NDI_PORT_LOG_TRACE("Bridge port  create failed for npu %d, port  %lu ",
               npu_id, sai_port);
        return rc;
    }
    // ndi_del_new_member_from_default_vlan(npu_id,npu_port,false);

    if (port_id_p != NULL) {
        *port_id_p = npu_port;
    }

    if (ndi_db_ptr->switch_notification->port_event_update_cb != NULL) {
        ndi_port_t ndi_port;
        ndi_port.npu_id = npu_id;
        ndi_port.npu_port = npu_port;
        ndi_db_ptr->switch_notification->port_event_update_cb(&ndi_port,
                                                ndi_port_ADD, hwport_list[0]);
    }

    rc = ndi_port_admin_state_set(npu_id, npu_port, false);
    if (rc != STD_ERR_OK) {
        return rc;
    }
    NDI_PORT_LOG_TRACE("Physical Port create is successful for port %d", npu_port);

    return rc;
}

t_std_error ndi_phy_port_delete(npu_id_t npu_id, npu_port_t port_id)
{
    nas_ndi_db_t *ndi_db_ptr = ndi_db_ptr_get(npu_id);
    if (ndi_db_ptr == NULL) {
        NDI_PORT_LOG_ERROR("Invalid NPU Id %d", npu_id);
        return STD_ERR(NPU, PARAM, 0);
    }

    sai_object_id_t sai_port;
    t_std_error rc = ndi_sai_port_id_get(npu_id, port_id, &sai_port);
    if (rc != STD_ERR_OK) {
        return rc;
    }

    rc = ndi_stg_delete_port_stp_ports(npu_id,port_id);
    if (rc != STD_ERR_OK) {
        NDI_PORT_LOG_ERROR("Port STP SAI object remove failed for npu %d"
                " port %d, ret %d ",npu_id, port_id, rc);
        return rc;
    }

    if ((rc = nas_ndi_delete_bridge_port_1Q(npu_id, sai_port)) != STD_ERR_OK) {
        NDI_PORT_LOG_TRACE("Bridge port  create failed for npu %d, port  %lu ",
               npu_id, sai_port);
        return rc;
    }
    sai_status_t sai_ret = ndi_sai_port_api_tbl_get(ndi_db_ptr)->
                            remove_port(sai_port);
    if (sai_ret != SAI_STATUS_SUCCESS) {
        NDI_PORT_LOG_TRACE("Physical port removal failed for npu %d port %d, ret %d ",
                            npu_id, port_id, sai_ret);
        return STD_ERR(NPU, FAIL, sai_ret);
    }

    npu_port_t deleted_port;
    rc = ndi_port_map_sai_port_delete(npu_id, sai_port, &deleted_port);
    if (rc != STD_ERR_OK) {
        return rc;
    }

    if (ndi_db_ptr->switch_notification->port_event_update_cb != NULL) {
        ndi_port_t ndi_port;
        ndi_port.npu_id = npu_id;
        ndi_port.npu_port = deleted_port;
        ndi_db_ptr->switch_notification->port_event_update_cb(&ndi_port,
                                                ndi_port_DELETE, 0);
    }

    return STD_ERR_OK;
}

void nas_ndi_port_map_dump(npu_id_t npu_id,npu_port_t port_id)
{
    sai_object_id_t sai_port;
    t_std_error ret_code = STD_ERR_OK;

    if ((ret_code = ndi_sai_port_id_get(npu_id, port_id, &sai_port)) != STD_ERR_OK) {
        printf("Interface Error    : Invalid port %d", port_id);
    } else {
        printf("Interface SAI OId  : 0x%"PRIx64" \r\n",sai_port);
    }
}

t_std_error ndi_port_set_packet_drop(npu_id_t npu_id, npu_port_t port_id,
                                     ndi_port_drop_mode_t mode, bool enable)
{
    sai_attribute_t drop_mode_attr;
    if (mode == NDI_PORT_DROP_UNTAGGED) {
        drop_mode_attr.id = SAI_PORT_ATTR_DROP_UNTAGGED;
    } else if (mode == NDI_PORT_DROP_TAGGED) {
        drop_mode_attr.id = SAI_PORT_ATTR_DROP_TAGGED;
    } else {
        NDI_PORT_LOG_ERROR("Unknown packet drop mode %d", mode);
        return STD_ERR(NPU, PARAM, 0);
    }

    drop_mode_attr.value.booldata = enable;

    return _sai_port_attr_set_or_get(npu_id, port_id, SAI_SG_ACT_SET, &drop_mode_attr, 1);
}
