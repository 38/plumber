unset(PLUMBER_V8_CONFIG CACHE)
find_file(PLUMBER_V8_CONFIG plumber-v8-config 
		  PATHS ${PLUMBER_V8_PREFIX}/bin
		  NO_DEFAULT_PATH)

if(NOT "${build_language_javascript}" STREQUAL "yes")
	set(build_language_javascript "no")
endif(NOT "${build_language_javascript}" STREQUAL "yes")

execute_process(COMMAND ${PLUMBER_V8_CONFIG} --cmake-include-dir
				OUTPUT_VARIABLE PLUMBER_V8_INCLUDE
				OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${PLUMBER_V8_CONFIG} --cmake-cflags-without-I
				OUTPUT_VARIABLE PLUMBER_V8_CFLAGS
				OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${PLUMBER_V8_CONFIG} --cmake-libs
				OUTPUT_VARIABLE PLUMBER_V8_LIBS
				OUTPUT_STRIP_TRAILING_WHITESPACE)

set(LOCAL_LIBS pstd ${PLUMBER_V8_LIBS})
set(LOCAL_INCLUDE ${PLUMBER_V8_INCLUDE})
set(LOCAL_CFLAGS ${PLUMBER_V8_CFLAGS})
set(INSTALL yes)
install_includes("${SOURCE_PATH}/lib" "lib/plumber/javascript" "*.js")

