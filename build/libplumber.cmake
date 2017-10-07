#Build the main library
set(src_files )
file(GLOB_RECURSE src_files "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_DIR}/*.c")

if("${MODULE_TLS_ENABLED}" EQUAL "1")
	find_package(OpenSSL)
	set(OPENSSL_INCLUDE_DIR "-I{OPENSSL_INCLUDE_DIR}")
else("${MODULE_TLS_ENABLED}" EQUAL "1")
	message("Notice: TLS support is disabled")
	set(OPENSSL_INCLUDE_DIR )
	set(OPENSSL_LIBRARIES )
endif("${MODULE_TLS_ENABLED}" EQUAL "1")

set_source_files_properties(${src_files} PROPERTIES COMPILE_FLAGS "${CFLAGS} ${OPENSSL_INCLUDE_DIR}")

if("${SHARED_LIBPLUMBER}" STREQUAL "yes")
	add_library(plumber SHARED ${src_files})
else("${SHARED_LIBPLUMBER}" STREQUAL "yes")
	add_library(plumber ${src_files})
endif("${SHARED_LIBPLUMBER}" STREQUAL "yes")

target_link_libraries(plumber ${OPENSSL_LIBRARIES} ${GLOBAL_LIBS} proto)

install(TARGETS plumber DESTINATION lib)
compile_protocol_type_files(${CMAKE_INSTALL_PREFIX} ${CMAKE_CURRENT_SOURCE_DIR}/prototype)

macro(install_plumber_headers prefix)
	file(GLOB_RECURSE api_headers RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}" 
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/*api.h" 
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/utils/static_assertion.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/constants.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/utils/log_macro.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/os/*const.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/error.h")
	foreach(header ${api_headers})
		get_filename_component(target_dir ${header} DIRECTORY)
		install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/${header} DESTINATION "${prefix}/${target_dir}")
	endforeach(header ${api_headers})
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/config.h DESTINATION "${prefix}")
endmacro(install_plumber_headers prefix)

macro(install_plumber_logging_utils prefix)
	file(GLOB_RECURSE api_headers RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}" 
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/utils/static_assertion.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/constants.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/utils/log_macro.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/error.h")
	foreach(header ${api_headers})
		get_filename_component(target_dir ${header} DIRECTORY)
		install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/${header} DESTINATION "${prefix}/${target_dir}")
	endforeach(header ${api_headers})
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/config.h DESTINATION "${prefix}")
endmacro(install_plumber_logging_utils prefix)
