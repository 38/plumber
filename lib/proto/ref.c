/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <package_config.h>
#include <err.h>
#include <ref.h>

/**
 * @brief a name segment
 **/
typedef struct {
	int  sym;            /*!< indicates if this name reference segment is a symbol, if 0 means this is a susbscript segment */
	union {
		char*     sym;   /*!< the actual symbol text for a symbol segment */
		uint32_t  sub;   /*!< the value for the subscript */
	}             value; /*!< the value of this name segment */
} _name_seg_t;

/**
 * @brief the actual data structure for the name reference object
 **/
struct _proto_ref_nameref_t {
	uint32_t      capacity;    /*!< the capacity of this the name reference object, this means how many segment slots in the segments array */
	uint32_t      size;        /*!< the actual size of the name reference object */
	_name_seg_t*  segments;    /*!< the segments array for this name reference */
};


/**
 * @brief the actual data structure for a type reference
 **/
struct _proto_ref_typeref_t {
	uint32_t      capacity;    /*!< the capacity of this type reference object, this means the numer of bytes allocated for the path string */
	uint32_t      size;        /*!< how many bytes is currently in use */
	char*         path;
};

proto_ref_nameref_t* proto_ref_nameref_new(uint32_t capacity)
{
	proto_ref_nameref_t* ret = (proto_ref_nameref_t*)malloc(sizeof(proto_ref_nameref_t));
	if(NULL == ret)
	    PROTO_ERR_RAISE_RETURN_PTR(ALLOC);

	ret->capacity = capacity;
	ret->size = 0;

	if(NULL == (ret->segments = (_name_seg_t*)malloc(sizeof(_name_seg_t) * capacity)))
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);

	return ret;

ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

int proto_ref_nameref_free(proto_ref_nameref_t* ref)
{
	if(NULL == ref)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(NULL != ref->segments)
	{
		uint32_t i;
		for(i = 0; i < ref->size; i ++)
		{
			_name_seg_t* seg = ref->segments + i;
			if(seg->sym && seg->value.sym != NULL)
			    free(seg->value.sym);
		}

		free(ref->segments);
	}
	free(ref);

	return 0;
}

proto_ref_nameref_seg_type_t proto_ref_nameref_get(const proto_ref_nameref_t* ref, uint32_t idx, const char** sym_buf, uint32_t* sub_buf)
{
	if(NULL == ref || idx >= ref->size || (sym_buf == NULL && sub_buf == NULL))
	    PROTO_ERR_RAISE_RETURN(proto_ref_nameref_seg_type_t, ARGUMENT);

	if(ref->segments[idx].sym && sym_buf != NULL)
	{
		*sym_buf = ref->segments[idx].value.sym;
		return PROTO_REF_NAMEREF_SEG_TYPE_SYM;
	}
	else if(!ref->segments[idx].sym && sub_buf != NULL)
	{
		*sub_buf = ref->segments[idx].value.sub;
		return PROTO_REF_NAMEREF_SEG_TYPE_SUB;
	}

	return PROTO_REF_NAMEREF_SEG_TYPE_ERR;
}

uint32_t proto_ref_nameref_nsegs(const proto_ref_nameref_t* ref)
{
	if(NULL == ref)
	    PROTO_ERR_RAISE_RETURN(uint32_t, ARGUMENT);
	return ref->size;
}

uint32_t proto_ref_nameref_size(const proto_ref_nameref_t* ref)
{
	if(NULL == ref)
	    PROTO_ERR_RAISE_RETURN(uint32_t, ARGUMENT);

	uint32_t ret = 0, i;
	for(i = 0; i < ref->size; i ++)
	{
		const _name_seg_t* seg = ref->segments + i;

		/* a single char indicates the type of the segment */
		ret += 1;

		if(seg->sym)
		    ret += (uint32_t)strlen(seg->value.sym) + 1;
		else
		    ret += (uint32_t)sizeof(seg->value.sub);
	}

	return ret;
}

const char* proto_ref_nameref_string(const proto_ref_nameref_t* ref, char* buf, size_t sz)
{
	if(NULL == ref)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	uint32_t i = 0;
	char* ptr = buf;
	for(i = 0; i < ref->size && sz > 0; i ++)
	{
		int bytes_written = 0;
		const _name_seg_t* seg = ref->segments + i;
		if(seg->sym)
		{
			if(i == 0)
			    bytes_written = snprintf(ptr, sz, "%s", seg->value.sym);
			else
			    bytes_written = snprintf(ptr, sz, ".%s", seg->value.sym);
		}
		else
		{
			bytes_written = snprintf(ptr, sz, "[%u]", seg->value.sub);
		}

		if(bytes_written < 0) PROTO_ERR_RAISE_RETURN_PTR(WRITE);

		if(sz > (uint32_t)bytes_written)
		{
			sz -= (uint32_t)bytes_written;
			ptr += (uint32_t)bytes_written;
		}
		else sz = 0;
	}

	return buf;
}

