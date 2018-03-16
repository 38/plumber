/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>
#include <pstd/types/file.h>

#include  <mime.h>
#include  <options.h>
#include  <input.h>
#include  <http.h>

static const char _default_301_page[] = "<html><body><cneter><h1>301 Moved</h1></center><hr/></body></html>";
static const char _default_403_page[] = "<html><body><center><h1>403 Forbiden</h1></center><hr/></body></html>";
static const char _default_404_page[] = "<html><body><center><h1>404 Page Not Found</h1></center><hr/></body></html>";
static const char _default_405_page[] = "<html><body><center><h1>405 Method Not Allowed</h1></center><hr/></body></html>";
static const char _default_406_page[] = "<html><body><center><h1>406 Not Acceptable</h1></center><hr/></body></html>";

struct _http_ctx_t {
	pipe_t                      p_file;           /*!< The file pipe */

	size_t                      root_dir_len;     /*!< The length of the root directory */

	mime_map_t*                 mime_map;         /*!< The MIME type mapping */

	options_output_err_page_t   page_not_found;   /*!< The page not found message */
	options_output_err_page_t   page_moved;       /*!< The moved page */
	options_output_err_page_t   page_forbiden;    /*!< The forbiden error page */
	options_output_err_page_t   method_disallowed;/*!< The method not allowed error page */
	options_output_err_page_t   request_rej;      /*!< The request is requested */

	char const* const*          index_page;       /*!< The index page */
	uint32_t                    default_index:1;  /*!< If we need to use the default index.html */
	uint32_t                    allow_range:1;    /*!< If we allow the range request */

	pstd_type_accessor_t        a_status_code;    /*!< The status code accessor */
	pstd_type_accessor_t        a_body_flags;     /*!< The body flags accessor */
	pstd_type_accessor_t        a_body_size;      /*!< The body size accessor */
	pstd_type_accessor_t        a_body_token;     /*!< The body token accessor */
	pstd_type_accessor_t        a_mime_type;      /*!< The MIME type accessor */
	pstd_type_accessor_t        a_redirect;       /*!< The redirect accessor */
	pstd_type_accessor_t        a_range_begin;    /*!< The accessor for the begin of range */
	pstd_type_accessor_t        a_range_end;      /*!< The accessor for the end of the range */
	pstd_type_accessor_t        a_total_size;     /*!< The accessor for the total size of the ranged file */

	uint32_t                    BODY_CAN_COMPRESS;  /*!< The constant indicates that the body can be compressed */
	uint32_t                    BODY_SEEKABLE;      /*!< The constant indicates that the body can be seeked */
	uint32_t                    BODY_RANGED;        /*!< Indicates the body is ranged */

	uint16_t                    HTTP_STATUS_OK;           /*!< OK */
	uint16_t                    HTTP_STATUS_PARTIAL;      /*!< The partial content */
	uint16_t                    HTTP_STATUS_MOVED;        /*!< Moved Permanently */
	uint16_t                    HTTP_STATUS_NOT_FOUND;    /*!< Not found */
	uint16_t                    HTTP_STATUS_FORBIDEN;     /*!< Forbiden */
	uint16_t                    HTTP_STATUS_METHOD_NOT_ALLOWED;  /*!< Method not allowed */
	uint16_t                    HTTP_STATUS_NOT_ACCEPTABLE;      /*!< Request is not acceptable */
};

