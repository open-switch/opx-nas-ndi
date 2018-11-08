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
 * filename: nas_ndi_init_unittest.cpp
 */

#include <gtest/gtest.h>
#include "nas_ndi_obj_cache.h"
#include "nas_ndi_obj_cache_utils.h"
using namespace std;


static void print_virtual_obj(ndi_virtual_obj_t & obj){
    std::cout<<"Obj id "<<obj.oid<<std::endl;
    std::cout<<"VLAN id "<<obj.vid<<std::endl;
    std::cout<<std::endl;
}

TEST(std_nas_ndi_obj_cache_test, ndi_virtual_obj_cache_test) {

    ndi_virtual_obj_t obj;
    obj.oid = 0xffff;
    obj.vid = 10;

    ASSERT_TRUE(nas_ndi_add_virtual_obj(&obj));

    ndi_virtual_obj_t obj1;
    obj1.oid = 0xaaaa;
    obj1.vid = 8000;

    ASSERT_TRUE(nas_ndi_add_virtual_obj(&obj1));

    ndi_virtual_obj_t obj2;
    obj2.oid = 0xffff;
    ASSERT_TRUE(nas_ndi_get_virtual_obj(&obj2,ndi_virtual_obj_query_type_FROM_OBJ));

    print_virtual_obj(obj2);

    ndi_virtual_obj_t obj3;
    obj3.vid = 8000;
    ASSERT_TRUE(nas_ndi_get_virtual_obj(&obj3,ndi_virtual_obj_query_type_FROM_VLAN));

    print_virtual_obj(obj3);

    ASSERT_TRUE(nas_ndi_remove_virtual_obj(&obj));
    ASSERT_TRUE(nas_ndi_remove_virtual_obj(&obj1));

    ASSERT_FALSE(nas_ndi_get_virtual_obj(&obj2,ndi_virtual_obj_query_type_FROM_OBJ));
    ASSERT_FALSE(nas_ndi_get_virtual_obj(&obj3,ndi_virtual_obj_query_type_FROM_VLAN));

    /* Bridge port tests */

    ndi_brport_obj_t port_obj1, port_obj2;
    ndi_brport_obj_t subport_obj, subport_obj1;

    port_obj1.brport_obj_id = 100;
    port_obj1.port_obj_id= 4098;
    port_obj1.brport_type = ndi_brport_type_PORT;

    port_obj2.brport_obj_id = 101;
    port_obj2.port_obj_id= 4099;
    port_obj2.brport_type = ndi_brport_type_PORT;

    subport_obj.brport_obj_id = 200;
    subport_obj.port_obj_id = 3000;
    subport_obj.vlan_id =  301;
    subport_obj.brport_type = ndi_brport_type_SUBPORT_TAG;

    subport_obj1.brport_obj_id = 201;
    subport_obj1.port_obj_id = 4098; /* same as port_obj1 */
    subport_obj1.vlan_id =  301;
    subport_obj1.brport_type = ndi_brport_type_SUBPORT_UNTAG;



    ASSERT_TRUE(nas_ndi_add_bridge_port_obj(&port_obj1));
    ASSERT_TRUE(nas_ndi_add_bridge_port_obj(&port_obj2));
    ASSERT_TRUE(nas_ndi_add_bridge_port_obj(&subport_obj));
    ASSERT_TRUE(nas_ndi_add_bridge_port_obj(&subport_obj1));

    ndi_brport_obj_t obj_get;
    obj_get.port_obj_id = 4098;
    ASSERT_TRUE(nas_ndi_get_bridge_port_obj(&obj_get, ndi_brport_query_type_FROM_PORT));
    cout << "query_FROM_PORT_1Q for 4098 " << obj_get.brport_obj_id << " " << obj_get.port_obj_id << endl;

    /* TO DO FOR PORT to brport queRY */
    obj_get.brport_obj_id = 201;
    ASSERT_TRUE(nas_ndi_get_bridge_port_obj(&obj_get, ndi_brport_query_type_FROM_BRPORT));
    cout << "query_FROM_BRPORT for 201 " << obj_get.brport_obj_id << " " << obj_get.port_obj_id
          <<" " << obj_get.vlan_id << endl;


   obj_get.vlan_id = 301;
   obj_get.port_obj_id =3000;
   ASSERT_TRUE(nas_ndi_get_bridge_port_obj(&obj_get, ndi_brport_query_type_FROM_PORT_VLAN));
   cout << "query_FROM_PORT_VLAN for vlan 301 " << obj_get.brport_obj_id << " " << obj_get.port_obj_id << endl;


   obj_get.vlan_id = 301;
   obj_get.port_obj_id =4098;
   ASSERT_TRUE(nas_ndi_get_bridge_port_obj(&obj_get, ndi_brport_query_type_FROM_PORT_VLAN));
   cout << "query_FROM_PORT_VLAN for vlan 301 " << obj_get.brport_obj_id << " " << obj_get.port_obj_id << endl;


   brport_list lst;
   ASSERT_TRUE(nas_ndi_get_bridge_port_obj_list(4098, lst));
   for(auto it_po = lst.begin();
       it_po != lst.end(); it_po++) {
       cout << "2 brport in list of  port 4098: BRIDGEPORT is " << *(it_po) << endl;
   }

   ASSERT_TRUE(nas_ndi_remove_bridge_port_obj(&port_obj1));
   ASSERT_TRUE(nas_ndi_remove_bridge_port_obj(&port_obj2));

   ASSERT_TRUE(nas_ndi_remove_bridge_port_obj(&subport_obj));
   ASSERT_TRUE(nas_ndi_remove_bridge_port_obj(&subport_obj1));

   ASSERT_FALSE(nas_ndi_get_bridge_port_obj(&subport_obj, ndi_brport_query_type_FROM_PORT_VLAN));
   ASSERT_FALSE(nas_ndi_get_bridge_port_obj(&port_obj2, ndi_brport_query_type_FROM_PORT));
   ASSERT_FALSE(nas_ndi_get_bridge_port_obj(&port_obj1, ndi_brport_query_type_FROM_BRPORT));

}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


