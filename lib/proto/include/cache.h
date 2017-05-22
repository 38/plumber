/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The protocol type cache
 * @details Because we use the filesystem as the database, so we have some performance concern
 *         on the disk IO performance. At the same time, a type may be reference multiple times,
 *         and it's bad to load the same type multiple times, since we don't actually needs to do
 *         thaty
 **/
#ifndef __PROTO_CACHE_H__
#define __PROTO_CACHE_H__

/**
 * @brief the callback function that used to dispose the node attached data
 * @param data the data to dispose
 * @return status code
 **/
typedef int (*proto_cache_node_data_dispose_func_t)(void* data);

/**
 * @brief initialize the cache
 * @return status code
 **/
int proto_cache_init();

/**
 * @brief finalize the the cache
 * @return status code
 **/
int proto_cache_finalize();

/**
 * @brief get the full type name for the given type name
 * @note because we allows relative typename referencing, for example, if there a type
 *       graphics.Vector2f and the graphics.Point2D inherit the type.
 *       The Point2D only needs to use the Vector2f for the Vector2f type, because
 *       graphics is the pwd. <br/>
 *       Because of this, the typename may not be the real typename. <br/>
 *       When requesting a type with pwd, the cache system will look up the path &lt;pwd&gt;. &lt; type_name &gt;
 *       first, and then try the absolute path. <br/>
 *       This function is used to convert the type name with pwd.
 * @param type_name the typename
 * @param pwd the current working directory
 * @return status code
 **/
const char* proto_cache_full_name(const char* type_name, const char* pwd);

/**
 * @brief check if the full typename exists
 * @param type_name the full typename
 * @return if this type exists in the system, or error code
 **/
int proto_cache_full_type_name_exist(const char* type_name);

/**
 * @brief Put or update the protocol type cache
 * @param type_name the the type name
 * @param proto the protocol type object
 * @note If it's in normal mode this function will transfer the ownership of the protocol type buffer, so do not
 *       dispose the protocol type object after this function sucessfully returned <br/>
 *       However, if it's in sandbox mode, it only take a reference, so the caller should dispose the protocol type
 *       object properly. <br/>
 *       This function won't maintain the reverse dependency list, so its upper level's responsibility
 *       to call the proto_cache_revdep_add
 * @return status code
 **/
int proto_cache_put(const char* type_name, proto_type_t* proto);

/**
 * @brief delete a type from the database
 * @param type_name the name of the type
 * @note this function will delete the type from disk and if the type is cached,
 *       remove it from cache as well
 * @return status code
 **/
int proto_cache_delete(const char* type_name);

/**
 * @brief attach the type data to the node in the cache
 * @note after this function successfully returned, the data pointer will be taken over by the cache system, so after that
 *       do not dispose the data pointer directly. <br>
 *       If the newly given data is NULL, dispose the prevous data
 * @param type_name the name of the type
 * @param pwd the current working directory of the type
 * @param dispose_cb the callback funciton which is used to cleanup the used data
 * @param data the data to attach
 * @return status code
 **/
int proto_cache_attach_type_data(const char* type_name, const char* pwd, proto_cache_node_data_dispose_func_t dispose_cb, void* data);

/**
 * @brief get the type to the protocol type cache
 * @param type_name the name for the type
 * @param pwd the current package name, will be used for relative type name
 * @param data the buffer used to return the node data, if NULL given, do not return the data buffer
 * @return the typename or NULL on error cases
 **/
const proto_type_t* proto_cache_get_type(const char* type_name, const char* pwd, void** data);

/**
 * @brief get the reverse dependency information of the given type name
 * @param type_name the type to query
 * @param pwd the current PWD
 * @return the NULL-terminated reverse dependency list, or NULL on error case
 **/
char const* const* proto_cache_revdep_get(const char* type_name, const char* pwd);

/**
 * @brief set the protocol type database root directory
 * @param root the root directory
 * @return status code
 **/
int proto_cache_set_root(const char* root);

/**
 * @brief set the cache to sandbox mode, which do not actuall write the filesystem, just update the in memory version
 * @param mode if the sandbox mode need to be enabled
 * @return status code
 **/
void proto_cache_sandbox_mode(int mode);

/**
 * @brief flush all the in-memory change to disk
 * @note this function won't perform the sandbox changes and can not be used in sandbox mode
 * @return status code
 **/
int proto_cache_flush();

/**
 * @brief get current root of the db
 * @return the root, NULL on error case
 **/
const char* proto_cache_get_root();

#endif /* __PROTO_CACHE_H__ */
