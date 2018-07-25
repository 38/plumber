/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <string.h>

#include <constants.h>
#include <utils/vector.h>
#include <utils/log.h>

#include <runtime/api.h>

#include <itc/itc.h>

#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <error.h>

/**
 * @brief The servlet namespace
 **/
static struct {
	vector_t* b_table;   /*!< The binary table */
	vector_t* i_table;   /*!< The instance table */
}
_namespace[2];

/**
 * @brief The current NSID
 **/
static uint32_t _current_nsid = 0;

#define _NS_MASK 0x70000000u

#define _NUM_NS (sizeof(_namespace) / sizeof(_namespace[0]))

#define _NSID(x) ((x & _NS_MASK) > 0)

static int _first_load = 1;
/**
 * @brief Get the servlet from the SID
 * @param sid The servlet ID
 * @return The servlet object, NULL on error case
 **/
static inline runtime_servlet_t* _get_servlet(runtime_stab_entry_t sid)
{
	if(sid == ERROR_CODE(runtime_stab_entry_t))
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	uint32_t nsid = (sid & _NS_MASK) > 0;

	if(nsid >= _NUM_NS || _namespace[nsid].i_table == NULL)
		ERROR_PTR_RETURN_LOG("Invalid servlet ID: namespace not exist");

	vector_t* i_table = _namespace[nsid].i_table;
	sid &= ~_NS_MASK;

	runtime_servlet_t* servlet = *VECTOR_GET_CONST(runtime_servlet_t*, i_table, sid);

	if(NULL == servlet)
		ERROR_PTR_RETURN_LOG("Cannot read the servlet table");

	return servlet;
}

/**
 * @brief Dispose the namespace
 * @param nsid The namespace ID
 * @return status code
 **/
static inline int _dispose_namespace(unsigned nsid)
{
	int rc = 0;
	unsigned i;
	vector_t* b_table = _namespace[nsid].b_table;
	vector_t* i_table = _namespace[nsid].i_table;
	if(NULL != i_table)
	{
		for(i = 0; i < vector_length(i_table); i ++)
			if(ERROR_CODE(int) == runtime_servlet_free(*VECTOR_GET_CONST(runtime_servlet_t*, i_table, i)))
				rc = ERROR_CODE(int);
		if(vector_free(i_table) == ERROR_CODE(int))
			rc = ERROR_CODE(int);
	}

	if(NULL != b_table)
	{
		for(i = 0; i < vector_length(b_table); i ++)
			if(runtime_servlet_binary_unload(*VECTOR_GET_CONST(runtime_servlet_binary_t*, b_table, i)) == ERROR_CODE(int))
				rc = ERROR_CODE(int);

		if(vector_free(b_table) == ERROR_CODE(int))
			rc = ERROR_CODE(int);
	}

	_namespace[nsid].b_table = NULL;
	_namespace[nsid].i_table = NULL;

	return rc;
}

static int _init_namespace(unsigned nsid)
{
	if(NULL == (_namespace[nsid].i_table = vector_new(sizeof(runtime_servlet_t*), RUNTIME_SERVLET_TAB_INIT_SIZE)))
		ERROR_RETURN_LOG(int, "Cannot create the servlet instance list");

	if(NULL == (_namespace[nsid].b_table = vector_new(sizeof(runtime_servlet_binary_t*), RUNTIME_SERVLET_TAB_INIT_SIZE)))
		ERROR_LOG_GOTO(ERR, "Cannot create the servlet binary list");

	return 0;
ERR:
	if(_namespace[nsid].i_table == NULL)
		vector_free(_namespace[nsid].i_table);
	if(_namespace[nsid].b_table == NULL)
		vector_free(_namespace[nsid].b_table);
	return ERROR_CODE(int);
}


int runtime_stab_init()
{
	return _init_namespace(0);
}

int runtime_stab_finalize()
{
	return 0;
}

int runtime_stab_dispose_all_namespaces()
{
	int rc = 0;
	if(ERROR_CODE(int) == _dispose_namespace(0))
		rc = ERROR_CODE(int);

	if(_NUM_NS > 1 && ERROR_CODE(int) == _dispose_namespace(1))
		rc = ERROR_CODE(int);

	return rc;
}

int runtime_stab_dispose_unused_namespace()
{
	unsigned nsid = (unsigned)(_NUM_NS - 1u - _current_nsid);

	return _dispose_namespace(nsid);
}

int runtime_stab_revert_current_namespace()
{
	unsigned nsid = (unsigned)(_NUM_NS - 1u - _current_nsid);
	if(_namespace[nsid].b_table == NULL || _namespace[nsid].i_table == NULL)
		ERROR_RETURN_LOG(int, "Cannot revert current namespace because the unused one is disposed");

	if(ERROR_CODE(int) == _dispose_namespace(_current_nsid))
		ERROR_RETURN_LOG(int, "Cannot dispose current namespace");
	_current_nsid = nsid;
	return 0;
}

