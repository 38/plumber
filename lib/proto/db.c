/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include <package_config.h>
#include <proto/err.h>
#include <proto/ref.h>
#include <proto/type.h>
#include <proto/cache.h>
#include <proto/db.h>
#include <proto/assert.h>

static int _init_count = 0;

/**
 * @brief the metadata of this type
 **/
typedef struct {
	uint32_t            actual_size;         /*!< the actual size of this type */
	uint32_t            nentity;             /*!< the number of entities in this type */
	char*               pwd;                 /*!< the "current working directory" for this type */
	char*               name;                /*!< The name for this type, not include the pwd */
	const proto_type_t* type_obj;            /*!< the actual type object */
	union {
		uint32_t*       field_off;           /*!< the computed offset */
		uint32_t*       alias_state;         /*!< the computation state, only used for name alias */
	};
} _type_metadata_t;

/**
 * @brief The flags bit describes an adhoc pritimive
 **/
typedef enum {
	_NONE   = -1,
	_SIZE1  = 0,     /*!< The size of this primitive is 1 */
	_SIZE2  = 1,     /*!< The size of this primitive is 2 */
	_SIZE4  = 2,     /*!< The size of this primitive is 4 */
	_SIZE8  = 3,     /*!< The size of this primitive is 8 */
	_FLOAT  = 4,     /*!< This is a float point number */
	_SIGNED = 8      /*!< This is a signed type */
} _primitive_desc_t;



#define _PD_FLOAT(x) (((x)&_FLOAT))
#define _PD_SIGNED(x)(((x)&_SIGNED))
#define _PD_SIZE(x)  (1u << ((x)&_SIZE8))

/**
 * @brief prase an adhoc type name to the primitive descriptor
 * @param typename The type name to parse
 * @return The primitive descriptor
 **/
static inline _primitive_desc_t _parse_adhoc_type(const char* typename)
{
	const char* trailer = NULL;
	_primitive_desc_t ret = 0;
	if(typename[0] == 'd' || typename[0] == 'f')
	{
		ret = _SIGNED | _FLOAT | (typename[0] == 'd' ? _SIZE8 : _SIZE4);
		trailer = (typename++[0] == 'd' ? "ouble" : "loat");
	}
	else
	{
		if(typename[0] == 'u') typename ++; else ret = _SIGNED;
		if(typename[0] == 'i' && typename[1] == 'n' && typename[2] == 't')
		{
			if      (typename[3] == '8') ret |= _SIZE1, trailer = "int8";
			else if (typename[3] == '1') ret |= _SIZE2, trailer = "int16";
			else if (typename[3] == '3') ret |= _SIZE4, trailer = "int32";
			else if (typename[3] == '6') ret |= _SIZE8, trailer = "int64";
		}
	}
	return (NULL == trailer || strcmp(trailer, typename))? _NONE : ret;
}

/**
 * @brief Generate the type name string from the primitive description
 * @param p The primitive description
 * @return The string, NULL if the pritmive description is not valid
 **/
static inline const char* _adhoc_typename(_primitive_desc_t p)
{
	static char* result_buf[16] = {}, memory[16][7] = {};
	static char* float_name[] = {[4]"float", [8]"double"};

	for(;result_buf[p] == NULL;)
	    if(_PD_FLOAT(p)) result_buf[p] = float_name[_PD_SIZE(p)];
	    else snprintf(result_buf[p] = memory[p], 7, "%sint%d", _PD_SIGNED(p) ? "" : "u", _PD_SIZE(p) * 8);

	return result_buf[p];
}

/**
 * @brief allocate memory for a new metadata object
 * @param proto the protocol type object
 * @return the newly created metadata object or NULL on error cases
 **/
static inline _type_metadata_t* _type_metadata_new(const proto_type_t* proto)
{
	uint32_t nent = proto_type_get_size(proto);
	if(ERROR_CODE(uint32_t) == nent)
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	_type_metadata_t* ret = (_type_metadata_t*)calloc(1, sizeof(*ret));
	if(NULL == ret) PROTO_ERR_RAISE_RETURN_PTR(ALLOC);

	if(NULL == (ret->field_off = (uint32_t*)malloc(sizeof(ret->field_off[0]) * nent)))
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);

	ret->nentity = nent;

	for(;nent > 0; nent --)
	    ret->field_off[nent - 1] = ERROR_CODE(uint32_t);

	ret->actual_size = ERROR_CODE(uint32_t);
	ret->type_obj = proto;
	ret->pwd = NULL;
	return ret;
