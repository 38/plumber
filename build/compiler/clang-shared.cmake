if("${SYSNAME}" STREQUAL "Darwin")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -undefined dynamic_lookup")
endif("${SYSNAME}" STREQUAL "Darwin")


# We need to figure out which standard library clang is using by default
execute_process(COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_SOURCE_DIR}/misc/detect_standard_lib.sh ${CMAKE_CXX_COMPILER}
				RESULT_VARIABLE result
				OUTPUT_VARIABLE stdlib_version
				OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT "${result}" STREQUAL "0")
	message(FATAL_ERROR "Cannot detect the compiler version: ${result}")
endif(NOT "${result}" STREQUAL "0")

if(NOT "${stdlib_version}" STREQUAL "libc++")
	set(SHARED_CXXFLAGS "${SHARED_CXXFLAGS} -nostdinc++ -I/usr/include/c++/v1")
endif(NOT "${stdlib_version}" STREQUAL "libc++")