/**
 * @brief make sure the reference object has at least one slot for the new entry
 * @param ref the reference object
 * @return status code
 **/
static inline int _nameref_ensure_space(proto_ref_nameref_t* ref)
{
	if(ref->capacity <= ref->size)
	{
		uint32_t new_size = ref->capacity * 2;
		_name_seg_t* new_seg = (_name_seg_t*)realloc(ref->segments, sizeof(_name_seg_t) * new_size);
		if(NULL == new_seg)
		    PROTO_ERR_RAISE_RETURN(int, ALLOC);

		ref->capacity = new_size;
		ref->segments = new_seg;
	}

	return 0;
}

/**
 * @brief make sure the type reference object has enough object for the new segment
 * @param ref the type reference object
 * @param seglen the segment length
 * @return status code
 **/
static inline int _typeref_ensure_space(proto_ref_typeref_t* ref, uint32_t seglen)
{
	/* Because for the non-initial segment, we need to append '/' before the segment */
	uint32_t actual_len = ref->size == 0 ? seglen : seglen + 1;
	uint32_t new_cap;
	for(new_cap = ref->capacity; new_cap - ref->size <= actual_len; new_cap *= 2);

	if(new_cap != ref->capacity)
	{
		char* new_path = (char*)realloc(ref->path, new_cap + 1);
		if(NULL == new_path)
		    PROTO_ERR_RAISE_RETURN(int, ALLOC);

		ref->capacity = new_cap;
		ref->path = new_path;
	}

	return 0;
}

int proto_ref_nameref_append_symbol_range(proto_ref_nameref_t* ref, const char* begin, const char* end)
{
	if(NULL == ref || NULL == begin || NULL == end || begin >= end)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(ERROR_CODE(int) == _nameref_ensure_space(ref))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	_name_seg_t* seg = ref->segments + ref->size;
	size_t len = (size_t)(end - begin);
	seg->sym = 1;
	if(NULL == (seg->value.sym = (char*)malloc(len + 1)))
	    PROTO_ERR_RAISE_RETURN(int, ALLOC);

	memcpy(seg->value.sym, begin, len);
	seg->value.sym[len] = 0;

	ref->size ++;
	return 0;

}

int proto_ref_nameref_append_symbol(proto_ref_nameref_t* ref, const char* symbol)
{
	if(NULL == ref || NULL == symbol)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	size_t len = strlen(symbol);

	return proto_ref_nameref_append_symbol_range(ref, symbol, symbol + len);
}


int proto_ref_nameref_append_subscript(proto_ref_nameref_t* ref, uint32_t subscript)
{
	if(NULL == ref)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(ERROR_CODE(int) == _nameref_ensure_space(ref))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	_name_seg_t* seg = ref->segments + ref->size;
	seg->sym = 0;
	seg->value.sub = subscript;

	ref->size ++;
	return 0;
}

proto_ref_nameref_t* proto_ref_nameref_load(FILE* fp, uint32_t size)
{
	if(NULL == fp)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	proto_ref_nameref_t* ret = proto_ref_nameref_new(PROTO_REF_NAME_INIT_SIZE);

	size_t bufsize = 128, bufused = 0;
	char* buf = (char*)malloc(bufsize);
	if(NULL == buf)
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);
	uint32_t state = 0, i;
	for(i = 0; i < size; i ++)
	{
		int ch = fgetc(fp);
		if(EOF == ch)
		    PROTO_ERR_RAISE_GOTO(ERR, ARGUMENT);

		switch(state)
		{
			case 0: /* segment type expected, PROTO_REF_NAMEREF_SEG_TYPE_SUB for subscript, PROTO_REF_NAMEREF_SEG_TYPE_SYM for symbol */
			    if(ch == PROTO_REF_NAMEREF_SEG_TYPE_SUB)
			    {
				    if(size - i - 1 < sizeof(uint32_t))
				        PROTO_ERR_RAISE_GOTO(ERR, FORMAT);
				    uint32_t value;
				    if(1 != fread(&value, sizeof(uint32_t), 1, fp))
				        PROTO_ERR_RAISE_GOTO(ERR, READ);
				    if(ERROR_CODE(int) == proto_ref_nameref_append_subscript(ret, value))
				        PROTO_ERR_RAISE_GOTO(ERR, FAIL);
				    i += (uint32_t)sizeof(uint32_t);
			    }
			    else if(ch == PROTO_REF_NAMEREF_SEG_TYPE_SYM)
			        state = 1, bufused = 0;
			    else PROTO_ERR_RAISE_GOTO(ERR, FORMAT);
			    break;
			case 1: /* a subscript */
			    if(bufused >= bufsize)
			    {
				    char* newbuf = (char*)realloc(buf, bufsize * 2);
				    if(NULL == newbuf)
				        PROTO_ERR_RAISE_GOTO(ERR, ALLOC);
				    buf = newbuf;
				    bufsize *= 2;
			    }
			    if(0 == (buf[bufused++] = (char)ch))
			    {
				    if(ERROR_CODE(int) == proto_ref_nameref_append_symbol(ret, buf))
				        PROTO_ERR_RAISE_GOTO(ERR, FAIL);
				    state = 0;
			    }
		}
	}

	free(buf);

	return ret;
