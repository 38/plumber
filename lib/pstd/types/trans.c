/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pservlet.h>

#include <pstd/types/trans.h>
#include <pstd/mempool.h>
#include <pstd/scope.h>

struct _pstd_trans_t {
	uint32_t             commited:1;   /*!< Indicates if the token is committed */
	uint32_t             opened:1;     /*!< Indicats if this transformer has been opened alreadly */
	pstd_trans_desc_t    ctx;          /*!< The transformer data */
	void*                stream_proc;  /*!< The stream processor instance */
	pstd_scope_stream_t* stream;       /*!< The data source stream */
};

pstd_trans_t* pstd_trans_new(pstd_trans_desc_t desc)
{
	if(desc.src_token == 0 || desc.src_token == ERROR_CODE(scope_token_t))
		ERROR_PTR_RETURN_LOG("Invalid scope token");

	if(desc.init_func == NULL || desc.feed_func == NULL || desc.fetch_func == NULL || desc.cleanup_func == NULL)
		ERROR_PTR_RETURN_LOG("Undefined callback functions");

	pstd_trans_t* ret = (pstd_trans_t*)pstd_mempool_alloc(sizeof(pstd_trans_t));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the stream processor");

	memset(ret, 0, sizeof(pstd_trans_t));

	ret->ctx = desc;

	return ret;
}

static int _free(pstd_trans_t* trans, int app_space)
{
	if(app_space && trans->commited)
		ERROR_RETURN_LOG(int, "Cannot dispose a token has already commited");

	int rc = 0;

	if(trans->stream_proc != NULL && ERROR_CODE(int) == trans->ctx.cleanup_func(trans->stream_proc))
		rc = ERROR_CODE(int);

	if(NULL != trans->stream &&  ERROR_CODE(int) == pstd_scope_stream_close(trans->stream))
		rc = ERROR_CODE(int);

	return rc;
}

int pstd_trans_free(pstd_trans_t* trans)
{
	return _free(trans, 1);
}

scope_token_t pstd_trans_commit(pstd_trans_t* trans);
