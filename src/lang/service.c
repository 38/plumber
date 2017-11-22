/**
 * Coypright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <error.h>

#include <utils/static_assertion.h>
#include <utils/log.h>

#include <itc/module_types.h>
#include <itc/module.h>
#include <itc/equeue.h>

#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <sched/service.h>
#include <sched/loop.h>
#include <sched/daemon.h>

#include <lang/service.h>

STATIC_ASSERTION_EQ_ID(__check_nid_fits_init64__, (int64_t)(sched_service_node_id_t)-1, (sched_service_node_id_t)-1);
STATIC_ASSERTION_EQ_ID(__check_pid_fits_inta64__, (int64_t)(runtime_api_pipe_id_t)-1, (runtime_api_pipe_id_t)-1);
STATIC_ASSERTION_EQ_ID(__check_sid_fits_inta64__, (int64_t)(runtime_stab_entry_t)-1, (runtime_stab_entry_t)-1);


/**
 * @brief The actual data strcture for a service
 **/
struct _lang_service_t {
	uint32_t    is_buffer:1;   /*!< Indicate if this service object is still a service buffer */
	union {
		sched_service_buffer_t* buffer;   /*!< The service buffer object */
		sched_service_t*        object;   /*!< The service object */
	};

	sched_service_node_id_t  sid_cap;   /*!< The capacity of the SID map */
	runtime_stab_entry_t* sid_map;  /*!< The actual SID map */
};


