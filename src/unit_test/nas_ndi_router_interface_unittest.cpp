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
 * filename: nas_ndi_router_interface_unittest.cpp
 */

#include <gtest/gtest.h>

extern "C"{
#include "std_error_codes.h"
#include  "nas_ndi_int.h"
#include  "nas_ndi_init.h"
#include  "nas_ndi_router_interface.h"
}

#include "dell-base-acl.h"
#include "std_error_codes.h"
#include  "nas_ndi_acl.h"
#include  "nas_ndi_init.h"
#include  "nas_ndi_port_map.h"
#include  "nas_ndi_vlan.h"
#include "nas_ndi_event_logs.h"

ndi_vrf_id_t        vr_oid;
ndi_rif_entry_t     rif_entry;
ndi_rif_id_t        rif_id;

t_std_error nas_ndi_init_test(void) {
    t_std_error ret_code = nas_ndi_init();
    if (ret_code == STD_ERR_OK) {
        ndi_port_map_table_dump();
        ndi_saiport_map_table_dump();
    }
    return(ret_code);
}

t_std_error nas_ndi_ut_rt_intf_vr_config (bool is_del, ndi_vrf_id_t *p_vr_oid) {
    ndi_vr_entry_t  vr_entry;
    t_std_error     rc;
    hal_mac_addr_t  src_mac = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

    if (!is_del) {
        memset (&vr_entry, 0, sizeof (ndi_vr_entry_t));
        memcpy (&vr_entry.src_mac, &src_mac, sizeof (vr_entry.src_mac));
        vr_entry.flags = NDI_VR_ATTR_SRC_MAC_ADDRESS;

        if ((rc = ndi_route_vr_create(&vr_entry, &vr_entry.vrf_id))!= STD_ERR_OK)
        {
            return rc;
        }
        *p_vr_oid = vr_entry.vrf_id;
    } else {
        if ((rc = ndi_route_vr_delete(0, *p_vr_oid))!= STD_ERR_OK)
        {
            return rc;
        }
    }
    return STD_ERR_OK;
}

t_std_error nas_ndi_rt_intf_test (void) {
    t_std_error         rc;
    hal_mac_addr_t      src_mac = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

    if ((rc = nas_ndi_ut_rt_intf_vr_config (false, &vr_oid)) != STD_ERR_OK)
        return rc;

    if ((rc = ndi_create_vlan (0, 2002))!= STD_ERR_OK)
    {
        return rc;
    }

    memset (&rif_entry, 0, sizeof (ndi_rif_entry_t));
    rif_entry.npu_id = 0;
    rif_entry.vrf_id = vr_oid;
    rif_entry.rif_type = NDI_RIF_TYPE_VLAN;
    rif_entry.attachment.vlan_id = 2002;
    rif_entry.flags = NDI_RIF_ATTR_SRC_MAC_ADDRESS;
    memcpy(&rif_entry.src_mac, &src_mac, sizeof(hal_mac_addr_t));

    if ((rc = ndi_rif_create(&rif_entry, &rif_id))!= STD_ERR_OK)
    {
        return rc;
    }

    /* enable ip redirect */
    rif_entry.npu_id = 0;
    rif_entry.rif_id = rif_id;
    rif_entry.flags = NDI_RIF_ATTR_IP_REDIRECT;
    rif_entry.ip_redirect_state = true;

    if ((rc = ndi_rif_set_attribute(&rif_entry))!= STD_ERR_OK)
    {
        return rc;
    }

    /* disable ip redirect */
    rif_entry.npu_id = 0;
    rif_entry.rif_id = rif_id;
    rif_entry.flags = NDI_RIF_ATTR_IP_REDIRECT;
    rif_entry.ip_redirect_state = false;

    if ((rc = ndi_rif_set_attribute(&rif_entry))!= STD_ERR_OK)
    {
        return rc;
    }


    /* cleanup */
    if ((rc = ndi_rif_delete (0, rif_id)) != STD_ERR_OK)
    {
        return rc;
    }
    if ((rc = nas_ndi_ut_rt_intf_vr_config (true, &vr_oid)) != STD_ERR_OK)
        return rc;

    return(rc);
}

TEST(std_nas_ndi_rt_test, nas_ndi_rt_intf_test) {
    ASSERT_EQ(STD_ERR_OK, nas_ndi_init_test());
    ASSERT_EQ(STD_ERR_OK, nas_ndi_rt_intf_test());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
