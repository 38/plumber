/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <proto/err.h>
#include <proto/ref.h>
#include <proto/type.h>

/**
 * @brief the file header for a protocol type description file
 **/
typedef union __attribute__((packed)) {
	uint64_t u64;    /*!< peek the header as a uint64_t */
	uint8_t  u8[8];  /*!< peek the header as an array */
} _file_header_t;

/**
 * @brief the header identifer for the protocol type description file
 **/
const _file_header_t _magic = {
	.u8 = {'p', 'r', 'o', 't', 'o', 0xa2, 0xe3, 0xd4}
};

/**
 * @brief The actual data structure for a protocol type description
 **/
struct _proto_type_t {
	uint32_t              capacity;        /*!< the capacity of this entity table */
	uint32_t              padding_size;    /*!< the padding boundary size of the protocol type */
	uint32_t              entity_count;    /*!< the number of entities in the symbol table */
	proto_type_entity_t*  entity_table;    /*!< the entity table for this type description */
};
/**
 * @brief make sure that the entity table have one more space for the next entity
 * @note this function won't check the validity of proto
 * @param proto the protocol type description to ensure
 * @return status code
 **/
static inline int _ensure_space(proto_type_t* proto)
{
	if(proto->capacity > proto->entity_count) return 0;

	proto_type_entity_t* new_table = (proto_type_entity_t*)realloc(proto->entity_table, sizeof(proto_type_entity_t) * proto->capacity * 2);
	if(NULL == new_table)
	    PROTO_ERR_RAISE_RETURN(int, ALLOC);

	proto->capacity *= 2;
	proto->entity_table = new_table;

	return 0;
}

/**
 * @brief dispose a given reference object
 * @note this function is not the function disposes the entire struct, instread
 *       it just dispose the reference object owned by this ref struct
 * @param ref the reference to dispose
 * @return status code
 **/
static inline int _clean_ref(proto_type_entity_t* ref)
{
	int rc = 0;
	switch(ref->header.refkind)
	{
		case PROTO_TYPE_ENTITY_REF_TYPE:
		    rc = proto_ref_typeref_free(ref->type_ref);
		    break;
		case PROTO_TYPE_ENTITY_REF_NAME:
		    rc = proto_ref_nameref_free(ref->name_ref);
		    break;
		case PROTO_TYPE_ENTITY_REF_NONE:
		    break;
		default:
		    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);
	}

	if(rc != ERROR_CODE(int))
	{
		ref->type_ref = NULL;
		ref->name_ref = NULL;
		ref->header.refkind = PROTO_TYPE_ENTITY_REF_NONE;
	}

	return rc;
}

/**
 * @biref Duplicate the metadata
 * @param metadata The metadata to duplicate
 * @return The duplicated metadata
 **/
static inline proto_type_atomic_metadata_t* _dup_metadata(const proto_type_atomic_metadata_t* metadata)
{
	if(NULL == metadata) PROTO_ERR_RAISE_RETURN_PTR(BUG);
	proto_type_atomic_metadata_t* ret = (proto_type_atomic_metadata_t*)malloc(sizeof(*ret));

	if(NULL == ret) PROTO_ERR_RAISE_RETURN_PTR(ALLOC);

	ret->scope_typename = NULL;
	ret->numeric_default = NULL;

	ret->size = (uint8_t)(uintptr_t)((proto_type_atomic_metadata_t*)NULL)->header_end;

	if(metadata->flags.scope.valid)
	{
		ret->flags.scope = metadata->flags.scope;
		if(ret->flags.scope.typename_size > 0 && NULL == (ret->scope_typename = (char*)malloc(ret->flags.scope.typename_size + 1u)))
		    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);
		if(ret->flags.scope.typename_size > 0)
		{
			memcpy(ret->scope_typename, metadata->scope_typename, ret->flags.scope.typename_size);
			ret->scope_typename[ret->flags.scope.typename_size] = 0;
		}
		return ret;
	}
	else
	{
		ret->flags.numeric = metadata->flags.numeric;

		if(ret->flags.numeric.default_size > 0 && NULL == (ret->numeric_default = malloc(ret->flags.numeric.default_size)))
		    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);
		if(ret->flags.numeric.default_size > 0)
		    memcpy(ret->numeric_default, metadata->numeric_default, ret->flags.numeric.default_size);
		return ret;
	}
