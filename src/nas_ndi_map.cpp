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
 * filename: nas_ndi_map.cpp
 */

#include "nas_ndi_map.h"
#include "std_mutex_lock.h"
#include <unordered_map>
#include <vector>
#include <stdlib.h>
#include <stdio.h>

struct _nas_ndi_map_hash
{
    size_t operator()(const nas_ndi_map_key_t& key) const {
        size_t hash;

        hash  = std::hash<int>()(key.type);
        hash ^= std::hash<uint64_t>()(key.id1);
        hash ^= std::hash<uint64_t>()(key.id2);
        return (hash);
    }
};

struct _nas_ndi_map_equal
{
    bool operator()(const nas_ndi_map_key_t& key1, const nas_ndi_map_key_t& key2) const {
        if ((key1.type == key2.type) &&
            (key1.id1 == key2.id1) && (key1.id2 == key2.id2)) {
            return true;
        }
        else {
            return false;
        }
    }
};

static inline bool operator==(const nas_ndi_map_key_t& key1, const nas_ndi_map_key_t& key2)
{
    return _nas_ndi_map_equal()(key1, key2);
}

std_mutex_lock_create_static_init_fast (nas_ndi_map_mutex);

static std::unordered_map<nas_ndi_map_key_t, std::vector <nas_ndi_map_data_t>, _nas_ndi_map_hash, _nas_ndi_map_equal> g_nas_ndi_map;

static bool nas_ndi_map_apply_filter (nas_ndi_map_data_t       *arg1,
                                      nas_ndi_map_data_t       *arg2,
                                      nas_ndi_map_val_filter_t  filter)
{
    if ((filter & NAS_NDI_MAP_VAL_FILTER_NONE) == NAS_NDI_MAP_VAL_FILTER_NONE) {
        return true;
    }

    if ((filter & NAS_NDI_MAP_VAL_FILTER_VAL1) == NAS_NDI_MAP_VAL_FILTER_VAL1) {
        if (arg1->val1 != arg2->val1) {
            return false;
        }
    }

    if ((filter & NAS_NDI_MAP_VAL_FILTER_VAL2) == NAS_NDI_MAP_VAL_FILTER_VAL2) {
        if (arg1->val2 != arg2->val2) {
            return false;
        }
    }

    return true;
}

static void nas_ndi_map_copy_value (nas_ndi_map_data_t       *dst,
                                    nas_ndi_map_data_t       *src,
                                    nas_ndi_map_val_filter_t  filter)
{
    if ((filter & NAS_NDI_MAP_VAL_FILTER_NONE) == NAS_NDI_MAP_VAL_FILTER_NONE) {
        *dst = *src;
        return;
    }

    if ((filter & NAS_NDI_MAP_VAL_FILTER_VAL1) == NAS_NDI_MAP_VAL_FILTER_VAL1) {
        dst->val1 = src->val1;
    }

    if ((filter & NAS_NDI_MAP_VAL_FILTER_VAL2) == NAS_NDI_MAP_VAL_FILTER_VAL2) {
        dst->val2 = src->val2;
    }
}