ERR:
	if(NULL != ret) proto_ref_nameref_free(ret);
	if(NULL != buf) free(buf);
	return NULL;

}

int proto_ref_nameref_dump(const proto_ref_nameref_t* ref, FILE* fp)
{
	if(NULL == ref || NULL == fp)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	uint32_t i;
	for(i = 0; i < ref->size; i ++)
	{
		const _name_seg_t* seg = ref->segments + i;
		if(seg->sym)
		{
			char type = (char)PROTO_REF_NAMEREF_SEG_TYPE_SYM;
			if(1 != fwrite(&type, 1, 1, fp))
			    PROTO_ERR_RAISE_RETURN(int, WRITE);
			size_t size = strlen(seg->value.sym);
			if(1 != fwrite(seg->value.sym, size + 1, 1, fp))
			    PROTO_ERR_RAISE_RETURN(int, WRITE);
		}
		else
		{
			char type = (char)PROTO_REF_NAMEREF_SEG_TYPE_SUB;
			if(1 != fwrite(&type, 1, 1, fp))
			    PROTO_ERR_RAISE_RETURN(int, WRITE);
			if(1 != fwrite(&seg->value.sub, sizeof(seg->value.sub), 1, fp))
			    PROTO_ERR_RAISE_RETURN(int, WRITE);
		}
	}

	return 0;
}

proto_ref_typeref_t* proto_ref_typeref_new(uint32_t capacity)
{
	proto_ref_typeref_t* ret = (proto_ref_typeref_t*)malloc(sizeof(proto_ref_typeref_t));
	if(NULL == ret)
	    PROTO_ERR_RAISE_RETURN_PTR(ALLOC);

	ret->capacity = capacity;
	ret->size = 0;
	/* because we need one more bit for the \0 */
	if(NULL == (ret->path = (char*)malloc(capacity + 1)))
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);

	ret->path[0] = 0;
	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->path)
		    free(ret->path);
		free(ret);
	}
	return NULL;
}

int proto_ref_typeref_free(proto_ref_typeref_t* ref)
{
	if(NULL == ref)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(NULL != ref->path)
	    free(ref->path);

	free(ref);

	return 0;
}

int proto_ref_typeref_append(proto_ref_typeref_t* ref, const char* segment)
{
	if(NULL == ref || NULL == segment)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	uint32_t seglen = (uint32_t)strlen(segment);

	if(ERROR_CODE(int) == _typeref_ensure_space(ref, seglen))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(ref->size > 0)
	    ref->path[ref->size++] = '/';

	memcpy(ref->path + ref->size, segment, seglen + 1);
	ref->size += seglen;

	return 0;
}

uint32_t proto_ref_typeref_size(const proto_ref_typeref_t* ref)
{
	if(NULL == ref)
	    PROTO_ERR_RAISE_RETURN(uint32_t, ARGUMENT);

	return ref->size;
}

const char* proto_ref_typeref_get_path(const proto_ref_typeref_t* ref)
{
	if(NULL == ref)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	return ref->path;
}

int proto_ref_typeref_dump(const proto_ref_typeref_t* ref, FILE* fp)
{
	if(NULL == ref || NULL == fp)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(1 != fwrite(ref->path, ref->size, 1, fp))
	    PROTO_ERR_RAISE_RETURN(int, WRITE);

	return 0;
}

proto_ref_typeref_t* proto_ref_typeref_load(FILE* fp, uint32_t size)
{
	if(NULL == fp)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	proto_ref_typeref_t* ret = proto_ref_typeref_new(size);

	if(1 != fread(ret->path, size, 1, fp))
	    PROTO_ERR_RAISE_GOTO(ERR, READ);

	ret->size = size;
	ret->path[size] = 0;
	return ret;
ERR:
	if(NULL != ret)
	    proto_ref_typeref_free(ret);
	return NULL;
}
