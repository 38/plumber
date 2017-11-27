include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/shared.cmake")
set(COMPILER_CFLAGS "${SHARED_CFLAGS} -Wstrict-overflow=5 -Wstringop-overflow=4 -Wvla-larger-than=4096 -Walloca-larger-than=4096")
set(COMPILER_CXXFLAGS "${SHARED_CXXFLAGS} -Wstrict-overflow=5 -Wstringop-overflow=4 -Wvla-larger-than=4096 -Walloca-larger-than=4096")