ERR:
	free(ret);
	return NULL;
}

/**
 * @brief Load the metadata from the file
 * @param fp The file to load
 * @return The newly created metadata object
 **/
static inline proto_type_atomic_metadata_t* _load_metadata(FILE* fp)
{
	size_t nbytes = (size_t)((proto_type_atomic_metadata_t*)NULL)->header_end;

	proto_type_atomic_metadata_t* ret = (proto_type_atomic_metadata_t*)malloc(sizeof(*ret));
	if(NULL == ret)
	    PROTO_ERR_RAISE_RETURN_PTR(ALLOC);

	ret->numeric_default = NULL;
	ret->scope_typename  = NULL;

	if(1 != fread(ret, nbytes, 1, fp))
	    PROTO_ERR_RAISE_GOTO(ERR, FORMAT);

	if(ret->size > nbytes)
	{
		char dropped[ret->size - nbytes];
		if(1 != fread(dropped, ret->size - nbytes, 1, fp))
		    PROTO_ERR_RAISE_GOTO(ERR, FORMAT);
	}

	if(ret->flags.scope.valid && (ret->flags.scope.typename_size == 0 ||  NULL != (ret->scope_typename = (char*)malloc(ret->flags.scope.typename_size + 1u))))
	{
		if(ret->scope_typename != NULL && fread(ret->scope_typename, ret->flags.scope.typename_size, 1, fp) != 1)
		    PROTO_ERR_RAISE_GOTO(ERR, FORMAT);
		if(ret->scope_typename != NULL) ret->scope_typename[ret->flags.scope.typename_size] = 0;
		return ret;
	}
	else if(!ret->flags.numeric.invalid && (ret->flags.numeric.default_size == 0 || NULL != (ret->numeric_default = malloc(ret->flags.numeric.default_size))))
	{
		if(ret->numeric_default != NULL && fread(ret->numeric_default, ret->flags.numeric.default_size, 1, fp) != 1)
		    PROTO_ERR_RAISE_GOTO(ERR, FORMAT);
		return ret;
	}

	PROTO_ERR_RAISE_GOTO(ERR, ALLOC);

ERR:
	if(NULL != ret)
	{
		if(NULL != ret->scope_typename)
		{
			free(ret->scope_typename);
			ret->scope_typename = NULL;
		}
		if(NULL != ret->numeric_default)
		{
			free(ret->numeric_default);
			ret->numeric_default = NULL;
		}
		free(ret);
	}
	return NULL;
}

/**
 * @brief dump the metadata section to the prototype file
 * @param fp The file pointer
 * @param metadata The metadata to dump
 * @return status code
 **/
static inline int _dump_metadata(FILE* fp, const proto_type_atomic_metadata_t* metadata)
{
	size_t nbytes = (size_t)((proto_type_atomic_metadata_t*)NULL)->header_end;

	if(1 != fwrite(metadata, nbytes, 1, fp))
	    PROTO_ERR_RAISE_RETURN(int,  WRITE);

	if(metadata->flags.scope.valid && NULL != metadata->scope_typename)
	{
		if(metadata->flags.scope.typename_size > 0 && 1 != fwrite(metadata->scope_typename, metadata->flags.scope.typename_size, 1, fp))
		    PROTO_ERR_RAISE_RETURN(int, WRITE);
	}
	else if(!metadata->flags.numeric.invalid && NULL != metadata->numeric_default)
	{
		if(metadata->flags.numeric.default_size > 0 && 1 != fwrite(metadata->numeric_default, metadata->flags.numeric.default_size, 1, fp))
		    PROTO_ERR_RAISE_RETURN(int, WRITE);
	}

	return 0;
}


