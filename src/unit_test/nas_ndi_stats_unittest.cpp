/*
 * Copyright (c) 2019 Dell Inc.
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
#include "dell-interface.h"
#include "ietf-interfaces.h"

#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <vector>

TEST(nas_ndi_port_stats_test, nas_ndi_init)
{
    ASSERT_TRUE(nas_ndi_init() == STD_ERR_OK);
}

TEST(nas_ndi_port_stats_test, nas_ndi_get_stats)
{
    npu_id_t npu_id = 0;
    npu_port_t port_id = 1;
    const int max_port_stat_id = 26;

    static uint64_t if_stat_ids[max_port_stat_id] = {
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_ALIGNMENT_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_FCS_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_SINGLE_COLLISION_FRAMES,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_MULTIPLE_COLISION_FRAMES,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_SQE_TEST_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_DEFERRED_TRANSMISSIONS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_LATE_COLLISIONS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_EXCESSIVE_COLLISIONS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_INTERNAL_MAC_TRANSMIT_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_CARRIER_SENSE_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_FRAME_TOO_LONG,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_INTERNAL_MAC_RECEIVE_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_SYMBOL_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IN_UNKNOWN_OPCODES,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IP_IN_RECEIVES,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IP_IN_HDR_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IP_IN_DISCARDS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IP_IN_FORW_DATAGRAMS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_RECEIVES,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_HDR_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_ADDR_ERRORS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_DISCARDS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_OUT_FORW_DATAGRAMS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_OUT_DISCARDS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_IN_MCAST_PKTS,
                                      DELL_IF_IF_INTERFACES_STATE_INTERFACE_STATISTICS_IPV6_OUT_MCAST_PKTS
                                    };

    uint64_t stat_values[max_port_stat_id];
    memset(stat_values,0,sizeof(stat_values));

    ASSERT_TRUE(ndi_port_stats_get(npu_id, port_id, if_stat_ids, stat_values, max_port_stat_id) == STD_ERR_OK);

    std::ofstream fd ("/tmp/stats.txt");
    fd << "Stats for NPU ID: 0 Port ID: 1\n";
    for(unsigned int ix = 0 ; ix < max_port_stat_id ; ++ix ) {
        fd << stat_values[ix] << " ";
    }
    fd.close();
}

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