http_ctx_t* http_ctx_new(const options_t* options, pstd_type_model_t* type_model)
{
	if(NULL == options || NULL == type_model)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	http_ctx_t* ret = (http_ctx_t*)malloc(sizeof(http_ctx_t));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the http reader context");

	if(ERROR_CODE(pipe_t) == (ret->p_file = pipe_define("file", PIPE_OUTPUT, "plumber/std_servlet/network/http/render/v0/Response")))
	    ERROR_LOG_GOTO(ERR, "Cannot declare the http output pipe port");

	ret->page_not_found = options->http_err_not_found;
	ret->page_forbiden = options->http_err_forbiden;
	ret->page_moved = options->http_err_moved;
	ret->method_disallowed = options->http_err_method;
	ret->request_rej = options->http_err_range;
	ret->index_page = (char const* const*)options->index_file_names;
	ret->root_dir_len = strlen(options->root_dir);
	if(ret->root_dir_len > 0 && options->root_dir[ret->root_dir_len - 1] == '/')
	    ret->root_dir_len --;
	ret->mime_map = options->mime_map;
	ret->default_index = options->directory_list_page;
	ret->allow_range = options->allow_range;

	PSTD_TYPE_MODEL(output_model)
	{
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  status.status_code,       ret->a_status_code),
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  body_flags,               ret->a_body_flags),
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  body_size,                ret->a_body_size),
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  body_object,              ret->a_body_token),
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  mime_type.token,          ret->a_mime_type),
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  redirect_location.token,  ret->a_redirect),
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  range_begin,              ret->a_range_begin),
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  range_end,                ret->a_range_end),
		PSTD_TYPE_MODEL_FIELD(ret->p_file,  range_total,              ret->a_total_size),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  BODY_CAN_COMPRESS,        ret->BODY_CAN_COMPRESS),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  status.OK,                ret->HTTP_STATUS_OK),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  status.PARTIAL,           ret->HTTP_STATUS_PARTIAL),             
		PSTD_TYPE_MODEL_CONST(ret->p_file,  status.NOT_FOUND,         ret->HTTP_STATUS_NOT_FOUND),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  status.MOVED_PERMANENTLY, ret->HTTP_STATUS_MOVED),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  status.FORBIDEN,          ret->HTTP_STATUS_FORBIDEN),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  status.METHOD_NOT_ALLOWED,ret->HTTP_STATUS_METHOD_NOT_ALLOWED),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  status.NOT_ACCEPTABLE,    ret->HTTP_STATUS_NOT_ACCEPTABLE),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  BODY_SEEKABLE,            ret->BODY_SEEKABLE),
		PSTD_TYPE_MODEL_CONST(ret->p_file,  BODY_RANGED,              ret->BODY_RANGED)
	};

	if(NULL == PSTD_TYPE_MODEL_BATCH_INIT(output_model, type_model))
	    ERROR_LOG_GOTO(ERR, "Cannot initialize the type model");

	return ret;
ERR:
	free(ret);
	return NULL;
}

int http_ctx_free(http_ctx_t* ctx)
{
	free(ctx);
	return 0;
}

static inline int _write_string_body(const http_ctx_t* ctx, pstd_type_instance_t* type_inst, const char* str, size_t length, const char* mime)
{
	if(ERROR_CODE(int) == pstd_string_create_commit_write(type_inst, ctx->a_body_token, str))
	    ERROR_RETURN_LOG(int, "Cannot write the response content");

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_body_size, (uint64_t)length))
	    ERROR_RETURN_LOG(int, "Cannot write the response size");

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_body_flags, (uint32_t)0))
	    ERROR_RETURN_LOG(int, "Cannot write the body flags");

	if(ERROR_CODE(int) == pstd_string_create_commit_write(type_inst, ctx->a_mime_type, mime))
	    ERROR_RETURN_LOG(int, "Cannot write the MIME type to the response");

	return 0;
}

static inline int _write_file_body(const http_ctx_t* ctx, pstd_type_instance_t* type_inst, 
		                           const char* filename, const char* mime, int compress, 
								   int seekable, off_t start, off_t end, int content)
{
	pstd_file_t* file = pstd_file_new(filename);
	if(NULL == file)
	    ERROR_RETURN_LOG(int, "Cannot create the file object");

	scope_token_t tok = pstd_file_commit(file);
	if(ERROR_CODE(scope_token_t) == tok)
	{
		pstd_file_free(file);
		ERROR_RETURN_LOG(int, "Cannot commit the file object to scope");
	}

	uint32_t body_flags = 0;
	if(compress) body_flags |= ctx->BODY_CAN_COMPRESS;
	if(seekable) body_flags |= ctx->BODY_SEEKABLE;

	size_t length = 0;
	
	if(ERROR_CODE(size_t) == (length = pstd_file_size(file)))
	    ERROR_RETURN_LOG(int, "Cannot determine the size of the file");

	if(start != -1 || end != -1)
	{
		uint64_t left = (start == 0) ? 0 : (uint64_t)start;
		uint64_t right  = (end == -1) ? (uint64_t)-1 : (uint64_t)end;
		if(ERROR_CODE(int) == pstd_file_set_range(file, left, right))
			ERROR_RETURN_LOG(int, "Cannot set the range mask to the file object");

		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_range_begin, left))
			ERROR_RETURN_LOG(int, "Cannot write the range begin");
		
		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_range_end, right))
			ERROR_RETURN_LOG(int, "Cannot write the range begin");
		
		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_total_size, length))
			ERROR_RETURN_LOG(int, "Cannot write the range begin");
		
		length = right - left;

		body_flags |= ctx->BODY_RANGED;
	}

	if(ERROR_CODE(int) == pstd_string_create_commit_write(type_inst, ctx->a_mime_type, mime))
	    ERROR_RETURN_LOG(int, "Cannot write the MIME type to the response");

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_body_size, (uint64_t)length))
	    ERROR_RETURN_LOG(int, "Cannot write the file size to the response");

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_body_flags, body_flags))
	    ERROR_RETURN_LOG(int, "Cannot write the body flag to the response");

	if(content &&  ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_body_token, tok))
	    ERROR_RETURN_LOG(int, "Cannot write the body token to the response");

	return 0;
}

