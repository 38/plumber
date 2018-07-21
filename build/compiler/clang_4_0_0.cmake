include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/shared.cmake")
set(COMPILER_CFLAGS "${SHARED_CFLAGS} -Wstrict-overflow=5 -Wno-address-of-packed-member")
set(COMPILER_CXXFLAGS "${SHARED_CXXFLAGS} -Wstrict-overflow=5")

if("${SYSNAME}" STREQUAL "Darwin")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -undefined dynamic_lookup")
endif("${SYSNAME}" STREQUAL "Darwin")


# We need to figure out which standard library clang is using by default
execute_process(COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_SOURCE_DIR}/misc/detect_standard_lib.sh ${CMAKE_CXX_COMPILER}
				RESULT_VARIABLE result
				OUTPUT_VARIABLE stdlib_version)

if(NOT "${result}" STREQUAL "0")
	message(FATAL_ERROR "Cannot detect the compiler version: ${result}")
endif(NOT "${result}" STREQUAL "0")

if(NOT "${stdlib_version}" STREQUAL "libc++")
    set(COMPILER_CXXFLAGS "-nostdinc++ -I/usr/include/c++/v1")
endif(NOT "${stdlib_version}" STREQUAL "libc++")

if("${LIB_PSS_VM_STACK_LIMIT}" STREQUAL "")
	set(LIB_PSS_VM_STACK_LIMIT 512)
endif("${LIB_PSS_VM_STACK_LIMIT}" STREQUAL "")
if("${STACK_SIZE}" STREQUAL "")
	set(STACK_SIZE "0x800000")
endif("${STACK_SIZE}" STREQUAL "")
include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/clang-sanitizer.cmake")
