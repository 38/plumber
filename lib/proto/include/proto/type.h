/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief Define a protocol type
 * @details The protocol type is the data structure that used to describe the memory layout of a
 *         type. It allows name alias, inheritance, primitive types and paddings. <br/>
 *         The protocol type can be load/dump from a file. And this is used as the basic data structure
 *         of the protocol system.
 * @file proto/include/proto/type.h
 **/

#ifndef __PROTO_TYPE_H__
#define __PROTO_TYPE_H__

/**
 * @brief the actual protocol type memory layout definition
 * @note this struct won't validate the inherit reference (If the target is exist and a name of type) <br/>
 *       This is guareented when the type installation stage, and the protocol type to install will be placed in
 *       a sandbox and validate all the type references. <br/>
 *       The protocol type descrition is only an abstraction of the semantics of the Protocol Type Description Language
 *       and all the validation work will happen in the install stage.
 **/
typedef struct _proto_type_t proto_type_t;

/**
 * @brief the enum for the type description
 **/
typedef enum {
	PROTO_TYPE_ENTITY_REF_NONE,   /*!< indicates there's no data in the reference section */
	PROTO_TYPE_ENTITY_REF_TYPE,   /*!< the reference to another type */
	PROTO_TYPE_ENTITY_REF_NAME    /*!< the reference to another name */
} proto_type_entity_ref_type_t;

/**
 * @brief The additional metadata for this entity
 * @note This is necessary, because sometimes we want to validate the type at runtime, or
 *       we want to provide a initial value for the type, then we need use the metadata
 **/
typedef struct __attribute__((packed)) {
	uint8_t             size;                  /*!< The size of the metadata section, it must be the size of this struct */
	union {
		uint32_t        plain;                 /*!< The plain value */
		struct __attribute__((packed)) {
			uint32_t    valid:1;               /*!< Indicates if this is a scope token object, which must be 1  */
			uint32_t    primitive:1;           /*!< Indicates if this is a scope primitvie, for example, string, which can be used accross the machine boarder */
			uint32_t    typename_size:30;      /*!< The scope identifier length */
		}               scope;                 /*!< The flags used for the scope token object */
		struct __attribute__((packed)) {
			uint32_t    invalid:1    ;         /*!< If this is a scope token, which must be 0 */
			uint32_t    is_signed:1;           /*!< If this type is a signed number */
			uint32_t    is_real:1;             /*!< If this type is a real number */
			uint32_t    default_size:29;       /*!< The size of the default value */
		}               numeric;               /*!< The primitive flags */
	} flags;                                   /*!< The metadata flags */
	uint8_t             header_end[0];         /*!< The address of the end of header */
	union {
		char*           scope_typename;        /*!< The identifer */
		void*           numeric_default;       /*!< The default value */
	};
} proto_type_atomic_metadata_t;


/**
 * @brief represent an entity in a protocol, a protocol is represented as a series of
 *        protocol entities. And the protocol entities is sorted in the memory layout
 *        order <br/>
 *        The reference data is right after the symbol section.
 * @details When the entity is a inheritence entity use                    [type = TYPE, count = 1, elem_size = 0, symlen = 0, reflen > ?] <br/>
 *         When the entity is a field with another non-pritimive type use  [type = TYPE, count > 0, elem_size = 0, symlen > 0, reflen > 0] <br/>
 *         When the entity is a pritmive data use                          [type = NONE, count > 0, elem_size > 0  symlen > 0, reflen = 0] <br/>
 *         When the entity is a padding use                                [type = NONE, count > 0, elem_size = 1, symlen = 0, reflen = 0] <br/>
 *         When the entity is a name alias                                 [type = NAME, count = 0, elem_size = 0, symlen > 0, reflen > 0] <br/>
 *         When the entity is a constant                                   [type = NONE, count = 0, elem_size = 0, symlen = 0, reflen = 0] <br/>
 **/
typedef struct __attribute__((packed)) {
	uint32_t                      symlen:31; /*!< the length of the symbol */
	uint32_t                      metadata:1;/*!< if this is type has a metadata section */
	proto_type_entity_ref_type_t  refkind:3; /*!< the kind of the reference */
	uint32_t                      reflen:29; /*!< the length of the symbol that is referring */
	uint32_t                      dimlen;    /*!< the dimension data length */
	uint32_t                      elem_size; /*!< the size of each element */
} proto_type_entity_info_t;

/**
 * @brief represent one entity in the protocol type, this is the in-memory version <br/>
 *        The in-file version has muliple variable sizes, so we won't use a C struct to
 *        represent that. In file storage:<br/>
 *        -------------------------------------------------<br/>
 *        |  Header  |  Dimension  |  Symbol  | Reference |<br/>
 *        -------------------------------------------------<br/>
 *        The symbol and reference section do not contains the ending 0
 *        because in the header we already have this information <br/>
 * @note  In this struct the dimension, symbol and reference data are concidered different memory allocation
 **/
