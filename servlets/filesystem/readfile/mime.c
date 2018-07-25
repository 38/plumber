/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fnmatch.h>

#include <pservlet.h>
#include <pstd.h>

#include <mime.h>

#define HASH_SIZE 997

/**
 * @brief The hash node
 **/
typedef struct _hashnode_t {
	uint64_t            hashcode;            /*!< The hash code for this extension name */
	char*               mimetype;            /*!< The MIME type */
	uint32_t            needs_compress:1;    /*!< If this is compressable */
	struct _hashnode_t* next;                /*!< The next node in the hash table */
} _hashnode_t;

/**
 * @brief The actual data structure for MIME map
 **/
struct _mime_map_t {
	_hashnode_t*   hash_table[HASH_SIZE];     /*!< The hash table */
	const char*    default_mime_type;         /*!< The default MIME type */
};

static inline int _ws(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static inline int _eol(char ch)
{
	return ch == '#' || ch == 0;
}


mime_map_t* mime_map_new(const char* map_file, const char* compress, const char* default_mime_type)
{
	uint32_t i;
	uint32_t n_compress_list = 0;
	char* compress_list_buf = NULL;
	char const* * compress_list = NULL;
	FILE* fp = NULL;

	mime_map_t* ret = (mime_map_t*)malloc(sizeof(*ret));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the MIME map");

	memset(ret->hash_table, 0, sizeof(ret->hash_table));

	ret->default_mime_type = default_mime_type == NULL ? "application/octet-stream" : default_mime_type;


	if(map_file != NULL)
	{
		fp = fopen(map_file, "r");
		if(NULL == fp)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open the mime type file: %s", map_file);


		if(NULL != compress)
		{
			if(NULL == (compress_list_buf = strdup(compress)))
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot dupilcate the compress list");

			char* ptr, *last = compress_list_buf;
			for(ptr = compress_list_buf;; ptr ++)
			{
				if(*ptr == ',' || *ptr == 0)
				{
					if(ptr - last > 0)
						n_compress_list ++;
					last = ptr + 1;
					if(*ptr == 0) break;
				}
			}

			if(NULL == (compress_list = (char const**)malloc(sizeof(char const*) * n_compress_list)))
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate the compress list");

			i = 0;
			for(last = ptr = compress_list_buf;; ptr ++)
			{
				if(*ptr == ',' || *ptr == 0)
				{
					int should_break = (*ptr == 0);
					if(ptr - last > 0)
					{
						*ptr = 0;
						compress_list[i] = last;
						i ++;
					}
					last = ptr + 1;
					if(should_break) break;
				}
			}
		}

		char buf[4096];
		while(NULL != fgets(buf, sizeof(buf), fp))
		{
			char* mime_begin = NULL, *mime_end = NULL;
			char* ext_begin = NULL, *ext_end = NULL;
			/* Skip the leading white space and determine the begining of the mime type section */
			for(mime_begin = buf;!_eol(*mime_begin) && _ws(*mime_begin); mime_begin ++);
			/* Search for the end of the mime type section */
			for(mime_end = mime_begin; !_eol(*mime_end) && !_ws(*mime_end); mime_end ++);

			ext_end = mime_end;

			for(;;)
			{
				/* Skip the whitespaces between the mime type and extension name */
				for(ext_begin = ext_end; !_eol(*ext_begin) && _ws(*ext_begin); ext_begin ++);
				/* The parsing ends when we get the end of the line */
				if(_eol(*ext_begin)) break;
				/* Search for the end of the extension name */
				for(ext_end = ext_begin; !_eol(*ext_end) && !_ws(*ext_end); ext_end ++);

				uint64_t hashcode = 0;
				size_t   len = (size_t)(ext_end - ext_begin);
				memcpy(&hashcode, ext_begin, (len > sizeof(hashcode)) ? sizeof(hashcode) : len);

				_hashnode_t* node = (_hashnode_t*)malloc(sizeof(_hashnode_t));
				if(NULL == node)
					ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the hash table");

				node->hashcode = hashcode;
				if(NULL == (node->mimetype = (char*)malloc((size_t)(mime_end - mime_begin + 1))))
					ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the MIME type string");

				memcpy(node->mimetype, mime_begin, (size_t)(mime_end - mime_begin));

				node->mimetype[mime_end - mime_begin] = 0;
				node->needs_compress = 0;

				node->next = ret->hash_table[hashcode % HASH_SIZE];
				ret->hash_table[hashcode % HASH_SIZE] = node;

				for(i = 0; i < n_compress_list && !node->needs_compress; i ++)
					if(fnmatch(compress_list[i], node->mimetype, 0) == 0)
						node->needs_compress = 1;
			}
		}

		free(compress_list);
		free(compress_list_buf);
		fclose(fp);
	}

	return ret;

ERR:

	for(i = 0; i < HASH_SIZE; i ++)
	{
		_hashnode_t* ptr = ret->hash_table[i];
		for(;NULL != ptr;)
		{
			_hashnode_t* cur = ptr;
			ptr = ptr->next;
			if(NULL != cur->mimetype) free(cur->mimetype);
			free(cur);
		}
	}

	if(NULL != fp) fclose(fp);

	if(NULL != ret) free(ret);

	if(NULL != compress_list) free(compress_list);

	if(NULL != compress_list_buf) free(compress_list_buf);

	return NULL;
}


int mime_map_free(mime_map_t* map)
{
	if(NULL == map)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t i;

	for(i = 0; i < HASH_SIZE; i ++)
	{
		_hashnode_t* ptr = map->hash_table[i];
		for(;NULL != ptr;)
		{
			_hashnode_t* cur = ptr;
			ptr = ptr->next;
			if(NULL != cur->mimetype) free(cur->mimetype);
			free(cur);
		}
	}

	free(map);

	return 0;
}

int mime_map_query(const mime_map_t* map, const char* extname, mime_map_info_t* buf)
{
	if(NULL == map || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	const _hashnode_t* node = NULL;

	if(extname != NULL)
	{

		size_t len = strlen(extname);
		if(len > sizeof(uint64_t)) len = sizeof(uint64_t);

		uint64_t hashcode = 0;
		memcpy(&hashcode, extname, len);

		for(node = map->hash_table[hashcode % HASH_SIZE]; NULL != node && node->hashcode != hashcode; node = node->next);
	}

	if(node == NULL)
	{
		buf->mime_type = map->default_mime_type;
		buf->compressable = 1;
	}
	else
	{
		buf->mime_type = node->mimetype;
		buf->compressable = node->needs_compress;
	}

	return 0;
}