/**
 * @brief dispose a used metadata
 * @param metadata The metadata we want to dispose
 * @return status code
 **/
static int _free_metadata(proto_type_atomic_metadata_t* metadata)
{
	if(metadata->flags.scope.valid)
	{
		if(NULL != metadata->scope_typename) free(metadata->scope_typename);
	}
	else
	{
		if(NULL != metadata->numeric_default) free(metadata->numeric_default);
	}
	free(metadata);

	return 0;
}

/**
 * @brief append an entity to the proto table
 * @note this function will assume all the param combinition are valid and do not check if it's meaningful
 * @param elem_size the size for each element
 * @param proto the protocol type description to append
 * @param symbol the name of the field, NULL if not apply
 * @param dimension the dimensional data terminates with 0
 * @param name_ref the name reference, if there's no name reference pass NULL
 * @param type_ref the type reference, if there's no type reference pass NULL
 * @param metadata The metadata for the atomic type, which means the nameref is NULL
 * @note  the dimension data contains at least one element, if this param is NULL, just use {1} as default dimension data <br/>
 *        For the name_ref and type_ref, if the non-NULL pointer passed in, this function will take the ownership of the pointer,
 *        which means the caller do not need to dispose the pointer anymore. And the pointer will be disposed once the protocol type
 *        description object gets disposed.
 * @return status code
 **/
static inline int _append_entity(proto_type_t* proto,
                                 uint32_t elem_size, const uint32_t* dimension,
                                 const char* symbol, proto_ref_nameref_t* name_ref,
                                 proto_ref_typeref_t* type_ref,
                                 const proto_type_atomic_metadata_t* metadata)
{
	/* See the documentation, an entity have both name_ref and type_ref is not allowed anyway */
	if(name_ref != NULL && type_ref != NULL)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(_ensure_space(proto) == ERROR_CODE(int))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	static const uint32_t default_dim[] = {1, 0};

	proto_type_entity_t* entity = proto->entity_table + proto->entity_count;
	entity->symbol = NULL;
	entity->dimension = NULL;
	entity->metadata = NULL;

	entity->header.elem_size = elem_size;
	entity->header.metadata = (metadata != NULL);

	if(NULL != symbol)
	{
		entity->header.symlen = strlen(symbol)&0x7fffffff;
		if(NULL == (entity->symbol = (char*)malloc(entity->header.symlen + 1u)))
		    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);
		memcpy(entity->symbol, symbol, entity->header.symlen + 1u);
	}
	else entity->header.symlen = 0;

	entity->header.reflen = 0;
	entity->header.refkind = PROTO_TYPE_ENTITY_REF_NONE;

	if(NULL != name_ref)
	{
		entity->header.reflen = proto_ref_nameref_size(name_ref) & ((1u << 29) - 1);
		entity->header.refkind = PROTO_TYPE_ENTITY_REF_NAME;
		entity->header.metadata = 0;
		entity->name_ref = name_ref;
	}
	else if(NULL != type_ref)
	{
		entity->header.reflen = proto_ref_typeref_size(type_ref) & ((1u << 29) - 1);
		entity->header.refkind = PROTO_TYPE_ENTITY_REF_TYPE;
		entity->header.metadata = (metadata != NULL);
		entity->type_ref = type_ref;
	}

	if(NULL == dimension) dimension = default_dim;

	for(entity->header.dimlen = 0; dimension[entity->header.dimlen]; entity->header.dimlen ++);
	if(NULL == (entity->dimension = (uint32_t*)malloc(sizeof(uint32_t) * entity->header.dimlen)))
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);
	memcpy(entity->dimension, dimension, entity->header.dimlen * sizeof(uint32_t));

	if(entity->header.metadata)
	{
		if(NULL == (entity->metadata = _dup_metadata(metadata)))
		    PROTO_ERR_RAISE_GOTO(ERR, FAIL);
	}
	else entity->metadata = NULL;

	proto->entity_count ++;

	return 0;