static inline int _write_message_page(const http_ctx_t* ctx, pstd_type_instance_t* type_inst,
                                      uint16_t status_code, const options_output_err_page_t* page,
                                      const char* defval, size_t defsz)
{
	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_status_code, status_code))
	    ERROR_RETURN_LOG(int, "Cannot write the status code");

	struct stat st;

	if(NULL != page->filename && ERROR_CODE(int) != pstd_fcache_stat(page->filename, &st) && (st.st_mode & S_IFREG))
	{
		/* If the page exists */
		if(ERROR_CODE(int) == _write_file_body(ctx, type_inst, page->filename, page->mime_type, page->compressable, 0, -1, -1, 1))
		    ERROR_RETURN_LOG(int, "Cannot write the message page");
	}
	else
	{
		/* Write the default page */
		if(ERROR_CODE(int) == _write_string_body(ctx, type_inst, defval, defsz, "text/html"))
		    ERROR_RETURN_LOG(int, "Cannot write the default error messasge");
	}

	return 0;
}

static inline int _write_default_index(const http_ctx_t* ctx, pstd_type_instance_t* type_inst, const char* path)
{
	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_status_code, ctx->HTTP_STATUS_OK))
	    ERROR_RETURN_LOG(int, "Cannot write the status code");

	DIR* dp = NULL;
	struct dirent* ep = NULL;
	pstd_string_t* result_str = NULL;

	if(NULL == (dp = opendir(path)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot open directory %s", path);

	if(NULL == (result_str = pstd_string_new(1024)))
	    ERROR_LOG_GOTO(ERR, "Cannot create new string object");

	if(ERROR_CODE(size_t) == pstd_string_printf(result_str, "<html><head><title>Directory Listing of %s</title></head>"
	                                                              "<body><h1>Directory Listing of %s</h1><hr><ul>",
	                                            path + ctx->root_dir_len,
	                                            path + ctx->root_dir_len))
	    ERROR_LOG_GOTO(ERR, "Cannot generate the directory listing page");

	size_t len = strlen(path);
	const char* sep = "";
	if(len > 0 && path[len - 1] != '/')
	    sep = "/";

	while((ep = readdir(dp)) != NULL)
	{
		const char* suffix = "";
		if(ep->d_type & DT_DIR)
		    suffix = "/";
		if(ERROR_CODE(size_t) == pstd_string_printf(result_str, "<li><a href=\"%s%s%s\">%s%s</a></li>",
		                                            path + ctx->root_dir_len, sep, ep->d_name, ep->d_name, suffix))
		    ERROR_LOG_GOTO(ERR, "Cannot generate the directory listing page");
	}

	if(ERROR_CODE(size_t) == pstd_string_printf(result_str, "</ul></body></html>"))
	    ERROR_LOG_GOTO(ERR, "Cannot generate the directory listing page");

	if(closedir(dp) < 0)
	    ERROR_LOG_GOTO(ERR, "Cannot close the directory pointer");

	dp = NULL;

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_body_size, pstd_string_length(result_str)))
	    ERROR_RETURN_LOG(int, "Cannot write the response size");

	scope_token_t tok = pstd_string_commit(result_str);
	if(ERROR_CODE(scope_token_t) == tok)
	    ERROR_LOG_GOTO(ERR, "Cannot commit the result string to the scope");

	result_str = NULL;

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_body_flags, (uint32_t)ctx->BODY_CAN_COMPRESS))
	    ERROR_RETURN_LOG(int, "Cannot write the body flags");

	if(ERROR_CODE(int) == pstd_string_create_commit_write(type_inst, ctx->a_mime_type, "text/html"))
	    ERROR_RETURN_LOG(int, "Cannot write the MIME type to the response");

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_body_token, tok))
	    ERROR_RETURN_LOG(int, "Cannot write body token");

	return 0;

ERR:
	if(NULL != dp) closedir(dp);
	if(NULL != result_str) pstd_string_free(result_str);
	return ERROR_CODE(int);
}