extern "C" {

t_std_error nas_ndi_map_insert (nas_ndi_map_key_t *key, nas_ndi_map_val_t *value)
{
    t_std_error rc = STD_ERR_OK;
    uint32_t    i;

    std_mutex_lock (&nas_ndi_map_mutex);

    try {
        auto map_it = g_nas_ndi_map.find (*key);

        if (map_it != g_nas_ndi_map.end()) {
            std::vector <nas_ndi_map_data_t>& list = map_it->second;

            for (i = 0; i < value->count; i++) {
                list.push_back (value->data[i]);
            }
        }
        else {
            std::vector <nas_ndi_map_data_t> new_list {};

            for (i = 0; i < value->count; i++) {
                new_list.push_back (value->data[i]);
            }
            g_nas_ndi_map.insert (std::make_pair (*key, new_list));
        }
    }
    catch (...) {
        rc = STD_ERR(NPU, FAIL, 0);
    }

    std_mutex_unlock (&nas_ndi_map_mutex);
    return (rc);
}

t_std_error nas_ndi_map_delete (nas_ndi_map_key_t *key)
{
    t_std_error rc = STD_ERR_OK;

    std_mutex_lock (&nas_ndi_map_mutex);

    try {
        auto map_it = g_nas_ndi_map.find (*key);
        if (map_it != g_nas_ndi_map.end()) {
            map_it->second.clear();
            g_nas_ndi_map.erase (map_it);
        }
    }
    catch (...) {
        rc = STD_ERR(NPU, FAIL, 0);
    }

    std_mutex_unlock (&nas_ndi_map_mutex);
    return rc;
}

t_std_error nas_ndi_map_delete_elements (nas_ndi_map_key_t        *key,
                                         nas_ndi_map_val_t        *value,
                                         nas_ndi_map_val_filter_t  filter)
{
    t_std_error rc = STD_ERR_OK;
    uint32_t    i;
    size_t      position;

    std_mutex_lock (&nas_ndi_map_mutex);

    try {
        auto map_it = g_nas_ndi_map.find (*key);
        if (map_it != g_nas_ndi_map.end()) {
            std::vector <nas_ndi_map_data_t>& list = map_it->second;

            for (i = 0; i < value->count; i++) {
                position = 0;
                for (auto data: list) {
                    if (nas_ndi_map_apply_filter (&value->data[i],
                                                  &data, filter)) {
                        list.erase (list.begin() + position);
                        break;
                    }
                    position++;
                }
            }
        }
    }
    catch (...) {
        rc = STD_ERR(NPU, FAIL, 0);
    }

    std_mutex_unlock (&nas_ndi_map_mutex);

    return rc;
}

t_std_error nas_ndi_map_get (nas_ndi_map_key_t *key, nas_ndi_map_val_t *value)
{
    size_t count;
    uint32_t i;
    t_std_error rc = STD_ERR_OK;

    std_mutex_lock (&nas_ndi_map_mutex);

    try {
        auto map_it = g_nas_ndi_map.find (*key);
        if (map_it != g_nas_ndi_map.end()) {
            std::vector <nas_ndi_map_data_t>& list = map_it->second;

            count = list.size();

            if (count > value->count) {
                /*
                 * The passed buffer is insufficient. So fill
                 * the count with the actual number of elements,
                 * so that the caller will call with sufficient
                 * memory again.
                 */
                rc = STD_ERR (NPU, NOMEM, 0);
            }
            else {
                for (i = 0; i < count; i++) {
                    value->data[i] = list.at(i);
                }
            }

            value->count = count;
        }
        else {
            rc = STD_ERR(NPU, NEXIST, 0);
        }
    }
    catch (...) {
        rc = STD_ERR(NPU, FAIL, 0);
    }

    std_mutex_unlock (&nas_ndi_map_mutex);
    return rc;
}

t_std_error nas_ndi_map_get_elements (nas_ndi_map_key_t        *key,
                                      nas_ndi_map_val_t        *value,
                                      nas_ndi_map_val_filter_t  filter)
{
    t_std_error rc = STD_ERR_OK;
    uint32_t    i;

    std_mutex_lock (&nas_ndi_map_mutex);

    try {
        auto map_it = g_nas_ndi_map.find (*key);
        if (map_it != g_nas_ndi_map.end()) {
            std::vector <nas_ndi_map_data_t>& list = map_it->second;

            for (i = 0; i < value->count; i++) {
                for (auto data: list) {
                    if (nas_ndi_map_apply_filter (&value->data[i],
                                                  &data, filter)) {
                        nas_ndi_map_copy_value (&value->data[i],
                                                &data, filter);
                        break;
                    }
                }
            }
        }
    }
    catch (...) {
        rc = STD_ERR(NPU, FAIL, 0);
    }

    std_mutex_unlock (&nas_ndi_map_mutex);

    return rc;
}

t_std_error nas_ndi_map_get_val_count (nas_ndi_map_key_t *key, size_t *count)
{
    t_std_error rc = STD_ERR_OK;

    std_mutex_lock (&nas_ndi_map_mutex);

    try {
        auto map_it = g_nas_ndi_map.find (*key);
        if (map_it != g_nas_ndi_map.end()) {
            std::vector <nas_ndi_map_data_t>& list = map_it->second;

            *count = list.size();
        }
        else {
            rc = STD_ERR(NPU, NEXIST, 0);
        }
    }
    catch (...) {
        rc = STD_ERR(NPU, FAIL, 0);
    }

    std_mutex_unlock (&nas_ndi_map_mutex);
    return rc;
}
}
