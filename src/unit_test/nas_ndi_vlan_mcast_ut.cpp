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
 * filename: nas_ndi_vlan_mcast.cpp
 */

#include "nas_ndi_init.h"
#include "nas_ndi_common.h"
#include "nas_ndi_vlan.h"
#include <gtest/gtest.h>

TEST(nas_ndi_vlan_mcast_test, nas_ndi_init)
{
    ASSERT_EQ(nas_ndi_init(),STD_ERR_OK);
}
TEST(nas_ndi_vlan_mcast_test, nas_ndi_set_vlan_mcast_lkup_key)
{
    npu_id_t npu = 0;
    hal_vlan_id_t vlan_id = 100;

    ASSERT_EQ(ndi_create_vlan(npu, vlan_id),STD_ERR_OK);
    ASSERT_EQ(ndi_vlan_set_mcast_lookup_key(npu,vlan_id,NDI_IPV4_VERSION,NAS_NDI_VLAN_MCAST_LOOKUP_KEY_XG),STD_ERR_OK);
    ASSERT_EQ(ndi_vlan_set_mcast_lookup_key(npu,vlan_id,NDI_IPV4_VERSION,NAS_NDI_VLAN_MCAST_LOOKUP_KEY_SG),STD_ERR_OK);
    ASSERT_EQ(ndi_vlan_set_mcast_lookup_key(npu,vlan_id,NDI_IPV4_VERSION,NAS_NDI_VLAN_MCAST_LOOKUP_KEY_XG_AND_SG),STD_ERR_OK);
    ASSERT_EQ(ndi_vlan_set_mcast_lookup_key(npu,vlan_id,NDI_IPV4_VERSION,NAS_NDI_VLAN_MCAST_LOOKUP_KEY_MACDA),STD_ERR_OK);

    ASSERT_EQ(ndi_vlan_set_mcast_lookup_key(npu,vlan_id,NDI_IPV6_VERSION,NAS_NDI_VLAN_MCAST_LOOKUP_KEY_XG),STD_ERR_OK);
    ASSERT_EQ(ndi_vlan_set_mcast_lookup_key(npu,vlan_id,NDI_IPV6_VERSION,NAS_NDI_VLAN_MCAST_LOOKUP_KEY_SG),STD_ERR_OK);
    ASSERT_EQ(ndi_vlan_set_mcast_lookup_key(npu,vlan_id,NDI_IPV6_VERSION,NAS_NDI_VLAN_MCAST_LOOKUP_KEY_XG_AND_SG),STD_ERR_OK);
    ASSERT_EQ(ndi_vlan_set_mcast_lookup_key(npu,vlan_id,NDI_IPV6_VERSION,NAS_NDI_VLAN_MCAST_LOOKUP_KEY_MACDA),STD_ERR_OK);
    ASSERT_EQ(ndi_delete_vlan(npu, vlan_id),STD_ERR_OK);

}

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