ERR:
	if(NULL != entity)
	{
		if(NULL != entity->symbol)    free(entity->symbol);
		if(NULL != entity->dimension) free(entity->dimension);
		if(NULL != entity->metadata) _free_metadata(entity->metadata);
	}

	return ERROR_CODE(int);
}

proto_type_t* proto_type_new(size_t cap, proto_ref_typeref_t* base_type, uint32_t padding_size)
{
	if((padding_size != 0 && padding_size != 4 && padding_size != 8))
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	if(cap == 0) cap = 1;
	proto_type_t* ret = (proto_type_t*)malloc(sizeof(*ret) * cap);
	if(NULL == ret)
	    PROTO_ERR_RAISE_RETURN_PTR(ALLOC);

	if(NULL == (ret->entity_table = (proto_type_entity_t*)malloc(sizeof(proto_type_entity_t) * cap)))
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);

	ret->capacity = (uint32_t)cap;
	ret->padding_size = padding_size;
	ret->entity_count = 0;

	if(NULL != base_type && ERROR_CODE(int) == _append_entity(ret, 0, NULL, NULL, NULL, base_type, 0))
	    PROTO_ERR_RAISE_GOTO(ERR, FAIL);

	return ret;
ERR:

	if(NULL != ret)
	{
		if(NULL != ret->entity_table)
		{
			uint32_t i;
			for(i = 0; i < ret->entity_count; i ++)
			{
				if(ret->entity_table[i].symbol != NULL)
				    free(ret->entity_table[i].symbol);
				if(ret->entity_table[i].dimension != NULL)
				    free(ret->entity_table[i].dimension);
				if(ret->entity_table[i].metadata != NULL)
				    _free_metadata(ret->entity_table[i].metadata);
			}
			free(ret->entity_table);
		}
		free(ret);
	}

	return NULL;
}

int proto_type_free(proto_type_t* proto)
{
	int rc = 0;
	if(NULL == proto)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(NULL != proto->entity_table)
	{
		uint32_t i;
		for(i = 0; i < proto->entity_count; i ++)
		{
			proto_type_entity_t* entity = proto->entity_table + i;
			rc = _clean_ref(entity);
			if(entity->symbol != NULL)
			    free(entity->symbol);
			if(entity->dimension != NULL)
			    free(entity->dimension);
			if(entity->metadata != NULL && ERROR_CODE(int) == _free_metadata(entity->metadata))
			    rc = ERROR_CODE(int);
		}
		free(proto->entity_table);
	}

	free(proto);

	return rc;
}

/**
 * @brief read an single entity from the protocol type description file
 * @param fp the file pointer
 * @param buf the buffer for the result
 * @note this function will assume that the file is already seeked to the position where
 *       next entity begins <br/>
 * @return status code
 **/