ERR:
	if(NULL != ret->field_off) free(ret->field_off);

	return NULL;
}

/**
 * @brief dispose a metadata object
 * @return status code
 **/
static inline int _type_metadata_free(void* data)
{
	if(NULL == data)  PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	_type_metadata_t* metadata = (_type_metadata_t*)data;
	if(NULL != metadata->field_off) free(metadata->field_off);
	if(NULL != metadata->pwd) free(metadata->pwd);
	if(NULL == metadata->pwd && NULL != metadata->name) free(metadata->name);
	free(metadata);
	return 0;
}

int proto_db_init()
{
	int rc = 0;
	if(_init_count == 0)
	    rc = proto_cache_init();

	_init_count ++;

	if(rc == ERROR_CODE(int))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return rc;
}

int proto_db_finalize()
{
	if(_init_count == 0)
	    PROTO_ERR_RAISE_RETURN(int, DISALLOWED);

	_init_count --;
	int rc = 0;

	if(_init_count == 0)
	    rc = proto_cache_finalize();

	if(ERROR_CODE(int) == rc)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return rc;
}

const proto_type_t* proto_db_query_type(const char* typename)
{
	if(_init_count == 0)
	    PROTO_ERR_RAISE_RETURN_PTR(DISALLOWED);

	if(NULL == typename)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	return proto_cache_get_type(typename, NULL, NULL);
}

/**
 * @brief get the child pwd
 * @param typename current type name
 * @param pwd current pwd
 * @param result the result buffer
 * @return status code
 **/
static inline int _get_type_pwd(const char* typename, const char* pwd, char** result, char** namebuf)
{

	*result = NULL;
	*namebuf = NULL;
	const char* full_name = proto_cache_full_name(typename, pwd);
	if(NULL == full_name)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	size_t bufsize = strlen(full_name) + 1;
	char* buf = (char*)malloc(bufsize);
	if(NULL == buf)
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);

	int len = snprintf(buf, bufsize, "%s", full_name);
	if((size_t)len > bufsize) len = (int)bufsize;

	for(;len > 0 && buf[len] != '/'; len --);

	if(len > 0)
	{
		buf[len] = 0;
		*result = buf;
	}

	*namebuf = buf + len + (len != 0);

	return 0;
ERR:
	if(NULL == buf) free(buf);
	return ERROR_CODE(int);
}

static inline const _type_metadata_t* _compute_type_metadata(const char* typename, const char* pwd);

/**
 * @brief get the size of this entity
 * @param entity the entity to compute
 * @param child_pwd the child pwd
 * @param padding_size the padding size of the parent type
 * @param actual_padding the result buffer for the padding size actually append to the end
 * @return the size of the entity, not include the padding bytes
 **/
static inline uint32_t _get_entity_size(const proto_type_entity_t* entity, const char* child_pwd, uint32_t padding_size, uint32_t* actual_padding)
{
	uint32_t elem_size = 0;

	/* If it's a type reference, we need to get the type of the target type first */
	if(entity->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
	{
		const char* target = proto_ref_typeref_get_path(entity->type_ref);
		const _type_metadata_t* child_metadata = target == NULL ? NULL : _compute_type_metadata(target, child_pwd);
		if(NULL == child_metadata)
		    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);
		elem_size = child_metadata->actual_size;
	}
	else if(entity->header.refkind == PROTO_TYPE_ENTITY_REF_NONE)
	    elem_size = entity->header.elem_size;

	/* Do not waste time on 0 sized objects */
	if(elem_size == 0)
	{
		*actual_padding = 0;
		return 0;
	}

	/* Then we need to figure out how many elements in this entity */
	uint32_t count = 1, j;
	for(j = 0; j < entity->header.dimlen; j ++)
	    count *= entity->dimension[j];

	/* Then we need to adjust the padding for the child size */
	uint32_t total = elem_size * count;

	/* Becuase we do not want to append padding bytes if the type reference is the last field in the
	 * memory layout, this means we don't want to waste our memory */
	if(padding_size > 0 && entity->header.refkind != PROTO_TYPE_ENTITY_REF_NONE)
	    *actual_padding = (padding_size - total % padding_size) % padding_size;
	else
	    *actual_padding = 0;

	return total;
}

