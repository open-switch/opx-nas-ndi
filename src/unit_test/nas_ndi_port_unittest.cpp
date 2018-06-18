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
 * filename: nas_ndi_port_unittest.cpp
 */

#include "nas_ndi_init.h"
#include "nas_ndi_port_map.h"
#include "nas_ndi_port.h"

#include <gtest/gtest.h>
#include <iostream>
#include <vector>

TEST(std_nas_ndi_port_test, nas_ndi_init)
{
    ASSERT_TRUE(nas_ndi_init() == STD_ERR_OK);
    ndi_port_map_table_dump();
    ndi_saiport_map_table_dump();
}

static bool get_avail_npu_port(npu_id_t& npu_id, npu_port_t& port_id)
{
    static const size_t MAX_NPU_PORT_NUM = 10;
    std::vector<std::pair<npu_id_t, npu_port_t>> npu_port_list;

    npu_id_t max_npu = static_cast<npu_id_t>(ndi_max_npu_get());
    for (npu_id_t npu = 0; npu < max_npu; npu ++) {
        npu_port_t max_port = static_cast<npu_port_t>(ndi_max_npu_port_get(npu));
        for (npu_port_t port = 0; port < max_port; port ++) {
            if (npu_port_list.size() >= MAX_NPU_PORT_NUM) {
                break;
            }
            if (ndi_port_is_valid(npu, port)) {
                npu_port_list.push_back({npu, port});
            }
        }
    }

    if (npu_port_list.size() == 0) {
        return false;
    }

    size_t idx = rand() % npu_port_list.size();
    npu_id = npu_port_list[idx].first;
    port_id = npu_port_list[idx].second;

    return true;
}

static bool get_packet_drop_from_mode(uint32_t mode, bool& untag_drop, bool& tag_drop)
{
    untag_drop = tag_drop = true;
    if (mode == 0) {
        return true;
    }
    BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_t tagging_mode;
    try {
        tagging_mode = static_cast<BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_t>(mode);
    } catch(...) {
        return false;
    }
    switch(tagging_mode) {
    case BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_UNTAGGED:
        untag_drop = false;
        break;
    case BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_TAGGED:
        tag_drop = false;
        break;
    case BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_HYBRID:
        untag_drop = tag_drop = false;
        break;
    }

    return true;
}

TEST(std_nas_ndi_port_test, nas_ndi_port_packet_drop)
{
    npu_id_t npu_id = 0;
    npu_port_t port_id = 0;
    ASSERT_TRUE(get_avail_npu_port(npu_id, port_id));
    std::cout << "Testing on NPU " << npu_id << " PORT " << port_id << std::endl;
    uint32_t old_mode = 0;
    ASSERT_TRUE(ndi_port_get_untagged_port_attrib(npu_id, port_id,
                reinterpret_cast<BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_t*>(&old_mode)) == STD_ERR_OK);
    std::cout << "Current tagging mode " << old_mode << std::endl;
    bool tag_drop, untag_drop;
    ASSERT_TRUE(get_packet_drop_from_mode(old_mode, untag_drop, tag_drop));
    ASSERT_TRUE(ndi_port_set_packet_drop(npu_id, port_id, NDI_PORT_DROP_UNTAGGED, !untag_drop) == STD_ERR_OK);
    ASSERT_TRUE(ndi_port_set_packet_drop(npu_id, port_id, NDI_PORT_DROP_TAGGED, !tag_drop) == STD_ERR_OK);
    uint32_t new_mode = 0;
    ASSERT_TRUE(ndi_port_get_untagged_port_attrib(npu_id, port_id,
                reinterpret_cast<BASE_IF_PHY_IF_INTERFACES_INTERFACE_TAGGING_MODE_t*>(&new_mode)) == STD_ERR_OK);
    std::cout << "New tagging mode " << new_mode << std::endl;
    bool new_tag_drop, new_untag_drop;
    ASSERT_TRUE(get_packet_drop_from_mode(new_mode, new_untag_drop, new_tag_drop));
    ASSERT_TRUE((untag_drop ^ new_tag_drop) && (tag_drop ^ new_tag_drop));
}

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