int _entity_load(FILE* fp, proto_type_entity_t* buf)
{
	if(1 != fread(&buf->header, sizeof(proto_type_entity_info_t), 1, fp))
	    PROTO_ERR_RAISE_RETURN(int, READ);

	uint32_t* dim = buf->dimension = NULL;
	char*     sym = buf->symbol    = NULL;
	buf->metadata = NULL;

	if(buf->header.dimlen > 0)
	{
		size_t bufsize = sizeof(uint32_t) * buf->header.dimlen;
		if(NULL == (buf->dimension = dim = (uint32_t*)malloc(bufsize)))
		    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);
		if(1 != fread(dim, bufsize, 1, fp))
		    PROTO_ERR_RAISE_GOTO(ERR, READ);
	}

	if(buf->header.symlen > 0)
	{
		size_t bufsize = buf->header.symlen;
		if(NULL == (buf->symbol = sym = (char*)malloc(bufsize + 1)))
		    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);
		if(1 != fread(sym, bufsize, 1, fp))
		    PROTO_ERR_RAISE_GOTO(ERR, READ);
		sym[bufsize] = 0;
	}

	if(buf->header.reflen > 0)
	{
		uint32_t len = buf->header.reflen;

		switch(buf->header.refkind)
		{
			case PROTO_TYPE_ENTITY_REF_TYPE:
			    if(NULL == (buf->type_ref = proto_ref_typeref_load(fp, len)))
			        PROTO_ERR_RAISE_GOTO(ERR, FAIL);
			    break;
			case PROTO_TYPE_ENTITY_REF_NAME:
			       if(NULL == (buf->name_ref = proto_ref_nameref_load(fp, len)))
			        PROTO_ERR_RAISE_GOTO(ERR, FAIL);
			    break;
			default:
			    PROTO_ERR_RAISE_GOTO(ERR, FORMAT);
		}
	}

	if(buf->header.metadata)
	{
		if(NULL == (buf->metadata = _load_metadata(fp)))
		    PROTO_ERR_RAISE_GOTO(ERR, FAIL);
	}

	return 0;

ERR:
	if(NULL != dim) free(dim);
	if(NULL != sym) free(sym);
	if(NULL != buf->metadata) _free_metadata(buf->metadata);
	_clean_ref(buf);
	return ERROR_CODE(int);
}

proto_type_t* proto_type_load(const char* filename)
{
	if(NULL == filename)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	FILE* fp = fopen(filename, "rb");
	if(NULL == fp)
	    PROTO_ERR_RAISE_RETURN_PTR(OPEN);

	_file_header_t header;
	proto_type_t* ret = NULL;
	uint32_t padding_size;
	uint32_t entity_count;

	if(fread(&header, sizeof(_file_header_t), 1, fp) != 1)
	    PROTO_ERR_RAISE_GOTO(ERR, READ);

	if(header.u64 != _magic.u64)
	    PROTO_ERR_RAISE_GOTO(ERR, FORMAT);

	if(fread(&padding_size, sizeof(padding_size), 1, fp) != 1)
	    PROTO_ERR_RAISE_GOTO(ERR, READ);

	if(fread(&entity_count, sizeof(entity_count), 1, fp) != 1)
	    PROTO_ERR_RAISE_GOTO(ERR, READ);

	ret = proto_type_new(entity_count, NULL, padding_size);
	if(NULL == ret)
	    PROTO_ERR_RAISE_GOTO(ERR, FAIL);

	ret->capacity = entity_count;

	for(ret->entity_count = 0; ret->entity_count < entity_count; ret->entity_count ++)
	    if(ERROR_CODE(int) ==  _entity_load(fp, ret->entity_table + ret->entity_count))
	        PROTO_ERR_RAISE_GOTO(ERR, FAIL);

	fclose(fp);
	return ret;
ERR:

	if(NULL != ret) proto_type_free(ret);
	if(NULL != fp) fclose(fp);

	return NULL;
}