/**
 * @brief Compute the metadata for the given type
 * @param typename the name of the type
 * @param pwd current working directory
 * @return the metadata object for the given type, NULL on error cases
 **/
static inline const _type_metadata_t* _compute_type_metadata(const char* typename, const char* pwd)
{
	_type_metadata_t* metadata;

	/* First of all we need to ask the cache for the protocol type object */
	const proto_type_t* proto = proto_cache_get_type(typename, pwd,  (void**)&metadata);
	if(NULL == proto) PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	/* Then we need to check if the metadata is already there */
	if(metadata != NULL)
	{
		if(metadata->actual_size == ERROR_CODE(uint32_t))
		    PROTO_ERR_RAISE_RETURN_PTR(CIRULAR_DEP);
		else return metadata;
	}

	/* Metadata is not there? create a new one */
	if(NULL == (metadata = _type_metadata_new(proto)))
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);

	if(ERROR_CODE(int) == proto_cache_attach_type_data(typename, pwd, _type_metadata_free, metadata))
	    PROTO_ERR_RAISE_GOTO(ERR, FAIL);

	/* Then we need the full name of the type to construct the pwd for its children */
	if(ERROR_CODE(int) == _get_type_pwd(typename, pwd, &metadata->pwd, &metadata->name))
	    PROTO_ERR_RAISE_GOTO(ERR, FAIL);

	uint32_t nsegs, i, padding_size, current_size = 0, prev_padding = 0, total, padding;

	if(ERROR_CODE(uint32_t) == (nsegs = proto_type_get_size(proto)))
	    PROTO_ERR_RAISE_GOTO(ERR, FAIL);

	if(ERROR_CODE(uint32_t) == (padding_size = proto_type_get_padding_size(proto)))
	    PROTO_ERR_RAISE_GOTO(ERR, FAIL);

	for(i = 0; i < nsegs; i ++)
	{
		const proto_type_entity_t* entity = proto_type_get_entity(proto, i);
		if(NULL == entity || ERROR_CODE(uint32_t) == (total = _get_entity_size(entity, metadata->pwd, padding_size, &padding)))
		    PROTO_ERR_RAISE_GOTO(ERR, FAIL);

		if(entity->header.refkind != PROTO_TYPE_ENTITY_REF_NAME)
		{
			metadata->field_off[i] = current_size + prev_padding;
			if(entity->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
			{
				const char* relative_name = proto_ref_typeref_get_path(entity->type_ref);
				if(NULL == relative_name)
				    PROTO_ERR_RAISE_GOTO(ERR, FAIL);
			}
		}

		if(total == 0 || metadata->field_off[i] == ERROR_CODE(uint32_t)) continue;

		current_size += prev_padding + total;
		prev_padding = padding;
	}

	metadata->actual_size = current_size;

	return metadata;
ERR:
	if(metadata != NULL)  proto_cache_attach_type_data(typename, pwd, NULL, NULL);
	return NULL;
}

/**
 * @brief parse the field name expression to a name reference
 * @param field_name the field name expression
 * @return the newly created name reference, NULL indicates an error
 **/
