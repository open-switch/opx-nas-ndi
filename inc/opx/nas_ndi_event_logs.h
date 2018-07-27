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
 * nas_ndi_event_logs.h
 */

#ifndef _NAS_NDI_EVENT_LOGS_H_
#define _NAS_NDI_EVENT_LOGS_H_

#include "event_log.h"

/*****************NDI Event log trace macros********************/

/*  Note: replace EV_LOG_TRACE  to EV_LOG_CON_TRACE for console msg.
 *  Only allowed during development Phase */
#define NDI_LOG_TRACE(ID, msg, ...) \
                   EV_LOGGING(NDI, DEBUG, ID, msg, ##__VA_ARGS__)

#define NDI_INIT_LOG_TRACE(msg, ...) \
                   NDI_LOG_TRACE("NDI-INIT", msg, ##__VA_ARGS__)

#define NDI_PORT_LOG_TRACE(msg, ...) \
                   NDI_LOG_TRACE("NDI-PORT", msg, ##__VA_ARGS__)

#define NDI_IDBR_LOG_TRACE(msg, ...) \
                   NDI_LOG_TRACE("NDI-1DBR", msg, ##__VA_ARGS__)


/*  Add other features log trace here  */

/******************NDI Error log macros************************/

#define NDI_LOG_ERROR(ID, msg, ...) \
                   EV_LOGGING(NDI, ERR, ID, msg, ##__VA_ARGS__)

#define NDI_INIT_LOG_ERROR(msg, ...) \
                   NDI_LOG_ERROR("NDI-INIT", msg, ##__VA_ARGS__)

#define NDI_PORT_LOG_ERROR(msg, ...) \
                   NDI_LOG_ERROR("NDI-PORT", msg, ##__VA_ARGS__)

#define NDI_VLAN_LOG_ERROR(msg, ...) \
                   NDI_LOG_ERROR("NDI-VLAN", msg, ##__VA_ARGS__)

#define NDI_LAG_LOG_ERROR(msg, ...) \
                   NDI_LOG_ERROR("NDI-LAG", msg, ##__VA_ARGS__)

#define NDI_ACL_LOG_ERROR(msg, ...) \
                   NDI_LOG_ERROR("NDI-ACL", msg, ##__VA_ARGS__)

#define NDI_UDF_LOG_ERROR(msg, ...) \
                   NDI_LOG_ERROR("NDI-UDF", msg, ##__VA_ARGS__)

#define NDI_MCAST_LOG_ERROR(msg, ...) \
                   NDI_LOG_ERROR("NDI-MCAST", msg, ##__VA_ARGS__)

#define NDI_IDBR_LOG_ERROR(msg, ...) \
                   NDI_LOG_ERROR("NDI-1DBR", msg, ##__VA_ARGS__)


/******************NDI INFO log macros************************/

#define NDI_LOG_INFO(ID, msg, ...) \
                   EV_LOGGING(NDI, INFO, ID, msg, ##__VA_ARGS__)

#define NDI_INIT_LOG_INFO(msg, ...) \
                   NDI_LOG_INFO("NDI-INIT", msg, ##__VA_ARGS__)

#define NDI_LAG_LOG_INFO(msg, ...) \
                   NDI_LOG_INFO("NDI-LAG", msg, ##__VA_ARGS__)

#define NDI_ACL_LOG_INFO(msg, ...) \
                   NDI_LOG_INFO("NDI-ACL", msg, ##__VA_ARGS__)

#define NDI_ACL_LOG_DETAIL(msg, ...) \
                   NDI_LOG_INFO("NDI-ACL", msg, ##__VA_ARGS__)

#define NDI_UDF_LOG_INFO(msg, ...) \
                   NDI_LOG_INFO("NDI-UDF", msg, ##__VA_ARGS__)

#define NDI_UDF_LOG_DETAIL(msg, ...) \
                   NDI_LOG_INFO("NDI-UDF", msg, ##__VA_ARGS__)

#define NDI_MCAST_LOG_INFO(msg, ...) \
                   NDI_LOG_INFO("NDI-MCAST", msg, ##__VA_ARGS__)


/*  Add other features log error here  */


#endif /* _NAS_NDI_EVENT_LOGS_H_  */