int proto_type_dump(const proto_type_t* proto, const char* filename)
{
	int ret = ERROR_CODE(int);

	if(NULL == filename || NULL == proto)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	FILE* fp = fopen(filename, "wb");
	if(NULL == fp)
	    PROTO_ERR_RAISE_RETURN(int, OPEN);

	if(1 != fwrite(&_magic, sizeof(_file_header_t), 1, fp))
	    PROTO_ERR_RAISE_RETURN(int, WRITE);

	if(1 != fwrite(&proto->padding_size, sizeof(uint32_t), 1, fp))
	    PROTO_ERR_RAISE_GOTO(RET, WRITE);

	if(1 != fwrite(&proto->entity_count, sizeof(uint32_t), 1, fp))
	    PROTO_ERR_RAISE_GOTO(RET, WRITE);

	uint32_t i;
	for(i = 0; i < proto->entity_count; i ++)
	{
		const proto_type_entity_t* current = proto->entity_table + i;

		if(1 != fwrite(&current->header, sizeof(proto_type_entity_info_t), 1, fp))
		    PROTO_ERR_RAISE_GOTO(RET, WRITE);

		if(NULL != current->dimension && 1 != fwrite(current->dimension, sizeof(uint32_t) * current->header.dimlen, 1, fp))
		    PROTO_ERR_RAISE_GOTO(RET, WRITE);

		if(NULL != current->symbol && 1 != fwrite(current->symbol, current->header.symlen, 1, fp))
		    PROTO_ERR_RAISE_GOTO(RET, WRITE);

		switch(current->header.refkind)
		{
			case PROTO_TYPE_ENTITY_REF_NAME:
			    if(ERROR_CODE(int) == proto_ref_nameref_dump(current->name_ref, fp))
			        PROTO_ERR_RAISE_GOTO(RET, FAIL);
			    break;
			case PROTO_TYPE_ENTITY_REF_TYPE:
			    if(ERROR_CODE(int) == proto_ref_typeref_dump(current->type_ref, fp))
			        PROTO_ERR_RAISE_GOTO(RET, FAIL);
			case PROTO_TYPE_ENTITY_REF_NONE:
			    break;
		}

		if(current->header.metadata && ERROR_CODE(int) == _dump_metadata(fp, current->metadata))
		    PROTO_ERR_RAISE_GOTO(RET, FAIL);
	}

	ret = 0;
RET:
	if(NULL != fp) fclose(fp);
	return ret;
}

/**
 * @brief get the size of this protocol type in bytes
 * @param proto the protocol type description
 * @note not validate arguments <br/>
 *       This function doesn't actually compute the size of the protocol, because in this stage
 *       We actually do not have the information about all it's references.
 *       However, we can assume all it's dependencies are padded in the same way. <br/>
 *       Of course, this is not necessarily true. So, when the time we construct the actual offset
 *       of the field, we may need to have additional padding after the reference types <br/>
 *       For example, type A has a padding size of 8 and type B has a padding size of 4 <br/>
 *       And A.b has type B (assume sizeof(B) = 20). <br/>
 *       The type description of A have no way to know the size of B. So it assume the sizeof B is
 *       muliply of 8, however, this is not true. So when the querying the offset of A, there should be
 *       a padding of 4 bytes after A.b, so that we make sure the padding rule doesn't break.  <br/>
 *       The unknown referring type is assumed as a size of padding_size in fact
 *       So at this point, we assume all the references are padded in the same way.
 * @return the size of the protocol type description or error code
 **/
static inline uint32_t _proto_padding_offset(const proto_type_t* proto)
{
	if(proto->padding_size == 0) return 0;

	uint32_t ret = 0;

	uint32_t i;
	for(i = 0; i < proto->entity_count; i ++)
	{
		const proto_type_entity_t * ent = proto->entity_table + i;
		/* Because all the reference types have proto->elem_size = 0, so it doesn't affect the padding */
		uint32_t delta = ent->header.elem_size;
		uint32_t i;
		for(i = 0; i < ent->header.dimlen; i ++)
		    delta *= ent->dimension[i];

		ret = (ret + delta) % proto->padding_size;
	}

	return ret;
}

/**
 * @brief append padding bytes to the end of the protocol type if it's needed
 * @note When should we padding ? <br/>
 *       1. If the next_size is smaller than the padding size, we need to make sure
 *          the data won't accross the padding boundary<br/>
 *       2. If the next_size is larger than padding, we need to make sure the data begins
 *          at the begining of the padding boundary <br/>
 * @param proto the protocol type description
 * @param next_size the size of data we added
 * @return status code
 **/
static inline int _append_padding(proto_type_t* proto, uint32_t next_size)
{
	uint32_t offset = _proto_padding_offset(proto);

	if(ERROR_CODE(uint32_t) == offset)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	/* If there's in the boundary, no need to pad */
	if(offset == 0) return 0;

	/* If even append the new entity right after current end, it won't touch the boundary */
	if(offset + next_size <= proto->padding_size)
	    return 0;

	uint32_t bytes_to_pad = (proto->padding_size - offset) % proto->padding_size;

	if(ERROR_CODE(int) == _append_entity(proto, bytes_to_pad, NULL, NULL, NULL, NULL, 0))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return 0;
}

