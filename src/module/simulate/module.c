/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>

#include <error.h>
#include <utils/log.h>
#include <itc/module_types.h>
#include <module/simulate/module.h>
#include <module/simulate/api.h>

/**
 * @brief represent a simulated event, which is defined by the event file
 **/
typedef struct _event_t {
	char*                 label;  /*!< The label for this event t */
	size_t                size;   /*!< The size of this event */
	char*                 data;   /*!< The data for this event */
	size_t                outcap; /*!< The capacity of the output buffer */
	size_t                outsize;/*!< The size of the output */
	char*                 outbuf; /*!< The output buffer */
	struct _event_t*      next;   /*!< The next event in the event list */
} _event_t;

/**
 * @brief The module context
 **/
typedef struct {
	_event_t*      event_list_tail;  /*!< The tail of the event list */
	_event_t*      event_list_head;  /*!< The event we want to simulate */
	_event_t*      next_event;       /*!< The next event we want to raise */
	char*          label;            /*!< The label for this module instance */
	char*          outfile;          /*!< The file we want to dump the output to */
	uint32_t       events_per_sec;   /*!< The rate of how many events will be poped up per seconds */
	uint32_t       remaining;        /*!< How many events is currently not closed */
	uint32_t       terminate:1;      /*!< Indicates this event is actuall terminate the platform */
	uint64_t       last_event_ts;    /*!< The timestamp of the last event */
} _module_context_t;

/**
 * @brief The pipe handle
 **/
typedef struct {
	uint32_t             output:1;/*!< If this is the output */
	_event_t*            event;   /*!< The event we are handling */
	size_t               offset;  /*!< The offset for where we are */
} _handle_t;

static inline int _start_with(const char* str, const char* pref)
{
	for(;*str != 0 && *pref != 0 && *str == *pref;str ++, pref++);
	return *pref == 0;
}

static inline int _parse_comment(FILE* fp)
{
	for(;;)
	{
		int ch = fgetc(fp);
		if(EOF == ch && ferror(fp)) ERROR_RETURN_LOG_ERRNO(int, "Cannot read file");
		if(EOF == ch || ch == '\n') break;
	}

	return 0;
}

static inline void _append_event(_module_context_t* ctx, _event_t* event)
{
	if(ctx->event_list_head == NULL) ctx->event_list_head = ctx->event_list_tail = event;
	else
	{
		ctx->event_list_tail->next = event;
		event->next = NULL;
		ctx->event_list_tail = event;
	}
	ctx->remaining ++;
}