static inline proto_ref_nameref_t* _parse_name_expr(const char* field_name)
{
#define _INRANGE(var, l, r) ((l) <= (var) && (var) <= (r))
#define _IDINITIAL(var) (_INRANGE(var, 'a', 'z') || _INRANGE(var, 'A', 'Z') || (var) == '_' || (var) == '$')
#define _IDBODY(var)    (_IDINITIAL(var) || _INRANGE(var, '0', '9'))
#define _OCTDIG(var)    (_INRANGE(var, '0', '7'))
#define _OCTINITIAL(var)((var) == '0')
#define _HEXSIGN(var)   ((var) == 'x' || (var) == 'X')
#define _HEXDIG(var)    (_INRANGE(var, '0', '9') || _INRANGE(var, 'a', 'f') || _INRANGE(var, 'A', 'F'))
#define _DECDIG(var)    (_INRANGE(var, '0', '9'))
	proto_ref_nameref_t* ret = proto_ref_nameref_new(PROTO_REF_NAME_INIT_SIZE);

	char const * ptr;
	/* this is the begining of current name segment, so it will be used as the state that determine if
	 * the parser is parsing state as well */
	char const*  seg_begin = NULL;
	/* Same as the seg_begin, it's also used to tracking the parser state */
	uint32_t subs_value = ERROR_CODE(uint32_t);

	/* This is the base of the subscription, 0 means the base is not determined */
	uint32_t subs_base = 0;

	if(NULL == ret) PROTO_ERR_RAISE_RETURN_PTR(ALLOC);

	for(ptr = field_name; ; ptr ++)
	{
		ASSERT_GOTO(seg_begin == NULL || subs_value == ERROR_CODE(uint32_t), ERR);

		if(seg_begin == NULL && subs_value == ERROR_CODE(uint32_t))
		{
			/* This means we are just start parsing the next field */
			if(_IDINITIAL(*ptr)) seg_begin = ptr;
			else PROTO_ERR_RAISE_GOTO(ERR, NAME_EXPR);
		}
		else if(seg_begin != NULL)
		{
			/* We are in the middle of parsing field name */
			if(*ptr == '.' || *ptr == '[' || *ptr == 0)
			{
				if(ERROR_CODE(int) == proto_ref_nameref_append_symbol_range(ret, seg_begin, ptr))
				    PROTO_ERR_RAISE_GOTO(ERR, FAIL);
				goto SEG_END;
			}
			/* otherwise, no need to do anything */
		}
		else if(subs_value != ERROR_CODE(uint32_t))
		{
			/* Then we are going to parse a subscript */
			switch(subs_base)
			{
				case 0:
				    /* If the base is not determined */
				    subs_base = _OCTINITIAL(*ptr) ? 8 : 10;
				    break;
				case 8:
				    if(subs_value == 0 && _HEXSIGN(*ptr))
				    {
					    subs_base = 16;   /* If this is 0x */
					    continue;
				    }
				    else if(subs_value == 0 && *ptr == '0') PROTO_ERR_RAISE_GOTO(ERR, NAME_EXPR);   /* 00 is not valid */
				    else if(!_OCTDIG(*ptr))                 goto NAD;   /* The end of the subscript */
				    break;
				case 10:
				    if(!_DECDIG(*ptr)) goto NAD;
				    break;
				case 16:
				    if(!_HEXDIG(*ptr)) goto NAD;
				    break;
				default:
				    PROTO_ERR_RAISE_GOTO(ERR, NAME_EXPR);
			}
			subs_value = subs_value * subs_base + (unsigned)(_INRANGE(*ptr, '0', '9') ? *ptr - '0' : (_INRANGE(*ptr, 'A', 'F') ? *ptr - 'A' + 10 : *ptr - 'a' + 10));
			if(ERROR_CODE(uint32_t) == subs_value) PROTO_ERR_RAISE_GOTO(ERR, NAME_EXPR);   /* we disallow define 0xffffffff, because it's error code */
			continue;
			NAD:  /* Not a digit */
			if(*ptr == ']')
			{
				if(ERROR_CODE(int) == proto_ref_nameref_append_subscript(ret, subs_value))
				    PROTO_ERR_RAISE_GOTO(ERR, FAIL);
				ptr ++;
				goto SEG_END;
			}
			else PROTO_ERR_RAISE_GOTO(ERR, NAME_EXPR);
		}
		continue;
SEG_END:
		/* execute this code whenever the segment terminates */
		seg_begin = NULL;
		subs_value = (*ptr == '[') ? 0 : ERROR_CODE(uint32_t);
		subs_base = 0;

		if(*ptr == 0) break;
	}

	return ret;
ERR:
	if(NULL != ret) proto_ref_nameref_free(ret);

	return NULL;
#undef _INRANGE
#undef _IDINITIAL
#undef _IDBODY
#undef _OCTDIG
#undef _OCTINITIAL
#undef _HEXSIGN
#undef _HEXDIG
#undef _DECDIG
}

uint32_t proto_db_type_size(const char* typename)
{
	if(_init_count == 0)
	    PROTO_ERR_RAISE_RETURN(uint32_t, DISALLOWED);

	if(NULL == typename)
	    PROTO_ERR_RAISE_RETURN(uint32_t, ARGUMENT);

	_primitive_desc_t pd;
	if(_NONE != (pd = _parse_adhoc_type(typename)))
	    return _PD_SIZE(pd);

	const _type_metadata_t* metadata = _compute_type_metadata(typename, NULL);

	if(NULL == metadata)
	    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);

	return metadata->actual_size;
}