lang_service_t* lang_service_new()
{
	lang_service_t* ret = (lang_service_t*)malloc(sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the service builtin object");

	ret->sid_cap = 32;
	if(NULL == (ret->sid_map = (runtime_stab_entry_t*)malloc(sizeof(ret->sid_map[0]) * ret->sid_cap)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the SID map");
	memset(ret->sid_map, -1, sizeof(ret->sid_map[0]) * ret->sid_cap);

	if(NULL == (ret->buffer = sched_service_buffer_new()))
	    ERROR_LOG_GOTO(ERR, "Cannot create the service buffer");

	ret->is_buffer = 1;
	return ret;
ERR:
	if(NULL != ret->sid_map) free(ret->sid_map);
	free(ret);
	return NULL;
}

int lang_service_free(lang_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;
	if(service->is_buffer && ERROR_CODE(int) == sched_service_buffer_free(service->buffer))
	    rc = ERROR_CODE(int);

	if(!service->is_buffer && ERROR_CODE(int) == sched_service_free(service->object))
	    rc = ERROR_CODE(int);

	free(service->sid_map);

	free(service);

	return rc;
}

int64_t lang_service_add_node(lang_service_t* service, const char* init_args)
{
	if(NULL == service || service->is_buffer == 0 || NULL == init_args)
	    ERROR_RETURN_LOG(int64_t, "Invalid arguments");

	uint32_t argc = 0;
	const char* ptr;
	for(ptr = init_args; *ptr;)
	{
		for(;*ptr == ' ' || *ptr == '\t'; ptr ++);
		int escape = 0;
		int empty = 1;
		for(;*ptr && !((*ptr == ' ' || *ptr == '\t') && !escape); ptr ++, empty = 0)
		    if(!escape) escape = (*ptr == '\\');
		    else escape = 0;
		if(!empty) argc ++;
	}

	size_t len = strlen(init_args);
	char  buf[len + 1];
	char const* argv[argc];
	size_t scanned, argidx, processed;
	memcpy(buf, init_args, len + 1);
	for(scanned = argidx = processed = 0; scanned < len; scanned ++)
	{
		for(;buf[scanned] == ' ' || buf[scanned] == '\t'; scanned ++);
		argv[argidx] = buf + processed;
		int escape = 0;
		for(;buf[scanned] && !((buf[scanned] == ' ' || buf[scanned] == '\t') && !escape); scanned ++)
		{
			if(!escape) escape = (buf[scanned] == '\\');
			else escape = 0;
			if(!escape) buf[processed++] = buf[scanned];
		}
		if(processed > 0)
		{
			buf[processed++] = 0;
			argidx ++;
		}
	}

	runtime_stab_entry_t sid = runtime_stab_load(argc, argv, NULL);
	if(ERROR_CODE(runtime_stab_entry_t) == sid)
	    ERROR_RETURN_LOG(int64_t, "Cannot not load servlet with init args: %s", init_args);

	sched_service_node_id_t nid = sched_service_buffer_add_node(service->buffer, sid);
	if(ERROR_CODE(sched_service_node_id_t) == nid)
	    ERROR_RETURN_LOG(int64_t, "Cannot add new node to the service buffer");

	sched_service_node_id_t new_cap = service->sid_cap;
	while(nid >= new_cap)
	{
		if((ERROR_CODE(sched_service_node_id_t) >> 1) >= new_cap)
		    new_cap *= 2;
		else
		    new_cap = ERROR_CODE(sched_service_node_id_t);
	}

	if(new_cap != service->sid_cap)
	{
		runtime_stab_entry_t* sid_map;
		if(NULL == (sid_map = (runtime_stab_entry_t*)realloc(service->sid_map, sizeof(sid_map[0]) * new_cap)))
		    ERROR_RETURN_LOG(int64_t, "Cannot resize the SID map");
		memset(sid_map + service->sid_cap, -1, sizeof(sid_map[0]) * (new_cap - service->sid_cap));
		service->sid_map = sid_map;
		service->sid_cap = new_cap;

	}

	service->sid_map[nid] = sid;

	return (int64_t)nid;
}

char** lang_service_node_port_names(const lang_service_t* service, int64_t nid)
{
	if(NULL == service || !service->is_buffer || nid < 0 || nid >= ERROR_CODE(sched_service_node_id_t))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(nid >= service->sid_cap || service->sid_map[nid] == ERROR_CODE(runtime_stab_entry_t))
	    ERROR_PTR_RETURN_LOG("Node %u not exist", (uint32_t)nid);

	int input_size = runtime_stab_get_num_input_pipe(service->sid_map[nid]);
	if(ERROR_CODE(int) == input_size)
	    ERROR_PTR_RETURN_LOG("Cannot get the number of input pipes");
	int output_size = runtime_stab_get_num_output_pipe(service->sid_map[nid]);
	if(ERROR_CODE(int) == output_size)
	    ERROR_PTR_RETURN_LOG("Cannot get the number of output pipes");

	char** ret = (char**)calloc((size_t)(input_size + output_size + 2), sizeof(char*));

	int i, ic, oc;
	for(i = ic = oc = 0; i < input_size + output_size; i ++)
	{
		runtime_api_pipe_id_t pid = (runtime_api_pipe_id_t)i;
		runtime_api_pipe_flags_t flags = runtime_stab_get_pipe_flags(service->sid_map[nid], pid);
		if(ERROR_CODE(runtime_api_pipe_flags_t) == flags)
		    ERROR_LOG_GOTO(ERR, "Cannot access the flag of pipe <SID=%u, PID=%u>", service->sid_map[nid], pid);

		int idx = -1;
		if(RUNTIME_API_PIPE_IS_INPUT(flags))
		    idx = ic ++;
		else if(RUNTIME_API_PIPE_IS_OUTPUT(flags))
		    idx = (oc ++) + input_size + 1;

		if(idx >= 0)
		{
			const runtime_pdt_t* pdt = runtime_stab_get_pdt(service->sid_map[nid]);
			if(NULL == pdt) ERROR_LOG_GOTO(ERR, "Cannot get the PDT for servlet %u", service->sid_map[nid]);
			const char* name = runtime_pdt_get_name(pdt, pid);
			if(NULL == name) ERROR_LOG_GOTO(ERR, "Cannot get the name of pipe <SID=%u, PID=%u>", service->sid_map[nid], pid);

			if(NULL == (ret[idx] = strdup(name)))
			    ERROR_LOG_GOTO(ERR, "Cannot duplicate the pipe name");
		}
	}

	return ret;
ERR:
	if(ret != NULL)
	{
		uint32_t j;
		for(j = 0; j < (size_t)(input_size + output_size + 2); j ++)
		    if(NULL != ret[j]) free(ret[j]);
		free(ret);
	}
	return NULL;
}

int lang_service_add_edge(lang_service_t* service, int64_t src_nid, const char* src_port, int64_t dst_nid, const char* dst_port)
{
	if(NULL == service || src_nid < 0 || src_nid >= ERROR_CODE(sched_service_node_id_t) ||
	  dst_nid < 0 || dst_nid >= ERROR_CODE(sched_service_node_id_t) ||
	  NULL == src_port || NULL == dst_port) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(src_nid >= service->sid_cap || service->sid_map[src_nid] == ERROR_CODE(runtime_stab_entry_t))
	    ERROR_RETURN_LOG(int, "Node %u not exist", (uint32_t)src_nid);

	if(dst_nid >= service->sid_cap || service->sid_map[dst_nid] == ERROR_CODE(runtime_stab_entry_t))
	    ERROR_RETURN_LOG(int, "Node %u not exist", (uint32_t)dst_nid);

	const runtime_pdt_t* src_pdt = runtime_stab_get_pdt(service->sid_map[src_nid]);
	if(NULL == src_pdt) ERROR_RETURN_LOG(int, "Cannot get the PDT for node %u", service->sid_map[src_nid]);
	const runtime_pdt_t* dst_pdt = runtime_stab_get_pdt(service->sid_map[dst_nid]);
	if(NULL == dst_pdt) ERROR_RETURN_LOG(int, "Cannot get the PDT for node %u", service->sid_map[dst_nid]);

	runtime_api_pipe_id_t src_pid = runtime_pdt_get_pd_by_name(src_pdt, src_port);
	if(ERROR_CODE(runtime_api_pipe_id_t) == src_pid) ERROR_RETURN_LOG(int, "Cannot get the PID for the pipe named %s", src_port);
	runtime_api_pipe_id_t dst_pid = runtime_pdt_get_pd_by_name(dst_pdt, dst_port);
	if(ERROR_CODE(runtime_api_pipe_id_t) == dst_pid) ERROR_RETURN_LOG(int, "Cannot get the PID for the pipe named %s", dst_port);

	sched_service_pipe_descriptor_t pd = {
		.source_node_id = (sched_service_node_id_t)src_nid,
		.source_pipe_desc = src_pid,
		.destination_node_id = (sched_service_node_id_t)dst_nid,
		.destination_pipe_desc = dst_pid
	};

	return sched_service_buffer_add_pipe(service->buffer, pd);
}

static inline int _set_service_input_or_output(lang_service_t* service, int64_t nid, const char* port, int input)
{
	if(NULL == service || nid < 0 || nid >= ERROR_CODE(sched_service_node_id_t) || NULL == port)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(nid >= service->sid_cap || service->sid_map[nid] == ERROR_CODE(runtime_stab_entry_t))
	    ERROR_RETURN_LOG(int, "Node %u not exist", (uint32_t)nid);

	const runtime_pdt_t* pdt = runtime_stab_get_pdt(service->sid_map[nid]);
	if(NULL == pdt) ERROR_RETURN_LOG(int, "Cannot get the PDT for node %u", service->sid_map[nid]);

	runtime_api_pipe_id_t pid = runtime_pdt_get_pd_by_name(pdt, port);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pid) ERROR_RETURN_LOG(int, "Cannot get the PID for the pipe named %s", port);

	if(input)
	    return sched_service_buffer_set_input(service->buffer, (sched_service_node_id_t)nid, pid);
	else
	    return sched_service_buffer_set_output(service->buffer, (sched_service_node_id_t)nid, pid);
}
int lang_service_set_input(lang_service_t* service, int64_t nid, const char* port)
{
	return _set_service_input_or_output(service, nid, port, 1);
}

int lang_service_set_output(lang_service_t* service, int64_t nid, const char* port)
{
	return _set_service_input_or_output(service, nid, port, 0);
}

const char* lang_service_get_type(lang_service_t* service, int64_t nid, const char* port)
{
	if(NULL == service || nid >= ERROR_CODE(sched_service_node_id_t) || NULL == port)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(service->is_buffer)
	{
		sched_service_t* service_obj = sched_service_from_buffer(service->buffer);
		if(NULL == service_obj)
		    ERROR_PTR_RETURN_LOG("Cannot build the service object from the service buffer");

		if(ERROR_CODE(int) == sched_service_buffer_free(service->buffer))
		    LOG_WARNING("Cannot dispose the used service buffer");

		service->is_buffer = 0;
		service->object = service_obj;
	}

	const runtime_pdt_t* pdt = runtime_stab_get_pdt(service->sid_map[nid]);

	if(NULL == pdt) ERROR_PTR_RETURN_LOG("Cannot get the PDT for node %u", service->sid_map[nid]);

	runtime_api_pipe_id_t pid = runtime_pdt_get_pd_by_name(pdt, port);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pid) ERROR_PTR_RETURN_LOG("Cannot get the PID for the pipe named %s", port);

	const char* type_expr = NULL;

	if(ERROR_CODE(int) == sched_service_get_pipe_type(service->object, (sched_service_node_id_t)nid, pid, &type_expr))
	    ERROR_PTR_RETURN_LOG("Cannot get the concerete type of the service");

	if(NULL == type_expr) return UNTYPED_PIPE_HEADER;
	return type_expr;
}

int lang_service_start(lang_service_t* service, int fork_twice)
{
	if(NULL == service)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(service->is_buffer)
	{
		sched_service_t* service_obj = sched_service_from_buffer(service->buffer);
		if(NULL == service_obj)
		    ERROR_RETURN_LOG(int, "Cannot build the service object from the service buffer");

		if(ERROR_CODE(int) == sched_service_buffer_free(service->buffer))
		    LOG_WARNING("Cannot dispose the used service buffer");

		service->is_buffer = 0;
		service->object = service_obj;
	}

	if(ERROR_CODE(int) == sched_loop_start(&service->object, fork_twice))
	    ERROR_RETURN_LOG(int, "Cannot start the service");

	return 0;
}

int lang_service_reload(const char* daemon, lang_service_t* service)
{
	if(NULL == daemon || NULL == service)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(service->is_buffer)
	{
		sched_service_t* service_obj = sched_service_from_buffer(service->buffer);
		if(NULL == service_obj)
		    ERROR_RETURN_LOG(int, "Cannot build the servicec object from the service buffer");
		if(ERROR_CODE(int) == sched_service_buffer_free(service->buffer))
		    LOG_WARNING("Cannot dispose the used service buffer");

		service->is_buffer = 0;
		service->object = service_obj;
	}

	return sched_daemon_reload(daemon, service->object);
}

