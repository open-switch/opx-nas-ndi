#include "std_error_codes.h"
#include "cps_api_operation.h"
#include "nas_ndi_event_logs.h"
#include "nas_sw_profile_api.h"
#include "sai.h"
#include <map>
#include <string.h>

typedef std::map<std::string, std::string> nas_ndi_cfg_kv_pair_t;
typedef std::map<std::string, std::string>::iterator kv_iter;
static auto kvpair = new nas_ndi_cfg_kv_pair_t;

static const char* ndi_profile_set_value(int profile_id,
                                 const char* variable,
                                 std::string value)
{
    kv_iter kviter;

    if (variable ==  NULL)
    {
        NDI_INIT_LOG_TRACE("%s - key value NULL  \n", __FUNCTION__);
        return NULL;
    }
    kviter = kvpair->find(variable);
    if (kviter == kvpair->end()) {
        kvpair->insert(std::pair<std::string, std::string>(variable, value));
    }
    else {
        kviter->second = value;
    }
    NDI_INIT_LOG_TRACE("populated key-value pair (%s - %s)\n", variable,
                        value.c_str());
    return value.c_str();
}
static inline void nas_ndi_get_file_name_from_profile(char *profile, char *file_name)
{

    char *profile_file_name = getenv("OPX_SAI_PROFILE_FILE");
    if (profile_file_name  != NULL) {
        snprintf(file_name, strlen(profile_file_name)+1, "%s", profile_file_name );
    }
    else {
#define NAS_NDI_FILE_PATH "/etc/opx/sai/"
        int size = 0;
        size = strlen(NAS_NDI_FILE_PATH) + strlen(profile) + strlen("-init.xmli");

        snprintf(file_name, size, "%s%s%s",NAS_NDI_FILE_PATH, profile,"-init.xml");
    }
}