static uint32_t _compute_token = 0;

/**
 * @brief do a linear search on the protocol type for the given symbol
 * @param type_data the type data for the protocol type to search
 * @param symbol the symbol to search
 * @param offbuf the buffer use to return the offset (starting from head of this type) in bytes to the field we are looking for
 * @return the entity matches the name or NULL on error
 **/
static inline const proto_type_entity_t* _find_member(const _type_metadata_t* type_data, const char* symbol, uint32_t* offbuf)
{
	uint32_t i;
	for(i = 0; i < type_data->nentity; i ++)
	{
		const proto_type_entity_t* ent = proto_type_get_entity(type_data->type_obj, i);
		if(ent != NULL && ent->symbol != NULL && strcmp(ent->symbol, symbol) == 0)
		{
			if(NULL != offbuf) *offbuf = type_data->field_off[i];
			if(ent->header.refkind == PROTO_TYPE_ENTITY_REF_NAME)
			{
				if(type_data->alias_state[i] == _compute_token)
				    PROTO_ERR_RAISE_RETURN_PTR(CIRULAR_DEP);
				else type_data->alias_state[i] = _compute_token;
			}
			return ent;
		}
	}

	return NULL;
}

static inline int _find_basetype(const _type_metadata_t* type_data, _type_metadata_t const* * resultbuf)
{
	const proto_type_entity_t* ent = proto_type_get_entity(type_data->type_obj, 0);
	if(NULL == ent)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(ent->symbol == NULL && ent->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
	{
		const char* target_type = proto_ref_typeref_get_path(ent->type_ref);
		if(NULL == target_type)
		    PROTO_ERR_RAISE_RETURN(int, FAIL);

		if(NULL == (*resultbuf = _compute_type_metadata(target_type, type_data->pwd)))
		    PROTO_ERR_RAISE_RETURN(int, FAIL);

		return 1;
	}

	return 0;
}
/**
 * @brief the information about a name expression during the offset comupation
 **/
typedef struct {
	const _type_metadata_t* typedata;  /*!< the type metadata for the underlying type */
	const proto_type_atomic_metadata_t* primitive_data;   /*!< The metadata for a primitive field */
	uint32_t                elemsize;  /*!< the element size for the underlying type, this is necessary because for primitive, we don't have typedata */
	uint32_t                dimlen;    /*!< the length of dimension data */
	const uint32_t*         dimension; /*!< the size of each dimension */
} _name_info_t;

/**
 * @brief compute the memory offset for the given name expression
 * @param type_data the type metadata for the type we want to comppute
 * @param name      the compiled name expression
 * @param start     because we need recursion, so the semantics for this function is read the name expression from this start point and resolve
 * @param base_off  the offset that comes from the parent
 * @param infobuf   the information buffer
 * @return the offset or error code
 **/
static inline uint32_t _compute_name_offset(const _type_metadata_t* type_data, const proto_ref_nameref_t* name,
                                            uint32_t start, uint32_t base_off, _name_info_t* infobuf)
{
	if(NULL == type_data || NULL == name || infobuf == NULL)
	    PROTO_ERR_RAISE_RETURN(uint32_t, ARGUMENT);

	/* The first segment in the name reference must be a symbol */
	const char* target_symbol = NULL;
	if(ERROR_CODE(proto_ref_nameref_seg_type_t) == proto_ref_nameref_get(name, start, &target_symbol, NULL))
	    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);

	uint32_t field_offset;
	infobuf->elemsize = 0;
	infobuf->typedata = NULL;
	infobuf->primitive_data = NULL;
	infobuf->dimlen   = 0;
	infobuf->dimension = NULL;
	const proto_type_entity_t* entity = _find_member(type_data, target_symbol, &field_offset);

	if(entity == NULL)
	{
		if(proto_err_stack() != NULL)
		    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);

		const _type_metadata_t* base_data;
		int rc = _find_basetype(type_data, &base_data);
		if(ERROR_CODE(int) == rc)
		    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);
		else if(rc == 0)
		    PROTO_ERR_RAISE_RETURN(uint32_t, UNDEFINED);
		else
		    return _compute_name_offset(base_data, name, start, base_off, infobuf);
	}

	const char* target_type = NULL;

	switch(entity->header.refkind)
	{
		case PROTO_TYPE_ENTITY_REF_NONE:
		    infobuf->elemsize = entity->header.elem_size;
		    infobuf->dimlen   = entity->header.dimlen;
		    infobuf->dimension = entity->dimension;
		    if(entity->header.metadata)
		        infobuf->primitive_data = entity->metadata;
		    break;
		case PROTO_TYPE_ENTITY_REF_TYPE:
		    if(NULL == (target_type = proto_ref_typeref_get_path(entity->type_ref)))
		        PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);

		    if(NULL == (infobuf->typedata = _compute_type_metadata(target_type, type_data->pwd)))
		        PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);

		    infobuf->elemsize = infobuf->typedata->actual_size;
		    infobuf->dimlen   = entity->header.dimlen;
		    infobuf->dimension = entity->dimension;
		    break;
		case PROTO_TYPE_ENTITY_REF_NAME:
		    if(ERROR_CODE(uint32_t) == (field_offset = _compute_name_offset(type_data, entity->name_ref, 0, 0, infobuf)))
		        PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);
		    break;
		default:
		    PROTO_ERR_RAISE_RETURN(uint32_t, FORMAT);
	}

	/* Then we need to handle the subscrpipts */
	uint32_t array_offset = 0, dimidx, namesize = proto_ref_nameref_nsegs(name);
	if(namesize == ERROR_CODE(uint32_t))
	    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);
	for(dimidx = 0; ++ start < namesize; dimidx ++)
	{
		uint32_t index;
		if(ERROR_CODE(proto_ref_nameref_seg_type_t) == proto_ref_nameref_get(name, start, NULL, &index))
		    break;

		if(dimidx >= infobuf->dimlen)
		    PROTO_ERR_RAISE_RETURN(uint32_t, DIM);

		if(index >= infobuf->dimension[dimidx])
		    PROTO_ERR_RAISE_RETURN(uint32_t, OUT_OF_BOUND);

		array_offset = array_offset * infobuf->dimension[dimidx] + index;
	}
	if(proto_err_stack())
	    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);


	infobuf->dimension += dimidx;
	infobuf->dimlen -= dimidx;

	for(dimidx = 0;dimidx < infobuf->dimlen; dimidx ++)
	    array_offset = array_offset * infobuf->dimension[dimidx];

	array_offset *= infobuf->elemsize;

	if(start >= namesize)
	    return base_off + field_offset + array_offset;

	if(infobuf->dimlen != 0 && (infobuf->dimlen > 1 || infobuf->dimension[0] != 1))
	    PROTO_ERR_RAISE_RETURN(uint32_t, UNDEFINED);
	return _compute_name_offset(infobuf->typedata, name, start, base_off + field_offset + array_offset, infobuf);
}

