include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/shared.cmake")
set(COMPILER_CFLAGS "${SHARED_CFLAGS} -Wstrict-overflow=5 -Wstringop-overflow=4")
set(COMPILER_CXXFLAGS "${SHARED_CXXFLAGS} -Wstrict-overflow=5 -Wstringop-overflow=4")
if("${OPTLEVEL}" GREATER "1")
	set(COMPILER_CFLAGS "${COMPILER_CFLAGS} -Wvla-larger-than=4096 -Walloca-larger-than=4096")
	set(COMPILER_CXXFLAGS "${COMPILER_CXXFLAGS} -Wvla-larger-than=4096 -Walloca-larger-than=4096")
endif("${OPTLEVEL}" GREATER "1")
