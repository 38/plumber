if("$ENV{SANITIZER}" STREQUAL "yes")
	set(COMPILER_CFLAGS "${COMPILER_CFLAGS} -fsanitize=undefined,address -DSANITIZER")
	set(COMPILER_CXXFLAGS "${COMPILER_CXXFLAGS} -fsanitize=undefined,address -DSANITIZER")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=undefined,address")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=undefined,address")
endif("$ENV{SANITIZER}" STREQUAL "yes")
