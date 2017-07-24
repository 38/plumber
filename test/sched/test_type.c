/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <stdio.h>

static inline runtime_stab_entry_t _load(const char* type)
{
	static char _args[32][128];
	static char const* args[32] = {_args[0]};
	uint32_t nargs;
	snprintf(_args[0], sizeof(_args[0]), "%s", "serv_typed");
	for(nargs = 1; *type != 0; nargs ++)
	{
		args[nargs] = _args[nargs];
		size_t len = 0;
		for(;*type != 0 && *type != ' '; type ++)
		{
			_args[nargs][len] = *type;
			if(_args[nargs][len] == '_')
			    _args[nargs][len] = ' ';
			len ++;
		}
		_args[nargs][len] = 0;

		while(*type == ' ')
		    type ++;
	}

	return runtime_stab_load(nargs, args);
}
#define MKBUF \
        sched_service_buffer_t* sbuf = sched_service_buffer_new();\
        ASSERT_PTR(sbuf, CLEANUP_NOP);\
        sched_service_t* serv = NULL;

#define MKSVC do {\
	    ASSERT_PTR(serv = sched_service_from_buffer(sbuf), CLEANUP_NOP);\
	    ASSERT_OK(sched_service_buffer_free(sbuf), CLEANUP_NOP);\
	    sbuf = NULL;\
    } while(0)