static inline uint32_t _compute_field_info(const char* typename, const char* fieldname, _name_info_t* infobuf)
{

	proto_ref_nameref_t* name = _parse_name_expr(fieldname);
	if(NULL == name)
	    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);

	const _type_metadata_t* metadata = _compute_type_metadata(typename, NULL);
	uint32_t ret = ERROR_CODE(uint32_t);

	if(NULL != metadata)
	    ret =  _compute_name_offset(metadata, name, 0, 0, infobuf);

	proto_ref_nameref_free(name);

	if(ERROR_CODE(uint32_t) == ret)
	    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);

	return ret;
}

uint32_t proto_db_type_offset(const char* typename, const char* fieldname, uint32_t* size)
{
	if(_init_count == 0)
	    PROTO_ERR_RAISE_RETURN(uint32_t, DISALLOWED);

	_compute_token ++;

	proto_err_clear();

	if(NULL == typename || NULL == fieldname)
	    PROTO_ERR_RAISE_RETURN(uint32_t, ARGUMENT);

	/* Handle the adhoc types */
	_primitive_desc_t pd;
	if(_NONE != (pd = _parse_adhoc_type(typename)))
	{
		if(strcmp(fieldname, "value") != 0)
		    PROTO_ERR_RAISE_RETURN(uint32_t, UNDEFINED);
		if(size != NULL) *size = _PD_SIZE(pd);
		return 0;
	}

	_name_info_t info;
	uint32_t ret;
	if(ERROR_CODE(uint32_t) == (ret = _compute_field_info(typename, fieldname, &info)))
	    PROTO_ERR_RAISE_RETURN(uint32_t, FAIL);

	if(size != NULL)
	{
		*size = info.elemsize;
		uint32_t i;
		for(i = 0; i < info.dimlen; i++)
		    *size *= info.dimension[i];
	}

	return ret;
}

