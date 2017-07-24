macro(constant var default)
	if("${${var}}" STREQUAL "")
		set(${var} "${default}")
	else("${${var}}" STREQUAL "")
		set(option_status "${option_status} -D${var}=${${var}}")
	endif("${${var}}" STREQUAL "")
endmacro(constant var default)

find_package(Git)

execute_process(COMMAND ${GIT_EXECUTABLE} "log" "-1" "--format=format:%h"
				WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
				OUTPUT_VARIABLE SRC_VERSION
				OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${GIT_EXECUTABLE} "diff" "--stat"
				WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
				OUTPUT_VARIABLE SRC_CHANGED
				OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT "${SRC_CHANGED}" STREQUAL "")
	set(SRC_VERSION "${SRC_VERSION}.dirty")
else(NOT "${SRC_CHANGED}" STREQUAL "")
	set(SRC_VERSION "${SRC_VERSION}")
endif(NOT "${SRC_CHANGED}" STREQUAL "")


#################################Constants##########################################
##General Config
constant(PLUMBER_VERSION "\"0.1.1.${SRC_VERSION}\"")

##Logging
constant(LOG_DEFAULT_CONFIG_FILE \"log.cfg\")
constant(CONFIG_PATH \".\")

##LibPlumber Configurations
constant(DO_NOT_COMPILE_ITC_MODULE_TEST 0)

##OpenSSL
constant(MODULE_TLS_ENABLED 1)

constant(RUNTIME_SERVLET_DEFINE_SYM __servdef__)
constant(RUNTIME_ADDRESS_TABLE_SYM __plumber_address_table)
constant(RUNTIME_SERVLET_TAB_INIT_SIZE 32)
constant(RUNTIME_SERVLET_NAME_LEN 128)
constant(RUNTIME_PIPE_NAME_LEN 128)
constant(RUNTIME_PDT_INIT_SIZE 8)
constant(RUNTIME_SERVLET_SEARCH_PATH_INIT_SIZE 4)
constant(RUNTIME_SERVLET_DEFAULT_SEARCH_PATH \"${CMAKE_INSTALL_PREFIX}/lib/plumber/servlet\")

constant(SCHED_SERVICE_BUFFER_NODE_LIST_INIT_SIZE 32)
constant(SCHED_SERVICE_BUFFER_OUT_GOING_LIST_INIT_SIZE 8)
constant(SCHED_TASK_TABLE_SLOT_SIZE 37813)
constant(SCHED_LOOP_EVENT_QUEUE_SIZE 4096)
constant(SCHED_CNODE_BOUNDARY_INIT_SIZE 8)
constant(SCHED_PROF_INIT_THREAD_CAPACITY 1)
constant(SCHED_RSCOPE_ENTRY_TABLE_INIT_SIZE  4096)
constant(SCHED_RSCOPE_ENTRY_TABLE_SIZE_LIMIT 65536)
constant(SCHED_TYPE_ENV_HASH_SIZE 97)
constant(SCHED_TYPE_MAX 65536)

constant(ITC_MODULE_EVENT_QUEUE_SIZE 128)
constant(ITC_EQUEUE_VEC_INIT_SIZE 4)
constant(ITC_MODTAB_MAX_PATH 4096)

constant(LANG_LEX_SEARCH_LIST_INIT_SIZE 4)
constant(LANG_BYTECODE_HASH_SIZE 10093)
constant(LANG_BYTECODE_LIST_INIT_SIZE 4096)
constant(LANG_BYTECODE_HASH_POOL_INIT_SIZE 4096)
constant(LANG_BYTECODE_LABEL_VECTOR_INIT_SIZE 32)
constant(LANG_COMPILER_NODE_HASH_SIZE 10093)
constant(LANG_COMPILER_NODE_HASH_POOL_INIT_SIZE 4096)
constant(LANG_LEX_FILE_BUF_INIT_SIZE 4096)
constant(LANG_VM_ENV_HASH_SIZE 1023)
constant(LANG_VM_ENV_POOL_INIT_SIZE 4096)
constant(LANG_VM_PARAM_INIT_SIZE 32)
constant(LANG_PROP_CALLBACK_VEC_INIT_SIZE 32)

constant(PSCRIPT_GLOBAL_MODULE_PATH "\"${CMAKE_INSTALL_PREFIX}/lib/plumber/pss\"")

##LibProto Configurations
constant(LIB_PROTO_REF_NAME_INIT_SIZE 32)
constant(LIB_PROTO_CACHE_HASH_SIZE 10007)
constant(LIB_PROTO_DEFAULT_DB_ROOT "/var/lib/plumber/protodb")
constant(LIB_PROTO_FILE_SUFFIX   "proto")
constant(LIB_PROTO_REVDEP_SUFFIX "rdeps")
constant(LIB_PROTO_CACHE_REVDEP_INIT_SIZE 8)

## LibPSS Configurations 
constant(LIB_PSS_BYTECODE_TABLE_INIT_SIZE 32)
constant(LIB_PSS_DICT_INIT_HASH_SIZE 7)
constant(LIB_PSS_DICT_SIZE_LEVEL 16)
constant(LIB_PSS_DICT_MAX_CHAIN_THRESHOLD 8)
constant(LIB_PSS_VM_STACK_LIMIT 2048)
constant(LIB_PSS_COMP_ENV_HASH_SIZE 209)
constant(LIB_PSS_COMP_ENV_SCOPE_MAX 1024)
constant(LIB_PSS_VM_ARG_MAX 256)
constant(LIB_PSS_COMP_MAX_SERVLET 63103)

## PScript Configurations
constant(PSCRIPT_CLI_PROMPT "PSS> ")
constant(PSCRIPT_CLI_MAX_BRACKET 256)
####################################################################################

string(TOUPPER ${SYSNAME} SYSMACRO)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config.h.in" 
               "${CMAKE_CURRENT_BINARY_DIR}/config.h")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/misc/Doxyfile.in"
               "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

