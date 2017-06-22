/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/*
 * filename: nas_ndi_udf_utl.h
 */

#ifndef __NAS_NDI_UDF_UTL_H__
#define __NAS_NDI_UDF_UTL_H__

#include "saiudf.h"
#include <list>
#include <stdlib.h>

const sai_udf_api_t *ndi_udf_api_get(const nas_ndi_db_t *ndi_db_ptr);

//////////////////////////////////////////////////////////
//Utilities to convert IDs from NDI to SAI and vice-versa
/////////////////////////////////////////////////////////
#define ndi_udf_ndi2sai_grp_id(x)         (sai_object_id_t) (x)
#define ndi_udf_ndi2sai_match_id(x)       (sai_object_id_t) (x)
#define ndi_udf_ndi2sai_udf_id(x)         (sai_object_id_t) (x)
#define ndi_udf_sai2ndi_grp_id(x)         (ndi_obj_id_t) (x)
#define ndi_udf_sai2ndi_match_id(x)       (ndi_obj_id_t) (x)
#define ndi_udf_sai2ndi_udf_id(x)         (ndi_obj_id_t) (x)

#endif