int proto_type_append_atomic(proto_type_t* proto, const char* symbol, uint32_t elem_size, const uint32_t* dim, const proto_type_atomic_metadata_t* metadata)
{
	if(NULL == proto || NULL == symbol)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	uint32_t size = elem_size, i;

	if(dim != NULL)
	    for(i = 0; dim[i] != 0; i ++)
	        size *= dim[i];

	if(ERROR_CODE(int) == _append_padding(proto, size))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(ERROR_CODE(int) == _append_entity(proto, elem_size, dim, symbol, NULL, NULL, metadata))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return 0;
}

int proto_type_append_compound(proto_type_t* proto, const char* symbol, const uint32_t* dim, proto_ref_typeref_t* type)
{
	if(NULL == proto || NULL == symbol || NULL == type)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(ERROR_CODE(int) == _append_padding(proto, proto->padding_size))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(ERROR_CODE(int) == _append_entity(proto, 0, dim, symbol, NULL, type, NULL))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return 0;
}

int proto_type_append_alias(proto_type_t* proto, const char* symbol, proto_ref_nameref_t* target)
{
	if(NULL == proto || NULL == symbol || NULL == target)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(ERROR_CODE(int) == _append_entity(proto, 0, NULL, symbol, target, NULL, NULL))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return 0;
}

uint32_t proto_type_get_size(const proto_type_t* proto)
{
	if(NULL == proto)
	    PROTO_ERR_RAISE_RETURN(uint32_t, ARGUMENT);
	return proto->entity_count;
}

uint32_t proto_type_get_padding_size(const proto_type_t* proto)
{
	if(NULL == proto)
	    PROTO_ERR_RAISE_RETURN(uint32_t, ARGUMENT);

	return proto->padding_size;
}

const proto_type_entity_t* proto_type_get_entity(const proto_type_t* proto, uint32_t idx)
{
	if(NULL == proto || idx >= proto->entity_count)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	return proto->entity_table + idx;
}

const char* proto_type_entity_str(const proto_type_entity_t* entity, char* buf, size_t size)
{
	if(NULL == entity || NULL == buf)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	if(entity->header.refkind == PROTO_TYPE_ENTITY_REF_NONE && entity->header.reflen == 0)
	{
		if(entity->header.symlen > 0)
		{
			snprintf(buf, size, "Field `%s', element size %u", entity->symbol, entity->header.elem_size);
			return buf;
		}
		else
		{
			snprintf(buf, size, "Padding %u bytes", entity->header.elem_size * entity->dimension[0]);
			return buf;
		}
	}
	else if(entity->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE && entity->header.reflen > 0)
	{
		if(entity->header.symlen > 0)
		{
			uint32_t count = 1, i;
			for(i = 0; i < entity->header.dimlen; i ++)
			    count *= entity->dimension[i];

			snprintf(buf, size, "Field `%s', %u elements, type `%s'", entity->symbol, count, proto_ref_typeref_get_path(entity->type_ref));
			return buf;
		}
		else
		{
			snprintf(buf, size, "Inheritance type `%s'", proto_ref_typeref_get_path(entity->type_ref));
			return buf;
		}
	}
	else if(entity->header.refkind == PROTO_TYPE_ENTITY_REF_NAME && entity->header.symlen > 0 && entity->header.reflen > 0)
	{
		snprintf(buf, size, "Name alias field `%s' = ", entity->symbol);
		char* next_buf = buf + strlen(buf);
		size_t sz = size - strlen(buf);

		proto_ref_nameref_string(entity->name_ref, next_buf, sz);
		return buf;
	}

	return "<invalid-entity>";
}
