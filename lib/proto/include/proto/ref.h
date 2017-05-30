/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief   The type references and name references
 * @details In the protocol type system, we need to make reference from a type to another. <br/>
 *          Originally we have two approach, first, we resolve all the reference at the time the protocol type get
 *          compiled. Another one, is we resolve all the references at the time when the protocol type databse is
 *          accessed. <br/>
 *          The first approach has an issue with updating the base types. <br/>
 *          So we need a group reference utilities to maintain, dump, load, access reference data.
 *  @file proto/include/proto/ref.h
 **/

#ifndef __PROTO_REF_H__
#define __PROTO_REF_H__

/**
 * @brief represent a name reference
 * @details The name references are the references that point to another field name. <br/>
 *         All the segments in a name reference are symbols rather than typenames.
 *         In addition the segment could be a subscript for an array. <br/>
 *         This concept is used for implementing name alias
 **/
typedef struct _proto_ref_nameref_t proto_ref_nameref_t;

/**
 * @brief indicates the type of the segment
 **/
typedef enum {
	PROTO_REF_NAMEREF_SEG_TYPE_ERR = ERROR_CODE(int),  /*!< represent error cases */
	PROTO_REF_NAMEREF_SEG_TYPE_SYM,                    /*!< represent this segment is a symbol */
	PROTO_REF_NAMEREF_SEG_TYPE_SUB                     /*!< represent this segment is a subscript */
} proto_ref_nameref_seg_type_t;

/**
 * @brief represent a type reference
 * @details a type reference is the reference to another type. This is used in serveral scenarios: <br/>
 *         1. Type inhertitance:    we use the type references indicates the base class <br/>
 *         2. Non-pritmive members: we use the type references indicates the type of the member <br/>
 *         Because we actually use the filesystem as the database, so the reference data would be a single string
 *         which is the path to the protocol type description file
 **/
typedef struct _proto_ref_typeref_t proto_ref_typeref_t;

/**
 * @brief create an empty name reference object
 * @param capacity the initial capacity of the name reference
 * @note  the name reference object is actually a self-adjusting buffer
 * @return the newly created reference object
 **/
proto_ref_nameref_t* proto_ref_nameref_new(uint32_t capacity);

/**
 * @brief dispose a used name reference object
 * @param ref the name reference object
 * @return status code
 **/
int proto_ref_nameref_free(proto_ref_nameref_t* ref);

/**
 * @brief get one segment from the name reference object
 * @param ref the name reference object
 * @param idx the index we want to poke
 * @param sym_buf the result buffer to be used when the target segment is a symbol segment
 * @param sub_buf the result buffer to be used when the target segment is a subscript segment
 * @note  if either sym_buf or sub_buf is NULL, means we do not expect that types of segment at this position, so
 *        the function will return error code.
 * @return the type of the segment, or error code otherwise
 **/
proto_ref_nameref_seg_type_t proto_ref_nameref_get(const proto_ref_nameref_t* ref, uint32_t idx, const char** sym_buf, uint32_t* sub_buf);

/**
 * @brief get the number of segments in the name reference
 * @param ref the name reference object
 * @return the number of segments or error code
 **/
uint32_t proto_ref_nameref_nsegs(const proto_ref_nameref_t* ref);

/**
 * @brief get the number of bytes is required for this name reference object *when dumpped to file*
 * @note actually we could use the pattern that the dump returns the number of bytes is written, but the problem is
 *       when we write the header, we haven't call the dump function yet, so we don't know what number to put until
 *       the dump functions gets called. This makes us have to rewind the file and correct the value, which makes things
 *       more complicated.
 * @param ref the name reference object
 * @return the number of bytes or error code
 **/
uint32_t proto_ref_nameref_size(const proto_ref_nameref_t* ref);

/**
 * @brief get the human-readable string representation of this name reference object
 * @param ref the name reference object
 * @param buf the buffer should used
 * @param sz the size of the buffer
 * @return the result string
 **/
const char* proto_ref_nameref_string(const proto_ref_nameref_t* ref, char* buf, size_t sz);

/**
 * @brief append a symbol segment to the name reference object
 * @details Unlike the normal version, which accept a NULL terminated string, this version accept a
 *          pair of pointer and the string is considered to be [begin, end)
 * @param ref the reference object
 * @param begin the begin of the symbol memory
 * @param end the end of the symbol memory
 * @return status code
 **/
int proto_ref_nameref_append_symbol_range(proto_ref_nameref_t* ref, const char* begin, const char* end);

    /**
 * @brief append a symbol segment to the name reference object
 * @param ref the name reference object
 * @param symbol the symbol we want to append
 * @return status code
 **/
int proto_ref_nameref_append_symbol(proto_ref_nameref_t* ref, const char* symbol);

/**
 * @brief append a subscript segment to the name reference object
 * @param ref the name reference object
 * @param subscript the subscript we want to append
 * @return status code
 **/
int proto_ref_nameref_append_subscript(proto_ref_nameref_t* ref, uint32_t subscript);

/**
 * @brief dump the name reference object to the given opened file
 * @param ref  the name reference object
 * @param fp the target file
 * @return status code
 **/
int proto_ref_nameref_dump(const proto_ref_nameref_t* ref, FILE* fp);

/**
 * @brief load the name reference object from the given opened file
 * @param fp the file to read
 * @param size the size in file of the object
 * @return the pointer for the newly created object, or error code
 * @note this function will allocate the object on heap, use proto_ref_nameref_free
 **/
proto_ref_nameref_t* proto_ref_nameref_load(FILE* fp, uint32_t size);

/**
 * @brief create a empty type reference object
 * @param capacity the initial capacity of the path buffer in bytes
 * @return the newly created type reference object or NULL on error case
 **/
proto_ref_typeref_t* proto_ref_typeref_new(uint32_t capacity);

/**
 * @brief dispose a used type reference object
 * @param ref the type reference object to dispose
 * @return status code
 **/
int proto_ref_typeref_free(proto_ref_typeref_t* ref);

/**
 * @brief append a segment to the type reference object
 * @param ref the type reference object
 * @param segment the new segment text
 * @return status code
 **/
int proto_ref_typeref_append(proto_ref_typeref_t* ref, const char* segment);

/**
 * @brief get the size in bytes of the type name
 * @param ref the type reference object
 * @return the size or error code
 **/
uint32_t proto_ref_typeref_size(const proto_ref_typeref_t* ref);

/**
 * @brief get the underlying path name to the protocol description file
 * @param ref the type reference object
 * @return the path from this type reference, or NULL when error happens
 **/
const char* proto_ref_typeref_get_path(const proto_ref_typeref_t* ref);

/**
 * @brief dump the type reference object to the given file
 * @param ref the type reference object
 * @param fp the file pointer
 * @return status code
 **/
int proto_ref_typeref_dump(const proto_ref_typeref_t* ref, FILE* fp);

/**
 * @brief load the type reference object from given file
 * @param fp the file pointer
 * @param size the size of the data segment in the file
 * @return the newly created type reference object, or NULL on error case
 **/
proto_ref_typeref_t* proto_ref_typeref_load(FILE* fp, uint32_t size);

#endif /** __PROTO_REF_H__ */
