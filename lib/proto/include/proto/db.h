/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The protocol type database system
 * @details This is the system that actually manages all the type registered in the
 *         system. It will use a cache for all the loaded types
 * @file proto/include/proto/db.h
 **/

#ifndef __PROTO_DB_H_
#define __PROTO_DB_H_

/**
 * @brief The primitive type metadata
 **/
typedef struct {
	uint32_t         is_primitive:1; /*!< If this is a primitive value */
	uint32_t         is_real:1;      /*!< If this primitive is a real number */
	uint32_t         is_signed:1;    /*!< If this is a signed number */
	uint32_t         is_scope:1;     /*!< If this is a scope object */
	uint32_t         size;           /*!< The size in bytes for this primitive */
} proto_db_primitvie_metadata_t;

/**
 * @brief initialize the protocol database
 * @note for each servlet uses the protocol database, this function should be called in the init callback and
 *       the finailize function should be called in the cleanup callback to avoid memory leak.
 * @return status code
 **/
int proto_db_init();

/**
 * @brief dispose a used protocol database context
 * @return status code
 **/
int proto_db_finalize();

/**
 * @brief query a type from given type name
 * @param type_name the name of the type to query
 * @return the protocol type object for this type
 **/
const proto_type_t* proto_db_query_type(const char* type_name);

/**
 * @brief compute the actual size of the protocol type
 * @param type_name the protocol type object to compute
 * @note This function doesn't support the adhoc primitive type
 * @return the size of the type or error code
 **/
uint32_t proto_db_type_size(const char* type_name);

/**
 * @brief compute the offset from the beging of the type to the queried field.
 * @note  The name is name of the field. <br/>
 *        For example, for a Point3D, we can query <br/>
 *        proto_db_type_offset(point3d_type, "y") and it will return 4 <br/>
 *        This function also accept the complex name, for example in a triangle3D type,
 *        we can use proto_db_type_offset(triangle3d_type, "vertices[0].x" for the x coordinate
 *        of the first vertex.
 * @param type_name the name of the type
 * @param fieldname the field name descriptor
 * @param size_buf the buffer used to return the data size of this name expression, NULL if we don't want the info
 * @return the offset from the beginging of the data strcturue, or error code
 **/
uint32_t proto_db_type_offset(const char* type_name, const char* fieldname, uint32_t* size_buf);

/**
 * @brief Get the field name of the type
 * @note If the field is a primitive, it will returns an adhoc data type for the primitves, for example uint32, etc
 *       For this kinds of virtual typename, the only thing we can access is <typename>.value, which has the primitive
 *       type
 * @param type_name The type name
 * @param fieldname The field name descriptor
 * @return The field type name
 **/
const char* proto_db_field_type(const char* type_name, const char* fieldname);

/**
 * @brief validate the type is well formed, which means the type's references are all defined.
 *        And there's no cirular dependcy with the type, all the alias are valid
 * @param type_name the name of the type to validate
 * @return status code
 **/
int proto_db_type_validate(const char* type_name);

/**
 * @brief get the common ancestor for the list of type names
 * @param type_name the list of type names we want to compute, it should ends with NULL pointer
 * @return the result type name or NULL on error case
 * @note if the buffer don't have enough space for the result, we will return an error code
 **/
const char* proto_db_common_ancestor(char const* const* type_name);

/**
 * @brief Get the type name string that managed by libproto
 * @param name The name value
 * @return The pointer
 **/
const char* proto_db_get_managed_name(const char* name);

#endif /* __PROTO_DB_H_ */