int http_ctx_exec(const http_ctx_t* ctx, pstd_type_instance_t* type_inst, const char* path, const char* extname, const input_metadata_t* meta)
{
	if(NULL == ctx || NULL == type_inst || NULL == path || NULL == meta)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(meta->disallowed)
	{
		if(ERROR_CODE(int) == _write_message_page(ctx, type_inst,
					                              ctx->HTTP_STATUS_METHOD_NOT_ALLOWED, &ctx->method_disallowed,
												  _default_405_page, sizeof(_default_405_page) - 1))
			ERROR_RETURN_LOG(int, "Cannot write the message page");
		return 0;
	}

	if(path[0] == 0)
	{
		if(ERROR_CODE(int) == _write_message_page(ctx, type_inst,
		                                          ctx->HTTP_STATUS_FORBIDEN, &ctx->page_forbiden,
		                                          _default_403_page, sizeof(_default_403_page) - 1))
		    ERROR_RETURN_LOG(int, "Cannnot write the message page");
		return 0;
	}

	struct stat st;
	if(ERROR_CODE(int) == pstd_fcache_stat(path, &st))
	{
		if(ERROR_CODE(int) == _write_message_page(ctx, type_inst,
		                                          ctx->HTTP_STATUS_NOT_FOUND, &ctx->page_not_found,
		                                          _default_404_page, sizeof(_default_404_page) - 1))
		    ERROR_RETURN_LOG(int, "Cannnot write the message page");
		return 0;
	}

	char buf[PATH_MAX + 1];

	if((st.st_mode & S_IFDIR))
	{
		size_t len = strlen(path);
		uint32_t i;

		if(len > PATH_MAX) len = PATH_MAX;
		memcpy(buf, path, len);

		if((len == 0 || buf[len - 1] != '/') && len < PATH_MAX)
		    buf[len++] = '/';

		for(i = 0; ctx->index_page && ctx->index_page[i]; i ++)
		{
			size_t bytes_avail = sizeof(buf) - len - 1;
			snprintf(buf + len, bytes_avail, "%s", ctx->index_page[i]);

			if(ERROR_CODE(int) == pstd_fcache_stat(buf, &st))
			    continue;

			if((st.st_mode & (S_IFREG | S_IFLNK)))
			{
				path = buf;
				break;
			}
		}

		if(path != buf)
		{
			if(ctx->default_index)
			{
				if(ERROR_CODE(int) == _write_default_index(ctx, type_inst, path))
				    ERROR_RETURN_LOG(int, "Cannot write the default index");
				return 0;
			}
			else
			{
				if(ERROR_CODE(int) == _write_message_page(ctx, type_inst,
				                                          ctx->HTTP_STATUS_NOT_FOUND, &ctx->page_not_found,
				                                          _default_404_page, sizeof(_default_404_page) - 1))
				    ERROR_RETURN_LOG(int, "Cannnot write the message page");
				return 0;
			}
		}
		else
		{
			if(ERROR_CODE(int) == _write_message_page(ctx, type_inst,
			                                          ctx->HTTP_STATUS_MOVED, &ctx->page_moved,
			                                          _default_301_page, sizeof(_default_301_page) - 1))
			    ERROR_RETURN_LOG(int, "Cannnot write the message page");

			if(ERROR_CODE(int) == pstd_string_copy_commit_write(type_inst, ctx->a_redirect, path + ctx->root_dir_len))
			    ERROR_RETURN_LOG(int, "Cannot write the message page");
			return 0;
		}
	}

	int partial = 0;
	off_t start = -1, end = -1;

	if(ctx->allow_range && meta->partial)
	{
		start = (off_t)meta->begin;
		end = (off_t)meta->end;
		if(end == -1)
			end = st.st_size;

		if(start > end || start > st.st_size || end > st.st_size)
		{
			if(ERROR_CODE(int) == _write_message_page(ctx, type_inst,
						                              ctx->HTTP_STATUS_NOT_ACCEPTABLE, &ctx->request_rej,
													  _default_406_page, sizeof(_default_406_page) - 1))
				ERROR_RETURN_LOG(int, "Cannto write the message page");
			return 0;
		}
		if(start != 0 || end != st.st_size)
			partial = 1;
		else
			start = end = (off_t)-1;
	}

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_status_code, partial ? ctx->HTTP_STATUS_PARTIAL : ctx->HTTP_STATUS_OK))
	    ERROR_RETURN_LOG(int, "Cannot write the status code");

	mime_map_info_t info;
	if(ERROR_CODE(int) == mime_map_query(ctx->mime_map, extname, &info))
	    ERROR_RETURN_LOG(int, "Cannot query the MIME type mapping");

	if(ERROR_CODE(int) == _write_file_body(ctx, type_inst, path, info.mime_type, info.compressable, ctx->allow_range, start, end, meta->content))
	    ERROR_RETURN_LOG(int, "Cannot write the file content to the response");

	return 0;
}