#define MKNODE(name, arg) \
    sched_service_node_id_t name = ERROR_CODE(sched_service_node_id_t); \
    runtime_stab_entry_t name##_sid = _load(arg);\
    {\
	    ASSERT_RETOK(runtime_stab_entry_t, name##_sid, CLEANUP_NOP);\
	    ASSERT_RETOK(sched_service_node_id_t, (name = sched_service_buffer_add_node(sbuf, name##_sid)), CLEANUP_NOP);\
    }

#define CONNECT(from_name, from_pipe, to_name, to_pipe) \
    do {\
	    runtime_api_pipe_id_t from_pid = runtime_stab_get_pipe(from_name##_sid, #from_pipe);\
	    ASSERT_RETOK(runtime_api_pipe_id_t, from_pid, CLEANUP_NOP);\
	    runtime_api_pipe_id_t to_pid = runtime_stab_get_pipe(to_name##_sid, #to_pipe);\
	    ASSERT_RETOK(runtime_api_pipe_id_t, to_pid, CLEANUP_NOP);\
	    sched_service_pipe_descriptor_t desc = {\
		    .source_node_id = from_name,\
		    .source_pipe_desc = from_pid,\
		    .destination_node_id = to_name,\
		    .destination_pipe_desc = to_pid\
	    };\
	    ASSERT_OK(sched_service_buffer_add_pipe(sbuf, desc), CLEANUP_NOP);\
    }while(0);

#define SETIO(type, name, pipe) do {\
	    runtime_api_pipe_id_t pid = runtime_stab_get_pipe(name##_sid, #pipe);\
	    ASSERT_RETOK(runtime_api_pipe_id_t, pid, CLEANUP_NOP);\
	    ASSERT_OK(sched_service_buffer_set_##type(sbuf, name, pid), CLEANUP_NOP);\
    }while(0)

#define CHKTYPE(name, pipe, type) do {\
	    runtime_api_pipe_id_t pid = runtime_stab_get_pipe(name##_sid, #pipe);\
	    const char* typestr;\
	    ASSERT_OK(sched_service_get_pipe_type(serv, name, pid, &typestr), CLEANUP_NOP);\
	    ASSERT_STREQ(typestr, type, CLEANUP_NOP);\
    }while(0)

#define FREESVC do {\
	    ASSERT_OK(sched_service_free(serv), CLEANUP_NOP);\
    }while(0)

int untyped()
{
	MKBUF;

	MKNODE(input, "in -> out1 out2");
	MKNODE(merge, "in1 in2 -> out");
	MKNODE(output, "in -> out");

	CONNECT(input, out1, merge, in1);
	CONNECT(input, out2, merge, in2);
	CONNECT(merge, out, output, in);

	SETIO(input, input, in);
	SETIO(output, output, out);

	MKSVC;

	CHKTYPE(input, out1, UNTYPED_PIPE_HEADER);
	CHKTYPE(input, out2, UNTYPED_PIPE_HEADER);
	CHKTYPE(merge, in1, UNTYPED_PIPE_HEADER);
	CHKTYPE(merge, in2, UNTYPED_PIPE_HEADER);
	CHKTYPE(merge, out, UNTYPED_PIPE_HEADER);
	CHKTYPE(output, in, UNTYPED_PIPE_HEADER);

	FREESVC;
	return 0;
}

int typed()
{
	MKBUF;

	MKNODE(input, "in -> out0:test/sched/typing/Triangle out1:test/sched/typing/ColoredTriangle out2");
	MKNODE(comp1, "raw:$T -> result:test/sched/typing/GZipCompressed_$T");
	MKNODE(comp2, "raw:$T -> result:test/sched/typing/ZlibCompressed_$T");
	MKNODE(select, "cond val0:$A val1:$B -> out:test/sched/typing/RSAEncrypted_$A|test/sched/typing/RSAEncrypted_$B");
	MKNODE(decry,  "enc:test/sched/typing/Encrypted_$T -> raw:$T");
	MKNODE(decomp, "cmp:test/sched/typing/Compressed_$T -> raw:$T");
	MKNODE(output, "input:test/sched/typing/Triangle -> output");

	CONNECT(input, out0, comp1, raw);
	CONNECT(input, out1, comp2, raw);
	CONNECT(input, out2, select, cond);
	CONNECT(comp1, result, select, val0);
	CONNECT(comp2, result, select, val1);
	CONNECT(select, out, decry, enc);
	CONNECT(decry, raw, decomp, cmp);
	CONNECT(decomp, raw, output, input);

	SETIO(input, input, in);
	SETIO(output, output, output);

	MKSVC;

	CHKTYPE(input, out0,   "test/sched/typing/Triangle");
	CHKTYPE(input, out1,   "test/sched/typing/ColoredTriangle");
	CHKTYPE(input, out2,   UNTYPED_PIPE_HEADER);
	CHKTYPE(comp1, raw,    "test/sched/typing/Triangle");
	CHKTYPE(comp1, result, "test/sched/typing/GZipCompressed test/sched/typing/Triangle");
	CHKTYPE(comp2, raw,    "test/sched/typing/ColoredTriangle");
	CHKTYPE(comp2, result,    "test/sched/typing/ZlibCompressed test/sched/typing/ColoredTriangle");
	CHKTYPE(select, cond,  UNTYPED_PIPE_HEADER);
	CHKTYPE(select, val0,  "test/sched/typing/GZipCompressed test/sched/typing/Triangle");
	CHKTYPE(select, val1,  "test/sched/typing/ZlibCompressed test/sched/typing/ColoredTriangle");
	CHKTYPE(select, out,   "test/sched/typing/RSAEncrypted test/sched/typing/Compressed test/sched/typing/Triangle");
	CHKTYPE(decry,  enc,   "test/sched/typing/Encrypted test/sched/typing/Compressed test/sched/typing/Triangle");
	CHKTYPE(decry,  raw,   "test/sched/typing/Compressed test/sched/typing/Triangle");
	CHKTYPE(decomp, cmp,   "test/sched/typing/Compressed test/sched/typing/Triangle");
	CHKTYPE(decomp, raw,   "test/sched/typing/Triangle");
	CHKTYPE(output, input, "test/sched/typing/Triangle");

	FREESVC;

	return 0;
}

int adhoc_type()
{
	MKBUF;

	MKNODE(input, "in -> out:test/sched/typing/Triangle out2:test/sched/typing/ColoredTriangle");
	MKNODE(comp1, "raw:$T -> result:test/sched/typing/GZipCompressed_$T");
	MKNODE(extract, "input:$T input2:$A -> output:$T.osize output2:$A.color");
	MKNODE(output, "input:$T input2:$S -> output");

	CONNECT(input, out, comp1, raw);
	CONNECT(input, out2, extract, input2);
	CONNECT(comp1, result, extract, input);
	CONNECT(extract, output, output, input);
	CONNECT(extract, output2, output, input2);

	SETIO(input, input, in);
	SETIO(output, output, output);

	MKSVC;

	CHKTYPE(input, out,   "test/sched/typing/Triangle");
	CHKTYPE(input, out2,  "test/sched/typing/ColoredTriangle");
	CHKTYPE(comp1, raw,   "test/sched/typing/Triangle");
	CHKTYPE(comp1, result,"test/sched/typing/GZipCompressed test/sched/typing/Triangle");
	CHKTYPE(extract, input, "test/sched/typing/GZipCompressed test/sched/typing/Triangle");
	CHKTYPE(extract, input2, "test/sched/typing/ColoredTriangle");
	CHKTYPE(extract, output, "uint64");
	CHKTYPE(extract, output2, "test/sched/typing/ColorRGB");
	CHKTYPE(output, input, "uint64");
	CHKTYPE(output, input2, "test/sched/typing/ColorRGB");

	FREESVC;
	return 0;
}

int invalid_conversion()
{
	MKBUF;

	(void)serv;

	MKNODE(input, "in -> out0:test/sched/typing/Triangle out1:test/sched/typing/ColoredTriangle out2");
	MKNODE(comp1, "raw:$T -> result:test/sched/typing/GZipCompressed_$T");
	MKNODE(comp2, "raw:$T -> result:test/sched/typing/DESEncrypted_$T");
	MKNODE(select, "cond val0:$A val1:$B -> out:test/sched/typing/RSAEncrypted_$A|test/sched/typing/RSAEncrypted_$B");
	MKNODE(output, "input:$T -> output");

	CONNECT(input, out0, comp1, raw);
	CONNECT(input, out1, comp2, raw);
	CONNECT(input, out2, select, cond);
	CONNECT(comp1, result, select, val0);
	CONNECT(comp2, result, select, val1);
	CONNECT(select, out, output, input);

	SETIO(input, input, in);
	SETIO(output, output, output);

	ASSERT(NULL == sched_service_from_buffer(sbuf), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_free(sbuf), CLEANUP_NOP);

	return 0;
}

int invalid_generialization()
{
	MKBUF;

	(void)serv;

	MKNODE(input, "in -> out0:test/sched/typing/Triangle out1:test/sched/typing/ColoredTriangle out2");
	MKNODE(comp1, "raw:$T -> result:test/sched/typing/Compressed_$T");
	MKNODE(comp2, "raw:$T -> result:test/sched/typing/Compressed_$T");
	MKNODE(select, "cond val0:$A val1:$B -> out:$A|$B");
	MKNODE(output, "input:test/sched/typing/GZipCompressed_$T -> output");

	CONNECT(input, out0, comp1, raw);
	CONNECT(input, out1, comp2, raw);
	CONNECT(input, out2, select, cond);
	CONNECT(comp1, result, select, val0);
	CONNECT(comp2, result, select, val1);
	CONNECT(select, out, output, input);

	SETIO(input, input, in);
	SETIO(output, output, output);

	ASSERT(NULL == sched_service_from_buffer(sbuf), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_free(sbuf), CLEANUP_NOP);

	return 0;

}

int setup()
{
	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR), CLEANUP_NOP);
	expected_memory_leakage();
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(untyped),
    TEST_CASE(typed),
    TEST_CASE(invalid_conversion),
    TEST_CASE(adhoc_type),
    TEST_CASE(invalid_generialization)
TEST_LIST_END;
