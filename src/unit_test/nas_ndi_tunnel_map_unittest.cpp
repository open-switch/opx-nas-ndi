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
 * filename: nas_ndi_tunnel_map_ut.cpp
 */

#include <gtest/gtest.h>
#include "ds_common_types.h"
#include "nas_ndi_tunnel_map.h"
#include "nas_ndi_tunnel_obj.h"

using namespace std;


static void std_ip_from_inet(hal_ip_addr_t *ip,struct in_addr *addr)
{
    memset (ip, 0, sizeof (hal_ip_addr_t));
    ip->af_index = AF_INET;
    ip->u.v4_addr = addr->s_addr;
}


TEST(std_nas_tunnel_map, ndi_tunnel_obj_map_test) {

    struct in_addr ins = {0x01010b0a};

    hal_ip_addr_t loc_ip, rem_ip;
    std_ip_from_inet(&loc_ip,&ins);

    struct in_addr inr = {0x01010b0b};
    std_ip_from_inet(&rem_ip,&inr);


    TunnelObj *obj1 = new TunnelObj(3000001, 40000001, 5000001,
                                6000000001, loc_ip, rem_ip);
    insert_tunnel_obj(&rem_ip, &loc_ip, obj1);

    inr = {0x01010b0c};
    std_ip_from_inet(&rem_ip,&inr);

    TunnelObj *obj2 = new TunnelObj(3000002, 40000002, 5000002,
                                6000000002, loc_ip, rem_ip);
    insert_tunnel_obj(&rem_ip, &loc_ip, obj2);

    inr = {0x01010b0d};
    std_ip_from_inet(&rem_ip,&inr);

    TunnelObj *obj3 = new TunnelObj(3000003, 40000003, 5000003,
                                6000000003, loc_ip, rem_ip);
    insert_tunnel_obj(&rem_ip, &loc_ip, obj3);


    cout << "All 3 members of the tunnel"  << endl;
    print_tunnel_map();
    cout << "--------------------------------------------------------------------" <<endl <<endl;
    inr = {0x01010b0c};
    std_ip_from_inet(&rem_ip,&inr);

    ASSERT_TRUE(has_tunnel_obj(&rem_ip, &loc_ip));

    auto obj = get_tunnel_obj(&rem_ip, &loc_ip);
    if (obj == NULL) {
        cout << "get_tunnel_obj failed " <<endl;
    }
    ASSERT_TRUE(remove_tunnel_obj(&rem_ip, &loc_ip) == false);
    cout << "Remaining 2 members of the tunnel after deleting one"  << endl;
    print_tunnel_map();
    cout << "--------------------------------------------------------------------" <<endl <<endl;

    inr = {0x01010b0b};
    std_ip_from_inet(&rem_ip,&inr);
    ASSERT_TRUE(remove_tunnel_obj(&rem_ip, &loc_ip) == false);
    cout << "Remaining 1 members of the tunnel after deleting one"  << endl;
    print_tunnel_map();
    cout << "--------------------------------------------------------------------" <<endl <<endl;

    inr = {0x01010b0d};
    std_ip_from_inet(&rem_ip,&inr);
    ASSERT_TRUE(remove_tunnel_obj(&rem_ip, &loc_ip) == false);
    print_tunnel_map();
}



int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