runtime_stab_entry_t runtime_stab_load(uint32_t argc, char const * const * argv, const char* path)
{
	if(argc < 1 || argv == NULL || argv[0] == NULL) ERROR_RETURN_LOG(runtime_stab_entry_t, "Invalid arguments");

	const char* name = argv[0];

	unsigned i;
	unsigned nsid = _current_nsid;
	runtime_servlet_binary_t* binary = NULL;

	vector_t* b_table = _namespace[nsid].b_table;
	vector_t* i_table = _namespace[nsid].i_table;

	if(NULL == i_table || NULL == b_table)
		ERROR_RETURN_LOG(runtime_stab_entry_t, "The namespace %u haven't been fully initialized", nsid);

	size_t nentry = vector_length(b_table);

	for(i = 0; i < nentry; i ++)
	{
		binary = *VECTOR_GET_CONST(runtime_servlet_binary_t*, b_table, i);
		if(strcmp(binary->name, name) == 0)
		{
			LOG_DEBUG("Servlet binary %s has been previous loaded", name);
			break;
		}
	}


	if(i == nentry)
	{
		LOG_DEBUG("Could not find the servlet binary %s from the servlet binary table, try to load from disk", name);
		path = path == NULL ? runtime_servlet_find_binary(name) : path;
		if(NULL == path) ERROR_RETURN_LOG(runtime_stab_entry_t, "Could not find any binary for servlet %s", name);

		LOG_DEBUG("Found servlet binary %s matches name %s", path, name);

		binary = runtime_servlet_binary_load(path, name, _first_load);

		if(NULL == binary) ERROR_RETURN_LOG(runtime_stab_entry_t, "Could not load binary %s", path);

		if(NULL == (b_table = vector_append(b_table, &binary)))
			ERROR_RETURN_LOG(runtime_stab_entry_t, "Could not append the newly loaded binary to the binary table");
		else  _namespace[nsid].b_table = b_table;
	}

	runtime_servlet_t* servlet = runtime_servlet_new(binary, argc, argv);

	if(NULL == servlet) ERROR_RETURN_LOG(runtime_stab_entry_t, "Could not create new servlet instance for %s", argv[0]);

	if(NULL == (i_table = vector_append(i_table, &servlet)))
	{
		runtime_servlet_free(servlet);
		ERROR_RETURN_LOG(runtime_stab_entry_t, "Failed to insert the servlet to servlet table");
	}
	else _namespace[nsid].i_table = i_table;

	return ((runtime_stab_entry_t)vector_length(i_table) - 1) | (nsid ? _NS_MASK : 0);
}

runtime_task_t* runtime_stab_create_exec_task(runtime_stab_entry_t sid, runtime_task_flags_t flags)
{
	runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return NULL;

	return runtime_task_new(servlet, (flags & ~RUNTIME_TASK_FLAG_ACTION_MASK) | RUNTIME_TASK_FLAG_ACTION_EXEC);
}

size_t runtime_stab_num_pipes(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(size_t);

	return runtime_pdt_get_size(servlet->pdt);

}

const runtime_pdt_t* runtime_stab_get_pdt(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return NULL;

	return servlet->pdt;
}

runtime_api_pipe_id_t runtime_stab_get_pipe(runtime_stab_entry_t sid, const char* pipe)
{
	if(NULL == pipe) ERROR_RETURN_LOG(runtime_api_pipe_id_t, "Invalid arguments");

	const runtime_pdt_t* pdt = runtime_stab_get_pdt(sid);

	if(NULL == pdt) return ERROR_CODE(runtime_api_pipe_id_t);

	return runtime_pdt_get_pd_by_name(pdt, pipe);
}

runtime_api_pipe_flags_t runtime_stab_get_pipe_flags(runtime_stab_entry_t sid, runtime_api_pipe_id_t pipe)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(runtime_api_pipe_flags_t);

	return runtime_pdt_get_flags_by_pd(servlet->pdt, pipe);
}

int runtime_stab_get_num_input_pipe(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(int);

	return runtime_pdt_input_count(servlet->pdt);

}

int runtime_stab_get_num_output_pipe(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(int);

	return runtime_pdt_output_count(servlet->pdt);

}


const char* runtime_stab_get_description(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return NULL;

	return servlet->bin->define->desc;
}


uint32_t runtime_stab_get_version(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(uint32_t);

	return servlet->bin->define->version;
}

char const* const* runtime_stab_get_init_arg(runtime_stab_entry_t sid, uint32_t* argc)
{
	if(NULL == argc) return NULL;

	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return NULL;

	*argc = servlet->argc;
	return (char const* const*)servlet->argv;
}

const char* runtime_stab_get_binary_path(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);
	if(NULL == servlet) return NULL;

	return servlet->bin->path;
}

int runtime_stab_set_owner(runtime_stab_entry_t sid, const void* owner, int reuse_servlet)
{
	if(ERROR_CODE(runtime_stab_entry_t) == sid || NULL == owner)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet)
		ERROR_RETURN_LOG(int, "No such servlet numbered %u", sid);

	if(NULL != servlet->owner && !reuse_servlet)
		ERROR_RETURN_LOG(int, "Try to set the owner reference, reuse servlet is not allowed at this time");

	servlet->owner = owner;

	return 0;
}

const void* runtime_stab_get_owner(runtime_stab_entry_t sid)
{
	if(ERROR_CODE(runtime_stab_entry_t) == sid)
		return NULL;

	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet)
		return NULL;

	return servlet->owner;
}

int runtime_stab_switch_namespace()
{
	if(_NUM_NS == 1) return 0;

	unsigned new_nsid = (unsigned)(_NUM_NS - 1 - _current_nsid);

	if(NULL != _namespace[new_nsid].b_table || NULL != _namespace[new_nsid].i_table)
		ERROR_RETURN_LOG(int, "The namespace haven't been released");

	_current_nsid = new_nsid;

	if(ERROR_CODE(int) == _init_namespace(new_nsid))
		return ERROR_CODE(int);

	_first_load = 0;
	return 0;
}


