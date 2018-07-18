#Build the main library
set(CONF "System: ${SYSNAME}\nCC=${CMAKE_C_COMPILER}\nCFLAGS=${CFLAGS}\nCXXFLAGS=${CXXFLAGS}")
set(CONF "${CONF}\n--------------------------------------------------------------------------------")
append_pakage_configure("package" "type" "build" "install")
set(CONF "${CONF}\n--------------------------------------------------------------------------------")

set(src_files )
file(GLOB_RECURSE src_files "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_DIR}/*.c")

if("${MODULE_TLS_ENABLED}" EQUAL "1")
	set(OPENSSL_INCLUDE_DIR "-I${OPENSSL_INCLUDE_DIR}")
else("${MODULE_TLS_ENABLED}" EQUAL "1")
	message("Notice: TLS support is disabled")
	set(OPENSSL_INCLUDE_DIR "")
	set(OPENSSL_LIBRARIES "")
endif("${MODULE_TLS_ENABLED}" EQUAL "1")

set_source_files_properties(${src_files} PROPERTIES COMPILE_FLAGS "${CFLAGS} ${OPENSSL_INCLUDE_DIR}")


if("${SHARED_LIBPLUMBER}" STREQUAL "yes")
	set(plumber_type "shared-library")
	add_library(plumber SHARED ${src_files})
	install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_INSTALL_PREFIX}/lib/libplumber.so ${CMAKE_INSTALL_PREFIX}/lib/libplumber_shared.so)")
	append_pakage_configure("plumber-shared" "symlink" "yes" "yes")
else("${SHARED_LIBPLUMBER}" STREQUAL "yes")
	set(plumber_type "static-library")
	add_library(plumber ${src_files})
	add_library(plumber_shared SHARED ${src_files})
	target_link_libraries(plumber_shared ${OPENSSL_LIBRARIES} ${GLOBAL_LIBS} proto)
	append_pakage_configure("plumber-shared" "shared-library" "yes" "yes")
	install(TARGETS plumber_shared DESTINATION lib)
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
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/version.h DESTINATION "${prefix}")
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

append_pakage_configure("plumber-core" "${plumber_type}" "yes" "yes")
