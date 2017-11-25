include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/shared.cmake")
set(COMPILER_CFLAGS "${SHARED_CFLAGS} -Wstrict-overflow=2")
set(COMPILER_CXXFLAGS "${SHARED_CXXFLAGS} -Wstrict-overflow=2")