typedef struct {
	proto_type_entity_info_t      header;    /*!< the fixed length header */
	uint32_t*                     dimension; /*!< the dimensional data */
	char*                         symbol;    /*!< the field name of this entity */
	proto_type_atomic_metadata_t* metadata;  /*!< The metadata for this entity if it's an atomic entity */
	union {
		proto_ref_nameref_t*     name_ref;  /*!< the pointer to the name reference data */
		proto_ref_typeref_t*     type_ref;  /*!< the pointer to the type reference data */
	};
} proto_type_entity_t;


/**
 * @brief create a new protocol type definition
 * @param cap the initial capacity of the protocol type table
 * @param base_type the base type is the type that the new type will inherit from
 * @param padding_size the size of the byte padding, it should be 0 for no padding, 4 for 32bit padding, 8 for 64 bit padding
 * @note  this function will assume all the referring type are also have padding in the same way. <br/>
 *        If this requirement doesn't meet at runtime, the system will add extra bytes make sure it's padded prefectly. <br/>
 *        As all the functions take the reference object, claims the ownership of the param, and no need for dispose the
 *        reference object outside of this function<br/>
 *        If the function fails, the onwership of the refreence object will not be transferred, so the caller should dispose the object
 * @return the newly created protocol type or NULL on error cases
 **/
proto_type_t* proto_type_new(size_t cap, proto_ref_typeref_t* base_type, uint32_t padding_size);

/**
 * @brief dispose a used protocol type definition
 * @param proto the protocol type to dispose
 * @return status code
 **/
int proto_type_free(proto_type_t* proto);

/**
 * @brief load a protocol type from the protocol file
 * @param filename the file name of the protocol file
 * @return the loaded protocol type definition
 **/
proto_type_t* proto_type_load(const char* filename);

/**
 * @brief dump a protocol type to the filename
 * @param proto the protocol type description to dump
 * @param filename the target filename to dump
 * @return status code
 **/
int proto_type_dump(const proto_type_t* proto, const char* filename);

/**
 * @brief append an atomic type to the protocol type descrition
 * @note this function is used for both array and scalar. <br/>
 *       The type system do not distinguish the difference between a[1] and a.
 *       Because the offset is actually the same
 * @param proto the protocol type description
 * @param symbol the field name
 * @param elem_size the element size
 * @param dim the dimensional data
 * @param metadata The metadata we should put in this atomic
 * @return status code
 **/
int proto_type_append_atomic(proto_type_t* proto, const char* symbol, uint32_t elem_size, const uint32_t* dim, const proto_type_atomic_metadata_t* metadata);

/**
 * @brief append a non-primitive type to the type description
 * @note the similar desgin as proto_type_append_atomic also applies
 * @param proto the protocol type description
 * @param symbol the field name
 * @param type the typename of the filed
 * @param dim the dimensional data
 * @todo make type a reference buffer
 * @return status code
 * @note the type reference object will be handed off to the protocol type description object, and it will
 *       be disposed at the time the protocol type description object gets disposed
 **/
int proto_type_append_compound(proto_type_t* proto, const char* symbol, const uint32_t* dim, proto_ref_typeref_t* type);

/**
 * @brief append a name alias to the protocol type description
 * @param proto the protocol type description to be appended
 * @param symbol the symbol name for this alias
 * @param target the target symbol name. This can be the field of another field or even subscription <br/>
 *        For example, it can be "some_other_name" when the alias is point to the field in the same protocol type <br/>
 *        It also can be "some_other_name.some_field_name.some_filed_name" for the field of a field <br/>
 *        It also can be "some_other_name[x].field_name" for an array type. <br/>
 *        Because it can not validate the target at the time we compile the single type description, so the
 *        validation will be happen when the installation is going on.
 * @note the type reference object will be handed off to the protocol type description object, and it will
 *       be disposed at the time the protocol type description object gets disposed
 * @todo make target a reference buffer
 * @return status code
 **/
int proto_type_append_alias(proto_type_t* proto, const char* symbol, proto_ref_nameref_t* target);

/**
 * @brief get the size of protocol type (In numbers of entity)
 * @param proto the protocol type description
 * @return the size of the protocol type or error code
 **/
uint32_t proto_type_get_size(const proto_type_t* proto);

/**
 * @brief get the type of the padding boundary
 * @param proto the protocol type description
 * @return the size of the padding boundary or error code
 **/
uint32_t proto_type_get_padding_size(const proto_type_t* proto);

/**
 * @brief get the protocol type entity from a protocol type description at index idx
 * @param proto the target protocol type description
 * @param idx the index
 * @return the protocol type entity or NULL on error cases
 **/
const proto_type_entity_t* proto_type_get_entity(const proto_type_t* proto, uint32_t idx);

/**
 * @brief get the human-readable entity string of the entity
 * @param entity the entity
 * @param buf the buffer we use
 * @param size the size of the buffer
 * @return the string representation of the entity
 **/
const char* proto_type_entity_str(const proto_type_entity_t* entity, char* buf, size_t size);

#endif /*__PROTO_TYPE_H__*/
