include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/shared.cmake")
set(COMPILER_CFLAGS "${SHARED_CFLAGS} -Wno-missing-field-initializers -Wno-strict-aliasing")
set(COMPILER_CXXFLAGS "${SHARED_CXXFLAGS} -Wno-missing-field-initializers -Wno-strict-aliasing")