extern "C" {
const char* ndi_profile_get_value(sai_switch_profile_id_t profile_id,
                                 const char* variable)
{
    kv_iter kviter;
    std::string key;

    if (variable ==  NULL)
        return NULL;

    key = variable;

    kviter = kvpair->find(key);
    if (kviter == kvpair->end()) {
        return NULL;
    }
    NDI_INIT_LOG_TRACE("get value for the key %s %s\n", variable,
                        kviter->second.c_str());
    return kviter->second.c_str();
}

int ndi_profile_get_next_value(sai_switch_profile_id_t profile_id,
                           const char** variable,
                           const char** value)
{
    kv_iter kviter;
    std::string key;

    if (variable == NULL || value == NULL) {
        return -1;
    }
    if (*variable == NULL) {
            if (kvpair->size() < 1) {
            return -1;
        }
        kviter = kvpair->begin();
    } else {
        key = *variable;
        kviter = kvpair->find(key);
        if (kviter == kvpair->end()) {
            return -1;
        }
        kviter++;
        if (kviter == kvpair->end()) {
            return -1;
        }
    }
    *variable = (char *)kviter->first.c_str();
    *value = (char *)kviter->second.c_str();
    NDI_INIT_LOG_TRACE("get next key-value pair key %s, value %s\n",
                        *variable, *value);
    return 0;
}

void nas_ndi_populate_cfg_key_value_pair (uint32_t switch_id)
{
    char conf_profile[64] = {0};
    char file_name[256] = {0};
    char key[NAS_CMN_NPU_PROFILE_ATTR_SIZE] = { 0 };
    char value[NAS_CMN_NPU_PROFILE_ATTR_SIZE] = { 0 };
    char acl_prof_db_name[NAS_CMN_PROFILE_NAME_SIZE] = { 0 };

    uint32_t conf_uft_mode,l2_size,l3_size,l3_host_size;
    uint32_t cur_max_ecmp_per_grp = 0;
    uint32_t cur_ipv6_ext_prefix_routes = 0;
    bool cur_deep_buffer_mode = false;
    uint32_t acl_prof_num_pool_req = 0;
    t_std_error ret = STD_ERR_OK;

    conf_uft_mode = l2_size = l3_size =  l3_host_size = 0;

    NDI_INIT_LOG_TRACE("nas ndi populate key value pair \n");

    /* after reading from DB and File, the read mode will be  in
        current as well as next_boot* */
    ret = nas_sw_profile_current_uft_get(&conf_uft_mode);

    if (ret != STD_ERR_OK)
    {
        NDI_INIT_LOG_TRACE("UFT mode is not configured ");
    }
    else
    {
        ret = nas_sw_profile_uft_info_get(conf_uft_mode, &l2_size,
                                    &l3_size, &l3_host_size);
        if (ret != STD_ERR_OK)
        {
            NDI_INIT_LOG_TRACE("Failed to get UFT info for mode %d ",
                                conf_uft_mode);
        }
        else
        {
            ndi_profile_set_value(switch_id, "SAI_FDB_TABLE_SIZE", std::to_string(l2_size));

            ndi_profile_set_value(switch_id, "SAI_L3_NEIGHBOR_TABLE_SIZE",
                                    std::to_string(l3_host_size));
            ndi_profile_set_value(switch_id, "SAI_L3_ROUTE_TABLE_SIZE", std::to_string(l3_size));
        }
    }

    ret = nas_sw_profile_current_profile_get(switch_id, conf_profile,
                                            sizeof(conf_profile));
    if (ret != STD_ERR_OK)
    {
        NDI_INIT_LOG_TRACE("Failed to get configured switch profile ");
    }
    else
    {
        nas_ndi_get_file_name_from_profile(conf_profile, file_name);
        ndi_profile_set_value(switch_id, "SAI_INIT_CONFIG_FILE", file_name);
    }
    /* Get current ecmp value and update */
    ret = nas_sw_profile_cur_max_ecmp_per_grp_get(&cur_max_ecmp_per_grp);
    if (ret != STD_ERR_OK)
    {
        NDI_INIT_LOG_TRACE("Failed to get current max_ecmp_per_grp ");
    }
    else
    {
        /* in case if max_ecmp_per_grp default value is not mentioned in
            switch.xml, then let lower layer set the default.
            not over write with 0 */
        if (cur_max_ecmp_per_grp == 0)
        {
            NDI_INIT_LOG_TRACE("current max_ecmp_per_grp 0, skip key-value ");
        }
        else
        {
            ndi_profile_set_value(switch_id, "SAI_NUM_ECMP_MEMBERS",
                                    std::to_string(cur_max_ecmp_per_grp));
        }
    }
    /* Get current ipv6 ext prefix value and update */
    ret = nas_sw_profile_cur_ipv6_ext_prefix_routes_get(&cur_ipv6_ext_prefix_routes);
    if (ret != STD_ERR_OK)
    {
        NDI_INIT_LOG_TRACE("Failed to get current Ipv6 extended prefix routes");
    }
    else
    {
        /* in case if max_ecmp_per_grp default value is not mentioned in
            switch.xml, then let lower layer set the default.
            not over write with 0 */
        if (cur_ipv6_ext_prefix_routes == 0)
        {
            NDI_INIT_LOG_TRACE("current ipv6_ext_prefix_routes 0, skip key-value ");
        }
        else
        {
            ndi_profile_set_value(switch_id, "SAI_KEY_L3_ROUTE_EXTENDED_PREFIX_ENTRIES",
                                    std::to_string(cur_ipv6_ext_prefix_routes));
        }
    }

    /* Get current deep buffer mode and update */
    ret = nas_sw_profile_cur_deep_buffer_mode_get(&cur_deep_buffer_mode);

    if (ret == STD_ERR_OK) 
    {
        ndi_profile_set_value(switch_id, "SAI_SWITCH_PDM_MODE",
                              std::to_string(cur_deep_buffer_mode));
        NDI_INIT_LOG_TRACE("ndi profile set: %d for SAI_SWITCH_PDM_MODE\n", 
                           cur_deep_buffer_mode);
    }

    /* after reading ACL profile default config from file and also from DB,
     * read ACL profile info will be in current as well as next_boot info.
     */
    ret = nas_sw_acl_profile_db_get_next(NULL, acl_prof_db_name, &acl_prof_num_pool_req);

    if (ret != STD_ERR_OK)
    {
        NDI_INIT_LOG_TRACE("ACL profile DB is not configured ");
    }
    else
    {
        do
        {
            NDI_INIT_LOG_TRACE("ACL profile DB info name:%s, num_pool_req:%d ",
                    acl_prof_db_name, acl_prof_num_pool_req);

            /* set the ACL profile DB in key-value pair for it to be ready by SAI */
            ndi_profile_set_value(switch_id, acl_prof_db_name, std::to_string(acl_prof_num_pool_req));

            ret = nas_sw_acl_profile_db_get_next(acl_prof_db_name,
                    acl_prof_db_name, &acl_prof_num_pool_req);

        } while (ret == STD_ERR_OK);
    }

    ret = nas_switch_npu_profile_get_next_value(key, value);
    if (ret != STD_ERR_OK)
    {
        NDI_INIT_LOG_TRACE("No NPU Switch Profile Configured");
    }
    else
    {
        for (; (ret == STD_ERR_OK);
                (ret = nas_switch_npu_profile_get_next_value(key, value))) {
           ndi_profile_set_value(switch_id, key, value);
        }
    }

    return;
}

} /* extern "C" */
