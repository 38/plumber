#Set up basic cmake variables
if(APPLE)
	set(SYSNAME "OS_X")
elseif(WIN32)
	set(SYSNAME WIN32)
else(APPLE)
	set(SYSNAME ${CMAKE_SYSTEM_NAME})
endif(APPLE)

message(${CMAKE_CURRENT_BINARY_DIR})


file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/lib)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/test)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/examples)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/lib)
set(TOOL_DIR "tools")
set(TEST_DIR "test")
set(EXAMPLE_DIR "examples")
set(SOURCE_DIR "src")
set(INCLUDE_DIR "include")
set(SERVLET_DIR "servlets")
set(LIB_DIR "lib")

# log level can be 0 to 6, 0 means no log, 6 means the most detialed log
if("$ENV{L}" STREQUAL "") 
	if("${LOG}" STREQUAL "")
		set(LOG     6) 
	endif("${LOG}" STREQUAL "")
else("$ENV{L}" STREQUAL "")
	set(LOG		$ENV{L})
endif("$ENV{L}" STREQUAL "")

if("$ENV{O}" STREQUAL "")
	if("${OPTLEVEL}" STREQUAL "")
		set(OPTLEVEL	0)
	endif("${OPTLEVEL}" STREQUAL "")
else("$ENV{O}" STREQUAL "")
	set(OPTLEVEL	$ENV{O})
endif("$ENV{O}" STREQUAL "")

if(NOT "$ENV{CC}" STREQUAL "")
	set(CMAKE_C_COMPILER "$ENV{CC}")
endif(NOT "$ENV{CC}" STREQUAL "")

if(NOT "$ENV{CXX}" STREQUAL "")
	set(CMAKE_CXX_COMPILER "$ENV{CXX}")
endif(NOT "$ENV{CXX}" STREQUAL "")


message("Compiler: ${CMAKE_C_COMPILER}")
message("Log Level: ${LOG}")
message("Optimization Level: ${OPTLEVEL}")
set(CFLAGS $ENV{CFLAGS}\ -O${OPTLEVEL}\ -Wconversion\ -Wextra\ -Wall\ -Werror\ -g)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}" 
	                "${CMAKE_CURRENT_BINARY_DIR}")

include("${CMAKE_CURRENT_SOURCE_DIR}/build/install_includes.cmake")

find_package(Threads)

if(NOT "$ENV{LIBS}" STREQUAL "")
	set(GLOBAL_LIBS "$ENV{LIBS} ${CMAKE_THREAD_LIBS_INIT}")
else(NOT "$ENV{LIBS}" STREQUAL "")
	set(GLOBAL_LIBS "${CMAKE_THREAD_LIBS_INIT}")
endif(NOT "$ENV{LIBS}" STREQUAL "")

if(NOT "$ENV{EXEC_LIBS}" STREQUAL "")
	set(EXEC_LIBS "$ENV{EXEC_LIBS}")
else(NOT "$ENV{LIBS}" STREQUAL "")
	set(EXEC_LIBS )
endif(NOT "$ENV{EXEC_LIBS}" STREQUAL "")

set(CMAKE_LINK_DEPENDS_NO_SHARED true)

set(TESTING_PROTODB_ROOT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/protodb.root)