static inline int _parse_command(_module_context_t* ctx, FILE* fp)
{
	char buf[1024];
	if(NULL == fgets(buf, sizeof(buf), fp))
	    ERROR_RETURN_LOG(int, "Unexpected EOF");
	char const* command = NULL;
	char const* label = NULL;

	char* ptr;
	for(ptr = buf; !(*ptr == 0 || *ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n'); ptr ++);
	command = buf;
	if(*ptr != 0)
	{
		*ptr = 0;
		for(ptr ++; *ptr == ' ' || *ptr == '\t'; ptr++);
		label = ptr;
		for(;!(*ptr == 0 || *ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r'); ptr ++);
		ptr[0] = 0;
	}


	int is_text = (strcmp(command, "TEXT") == 0);
	int is_file = (!is_text && strcmp(command, "FILE") == 0);

	if(!is_text && !is_file && strcmp(command, "STOP") == 0)
	{
		ctx->terminate = 1;
		return 0;
	}

	if(label[0] == 0) ERROR_RETURN_LOG(int, "Missing event label");

	LOG_DEBUG("Data %s event section %s", command, label);

	if(label == NULL || label[0] == 0)
	    ERROR_RETURN_LOG(int, "Missing event label: it should follows pattern .<command> <label>");

	if(!is_text && !is_file) ERROR_RETURN_LOG(int, "Invalid event type %s", command);

	size_t size, len = 0;
	char* event_data = (char*)malloc(size = 32);
	if(NULL == event_data) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the event body");

	static const char* event_end = "\n.END";
	const char* state = event_end;

	for(;;)
	{
		int ch = fgetc(fp);
		if(EOF == ch && ferror(fp))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the event file");
		if(EOF == ch && *state != 0)
		    ERROR_LOG_GOTO(ERR, "Unexpected EOF while .end directive is expected");
		if(*state == 0)
		{
			while(ch == ' ' || ch == '\t' || ch == '\r')
			    ch = fgetc(fp);
			if(ch == EOF || ch == '\n') break;
			else goto FLUSH;
		}
		else
		{
			if(*state == ch)
			{
				state ++;
				continue;
			}
			size_t delta, new_size;
FLUSH:
			delta = (size_t)(state - event_end);
			new_size = size;
			while(len + delta + 1 >= new_size)
			    new_size *= 2;
			if(new_size != size)
			{
				char* new_data = (char*)realloc(event_data, new_size);
				if(NULL == new_data) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot resize the event data buffer");
				size  = new_size;
				event_data = new_data;
			}
			const char* p;
			for(p = event_end; p < state; p ++)
			    event_data[len++] = *p;
			event_data[len++] = (char)ch;
			event_data[len] = 0;
			state = event_end;
		}
	}

	_event_t* event = (_event_t*)calloc(sizeof(*event), 1);
	if(NULL == event) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the event");

	if(NULL == (event->label = strdup(label)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the label name");

	if(is_text)
	{
		event->data = event_data;
		event->size = len;
	}
	else if(is_file)
	{
		struct stat st;
		if(stat(event_data, &st) < 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the stat of the file %s", event_data);

		size_t data_size = (size_t)st.st_size;

		char* data_buffer = (char*)malloc(data_size);
		FILE* data_fp = fopen(event_data, "rb");
		if(data_fp == NULL)
		    ERROR_LOG_ERRNO_GOTO(FILE_ERR, "Cannot open the data file %s", event_data);

		event->data = data_buffer;

		for(;data_size > 0;)
		{
			size_t bytes_written = fread(data_buffer, 1, data_size, data_fp);
			if(bytes_written == 0)
			    ERROR_LOG_ERRNO_GOTO(FILE_ERR, "Cannot read the data from file %s", event_data);
			data_size -= bytes_written;
			data_buffer += bytes_written;
		}
		event->size = (size_t)st.st_size;
		fclose(data_fp);
		free(event_data);
		goto EXIT_NORMALLY;
FILE_ERR:
		if(NULL != event->data) free(event->data);
		if(NULL != data_fp) fclose(data_fp);
		goto ERR;
	}
	else ERROR_LOG_ERRNO_GOTO(ERR, "Code bug!");

EXIT_NORMALLY:
	_append_event(ctx, event);

	return 0;
ERR:
	if(NULL != event_data) free(event_data);
	return ERROR_CODE(int);
}

static inline int _parse_input(_module_context_t* ctx, FILE* fp)
{
	for(;;)
	{
		int ch = fgetc(fp);
		if(EOF == ch)
		{
			if(ferror(fp)) ERROR_RETURN_LOG_ERRNO(int, "Cannot read file");
			else break;
		}

		switch(ch)
		{
			case '#':
			    if(ERROR_CODE(int) == _parse_comment(fp))
			        ERROR_RETURN_LOG(int, "Cannot parse the comment");
			    break;
			case '.':
			    if(ERROR_CODE(int) == _parse_command(ctx, fp))
			        ERROR_RETURN_LOG(int, "Cannot parse the command");
			    break;
			case ' ':
			case '\t':
			case '\n':
			case '\r':
			    break;
			default:
			    ERROR_RETURN_LOG(int, "Invalid sytax");
		}
	}

	LOG_DEBUG("%u events has been loaded from file", ctx->remaining);

	return 0;
}

static int _init(void* __restrict ctxbuf, uint32_t argc, char const* __restrict const* __restrict argv)
{
	_module_context_t* ctx = (_module_context_t*)ctxbuf;
	if(NULL == ctx) ERROR_RETURN_LOG(int, "Invalid arguments");

	memset(ctx, 0, sizeof(*ctx));

	const char input_prefix[] = "input=";
	const char output_prefix[] = "output=";
	const char id_prefix[] = "label=";

	const char* input_name = NULL;
	const char* output_name = NULL;
	const char* id = NULL;

	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		if(_start_with(argv[i], input_prefix)) input_name = argv[i] + sizeof(input_prefix) - 1;
		else if(_start_with(argv[i], output_prefix)) output_name = argv[i] + sizeof(output_prefix) - 1;
		else if(_start_with(argv[i], id_prefix)) id = argv[i] + sizeof(id_prefix) - 1;
		else ERROR_RETURN_LOG(int, "Invalid module initialization arguments, expected: simulate input=inputfile output=outputfile label=labelname");
	}

	if(input_name == NULL || input_name[0] == 0)
	    ERROR_RETURN_LOG(int, "Missing input file name");
	if(output_name == NULL || output_name[0] == 0)
	    ERROR_RETURN_LOG(int, "Missing output file name");
	if(id == NULL || id[0] == 0)
	    ERROR_RETURN_LOG(int, "Missing label of the module");

	if(NULL == (ctx->label = strdup(id)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the ID label");

	/* Then let's parse the input file */
	FILE* fp = fopen(input_name, "r");
	if(NULL == fp) ERROR_RETURN_LOG_ERRNO(int, "Cannot open the input file %s", input_name);

	if(ERROR_CODE(int) == _parse_input(ctx, fp))
	{
		fclose(fp);
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot pasrse the input");
	}

	fclose(fp);

	if(NULL == (ctx->outfile = strdup(output_name)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open the output file %s", output_name);

	LOG_DEBUG("Event Simulation Module has been initialized, input = %s, output = %s", input_name, output_name);

	ctx->next_event = ctx->event_list_head;

	setpgrp();

	return 0;
ERR:
	return ERROR_CODE(int);
}

static inline int _cleanup(void* __restrict ctxbuf)
{
	int ret = 0;
	FILE* fout = NULL;
	_module_context_t* ctx = (_module_context_t*)ctxbuf;
	if(ctx->label != NULL) free(ctx->label);
	if(ctx->outfile != NULL && NULL == (fout = fopen(ctx->outfile, "wb")))
	{
		LOG_ERROR_ERRNO("Cannot open the output file");
		ret = ERROR_CODE(int);
	}
	if(ctx->outfile != NULL) free(ctx->outfile);
	for(;ctx->event_list_head != NULL;)
	{
		_event_t* event = ctx->event_list_head;
		ctx->event_list_head = ctx->event_list_head->next;
		if(event->label != NULL && event->outbuf != NULL)
		{
			size_t bytes_to_write = event->outsize;
			const char* data_buf = event->outbuf;
			fprintf(fout, ".OUTPUT %s\n", event->label);
			for(;bytes_to_write > 0;)
			{
				size_t rc = fwrite(data_buf, 1, bytes_to_write, fout);
				if(rc == 0 && ferror(fout))
				{
					LOG_ERROR_ERRNO("Cannot write to the output file");
					ret = ERROR_CODE(int);
					break;
				}
				data_buf += rc;
				bytes_to_write -= rc;
			}
			fprintf(fout, "\n.END\n");
			free(event->outbuf);
			LOG_DEBUG("Dumped simulated event output %s", event->label);
		}
		else if(event->label != NULL) LOG_DEBUG("Skip untouched simulated event %s", event->label);
		if(event->data != NULL) free(event->data);
		if(event->label != NULL) free(event->label);
		free(event);
	}
	if(NULL != fout) fclose(fout);
	return ret;
}

static const char* _get_path(void* __restrict ctx, char* buf, size_t sz)
{
	_module_context_t* context = (_module_context_t*)ctx;
	snprintf(buf, sz, "%s", context->label);
	return buf;
}

static int _accept(void* __restrict ctxbuf, const void* __restrict args, void* __restrict inbuf, void* __restrict outbuf)
{
	(void)args;
	_module_context_t* ctx = (_module_context_t*)ctxbuf;
	if(NULL == ctx) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(NULL == ctx->next_event)
	{
		LOG_NOTICE("Event exhausted, terminating the event loop");
		return ERROR_CODE(int);
	}

	/* If we have finite events per second rate, we need to wait */
	if(ctx->events_per_sec != 0)
	{
		struct timespec time;
		clock_gettime(CLOCK_REALTIME, &time);

		uint64_t ts = ((uint64_t)time.tv_sec * 1000000000ull) + (uint64_t)time.tv_nsec;
		uint64_t interval = 1000000000ull / ctx->events_per_sec;
		uint64_t time_to_sleep = (ctx->last_event_ts + interval <= ts) ? 0 : ctx->last_event_ts + interval - ts;

		if(time_to_sleep > 0)
		    usleep((unsigned)time_to_sleep / 1000);

		if(ctx->last_event_ts == 0) ctx->last_event_ts = ts;
		else ctx->last_event_ts += interval;
	}

	_handle_t* in = (_handle_t*)inbuf;
	_handle_t* out = (_handle_t*)outbuf;

	in->output = 0;
	out->output = 1;

	in->event = out->event = ctx->next_event;
	in->offset = out->offset = 0;

	LOG_INFO("Event %s has been poped up to the application", ctx->next_event->label);

	ctx->next_event = ctx->next_event->next;

	return 0;
}

static int _dealloc(void* __restrict ctxbuf, void* __restrict pipe, int error, int purge)
{
	(void)error;
	(void)pipe;

	_module_context_t* ctx = (_module_context_t*)ctxbuf;

	if(purge)
	{
		uint32_t value;
		do{
			value = ctx->remaining;
		} while(!__sync_bool_compare_and_swap(&ctx->remaining, value, value - 1));
		if(value == 1 && ctx->terminate)
		{
			LOG_NOTICE("Terminating the entire platform");
			kill(0, SIGINT);
		}
	}

	LOG_DEBUG("Event simulation pipe is dead");

	return 0;
}

static size_t _read(void* __restrict ctxbuf, void* __restrict buffer, size_t nbytes, void* __restrict pipe)
{
	(void)ctxbuf;
	_handle_t* handle = (_handle_t*)pipe;
	if(handle->output) ERROR_RETURN_LOG(size_t, "Invalid pipe type: input side expected");

	size_t bytes_to_read = nbytes;
	if(bytes_to_read > handle->event->size - handle->offset)
	    bytes_to_read = handle->event->size - handle->offset;

	memcpy(buffer, handle->event->data + handle->offset, bytes_to_read);

	handle->offset += bytes_to_read;

	return bytes_to_read;
}

static size_t _write(void* __restrict ctx, const void* __restrict buffer, size_t nbytes, void* __restrict pipe)
{
	(void)ctx;
	_handle_t* handle = (_handle_t*)pipe;
	if(handle->output == 0) ERROR_RETURN_LOG(size_t, "Invalid pipe type: output side expected");

	if(handle->event->outbuf == NULL)
	{
		if(NULL == (handle->event->outbuf = (char*)malloc(handle->event->outcap = (nbytes < 128 ? nbytes * 2 : 256))))
		    ERROR_RETURN_LOG_ERRNO(size_t, "Cannot allocate the buffer for the output");
		handle->event->outsize = 0;
	}

	size_t bufsize = handle->event->outcap;
	for(;bufsize < handle->event->outsize + nbytes; bufsize *= 2);
	if(bufsize != handle->event->outcap)
	{
		char* new_buf = (char*)realloc(handle->event->outbuf, bufsize);
		if(NULL == new_buf) ERROR_RETURN_LOG_ERRNO(size_t, "Cannot allocate memory for the bytes to write");
		handle->event->outbuf = new_buf;
		handle->event->outcap = bufsize;
	}

	memcpy(handle->event->outbuf + handle->event->outsize, buffer, nbytes);
	handle->event->outsize += nbytes;

	return nbytes;
}

static int _fork(void* __restrict ctx, void* __restrict dest, void* __restrict src, const void* __restrict args)
{
	(void) ctx;
	(void) args;
	_handle_t* dh = (_handle_t*)dest;
	_handle_t* sh = (_handle_t*)src;

	dh->output = 0;
	dh->event = sh->event;
	dh->offset = 0;

	return 0;
}

static int _has_unread_data(void* __restrict ctx, void* __restrict data)
{
	(void)ctx;
	_handle_t *handle = (_handle_t*)data;

	if(handle->output) ERROR_RETURN_LOG(int, "Invalid pipe type: input pipe expected for this module call");

	return handle->event->size > handle->offset;
}

static itc_module_flags_t _get_flags(void* __restrict ctx)
{
	_module_context_t* context = (_module_context_t*)ctx;
	itc_module_flags_t flags = ITC_MODULE_FLAGS_EVENT_LOOP;

	if(context->next_event == NULL) flags |= ITC_MODULE_FLAGS_EVENT_EXHUASTED;

	return flags;
}

static int  _cntl(void* __restrict context, void* __restrict pipe, uint32_t opcode, va_list va_args)
{
	(void)context;
	_handle_t* handle = (_handle_t*)pipe;

	switch(opcode)
	{
		case MODULE_SIMULATE_CNTL_OPCODE_GET_LABEL:
		{
			const char** target = va_arg(va_args, char const**);
			if(NULL == target) ERROR_RETURN_LOG(int, "Invalid arguments");
			*target = handle->event->label;
			return 0;
		}
		default:
		    ERROR_RETURN_LOG(int, "Invalid opcode");
	}
}

static int _set_prop(void* __restrict ctx, const char* sym, itc_module_property_value_t value)
{
	_module_context_t* context = (_module_context_t*)ctx;
	if(value.type == ITC_MODULE_PROPERTY_TYPE_INT)
	{
		if(strcmp(sym, "events_per_sec") == 0) context->events_per_sec = (uint32_t)value.num;
		else return 0;
	}
	return 0;
}

itc_module_t module_simulate_module_def = {
	.mod_prefix   = "pipe.simulate",
	.context_size = sizeof(_module_context_t),
	.handle_size  = sizeof(_handle_t),
	.module_init  = _init,
	.module_cleanup  = _cleanup,
	.get_path        = _get_path,
	.accept          = _accept,
	.deallocate      = _dealloc,
	.read            = _read,
	.write           = _write,
	.fork            = _fork,
	.has_unread_data = _has_unread_data,
	.get_flags       = _get_flags,
	.set_property    = _set_prop,
	.cntl            = _cntl
};