int proto_db_type_validate(const char* typename)
{
	if(NULL == typename)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	proto_err_clear();
	const _type_metadata_t* metadata = _compute_type_metadata(typename, NULL);
	if(NULL == metadata)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	uint32_t i;
	for(i = 0; i < metadata->nentity; i ++)
	{
		const proto_type_entity_t* ent = proto_type_get_entity(metadata->type_obj, i);
		if(NULL == ent)
		    PROTO_ERR_RAISE_RETURN(int, FAIL);
		if(ent->header.refkind == PROTO_TYPE_ENTITY_REF_NAME)
		{
			_compute_token ++;
			const proto_ref_nameref_t* name = ent->name_ref;
			_name_info_t info;
			if(ERROR_CODE(uint32_t) == _compute_name_offset(metadata, name, 0, 0, &info))
			    PROTO_ERR_RAISE_RETURN(int, FAIL);
		}
	}

	return 0;
}

/**
 * @brief get the parent of the given type name
 * @note the type name should be the absolute type name
 * @param type the type name to query
 * @return the parent type name, please note the pointer is managed by libproto, DO NOT dispose it externally
 **/
static inline const char* _parent_of(const char* type)
{
	/* If this is an adhoc type, the parent the none */
	if(_NONE != _parse_adhoc_type(type))
	    return NULL;

	const proto_type_t* proto = proto_db_query_type(type);

	if(NULL == proto)
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	const proto_type_entity_t* ent = proto_type_get_entity(proto, 0);

	if(NULL == ent)
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	if(ent->symbol == NULL && ent->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
	{
		static char pwdbuf[PATH_MAX];
		int n = snprintf(pwdbuf, sizeof(pwdbuf), "%s", type);
		if(n > PATH_MAX) n = PATH_MAX;
		for(;n > 0 && pwdbuf[n] != '/'; n--);
		pwdbuf[n] = 0;

		const char* refname = proto_ref_typeref_get_path(ent->type_ref);
		if(NULL == refname)
		    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

		const char* fullname = proto_cache_full_name(refname, pwdbuf);
		if(NULL == fullname)
		    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

		return fullname;
	}

	return NULL;
}

const char* proto_db_common_ancestor(char const* const* type_name)
{
	if(NULL == type_name)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	if(type_name[0] == NULL)
	    return NULL;

	const char* ret = type_name[0];

	uint32_t i;
	for(i = 1; type_name[i] && ret != NULL; i ++)
	{
		const char* current_left = ret;
		for(;NULL != current_left;)
		{
			const char* current_right = type_name[i];
			for(;NULL != current_right;)
			{
				if(strcmp(current_left, current_right) == 0)
				    goto FOUND;
				current_right = _parent_of(current_right);
			}
			current_left = _parent_of(current_left);
		}
FOUND:
		ret = current_left;
	}

	return ret;
}

const char* proto_db_field_type(const char* typename, const char* fieldname)
{
	if(_init_count == 0)
	    PROTO_ERR_RAISE_RETURN_PTR(DISALLOWED);

	_compute_token ++;

	proto_err_clear();

	if(NULL == typename || NULL == fieldname)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	/* Handle the adhoc types */
	_primitive_desc_t pd;
	if(_NONE != (pd = _parse_adhoc_type(typename)))
	{
		if(strcmp(fieldname, "value") != 0)
		    PROTO_ERR_RAISE_RETURN_PTR(UNDEFINED);
		return typename;
	}

	_name_info_t info;
	if(ERROR_CODE(uint32_t) == _compute_field_info(typename, fieldname, &info))
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	if(info.typedata != NULL)
	    return proto_cache_full_name(info.typedata->name, info.typedata->pwd);
	else
	{
		/* Basically we do not allow any adhoc scope token, because it's dangerous and not make sense */
		if(info.primitive_data->flags.scope.valid)
		    PROTO_ERR_RAISE_RETURN_PTR(DISALLOWED);

		/* Also we do not allow any constant value becomes an adhoc type */
		if(info.elemsize == 0)
		    PROTO_ERR_RAISE_RETURN_PTR(DISALLOWED);

		uint8_t sizecode = 0;
		for(;info.elemsize > 1; sizecode ++, info.elemsize /= 2);

		/* We do not allow an adhoc array at this point */
		if(info.dimlen > 1 || (info.dimlen == 1 && info.dimension[0] > 1))
		    PROTO_ERR_RAISE_RETURN_PTR(DISALLOWED);

		/* Then let's construct the primitive descriptor */
		_primitive_desc_t pd = (info.primitive_data->flags.numeric.is_real ? _FLOAT : 0) |
		                       (info.primitive_data->flags.numeric.is_signed ? _SIGNED : 0) |
		                       sizecode;
		return _adhoc_typename(pd);
	}
}

const char* proto_db_get_managed_name(const char* name)
{
	_primitive_desc_t pd;
	if(_NONE != (pd = _parse_adhoc_type(name)))
	    return _adhoc_typename(pd);

	return proto_cache_full_name(name, NULL);
}

proto_db_field_prop_t proto_db_field_type_info(const char* typename, const char* fieldname)
{
	if(_init_count == 0)
	    PROTO_ERR_RAISE_RETURN(proto_db_field_prop_t, DISALLOWED);

	_compute_token ++;

	proto_err_clear();

	if(NULL == typename || NULL == fieldname)
	    PROTO_ERR_RAISE_RETURN(proto_db_field_prop_t, ARGUMENT);

	/* Handle the adhoc types */
	_primitive_desc_t pd;
	if(_NONE != (pd = _parse_adhoc_type(typename)))
	{
		if(strcmp(fieldname, "value") != 0)
		    PROTO_ERR_RAISE_RETURN(proto_db_field_prop_t, UNDEFINED);
		proto_db_field_prop_t ret = (proto_db_field_prop_t)0;
		if(pd & _FLOAT)  ret |= PROTO_DB_FIELD_PROP_REAL;
		if(pd & _SIGNED) ret |= PROTO_DB_FIELD_PROP_SIGNED;
		ret |= PROTO_DB_FIELD_PROP_NUMERIC;
		return ret;
	}

	_name_info_t info;
	if(ERROR_CODE(uint32_t) == _compute_field_info(typename, fieldname, &info))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(info.primitive_data == NULL)
	    return (proto_db_field_prop_t)0;
	else
	{
		proto_db_field_prop_t ret = (proto_db_field_prop_t)0;

		if(info.primitive_data->flags.scope.valid)
		{
			ret |= PROTO_DB_FIELD_PROP_SCOPE;
			if(info.primitive_data->flags.scope.primitive) ret |= PROTO_DB_FIELD_PROP_PRIMITIVE_SCOPE;
		}
		else
		{
			ret |= PROTO_DB_FIELD_PROP_NUMERIC;
			if(info.primitive_data->flags.numeric.is_real) ret |= PROTO_DB_FIELD_PROP_REAL;
			if(info.primitive_data->flags.numeric.is_signed) ret |= PROTO_DB_FIELD_PROP_SIGNED;
		}
		return ret;
	}

}

const char* proto_db_field_scope_id(const char* typename, const char* fieldname)
{
	if(_init_count == 0)
	    PROTO_ERR_RAISE_RETURN_PTR(DISALLOWED);

	_compute_token ++;

	proto_err_clear();

	if(NULL == typename || NULL == fieldname)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	if(_NONE != _parse_adhoc_type(typename)) return NULL;

	_name_info_t info;
	if(ERROR_CODE(uint32_t) == _compute_field_info(typename, fieldname, &info))
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	if(info.primitive_data == NULL) return NULL;

	if(!info.primitive_data->flags.scope.valid) return NULL;

	return info.primitive_data->scope_typename;
}

int proto_db_field_get_default(const char* typename, const char* fieldname, const void** buf, size_t* sizebuf)
{
	if(_init_count == 0)
	    PROTO_ERR_RAISE_RETURN(int, DISALLOWED);

	_compute_token ++;

	proto_err_clear();

	if(NULL == typename || NULL == fieldname || NULL == buf || NULL == sizebuf)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(_NONE != _parse_adhoc_type(typename)) return 0;

	_name_info_t info;
	if(ERROR_CODE(uint32_t) == _compute_field_info(typename, fieldname, &info))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(info.primitive_data == NULL) return 0;

	if(info.primitive_data->flags.scope.valid) return 0;

	*buf = info.primitive_data->numeric_default;
	*sizebuf = info.primitive_data->flags.numeric.default_size;
	return 1;
}
