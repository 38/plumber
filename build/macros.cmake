find_program(VALGRIND_PROGRAM NAMES valgrind PATH ${VALGRIND_PREFIX})

macro(tablify str wide output)
	string(LENGTH ${str} string_length)
	set(${output} ${str})
	while(${wide} GREATER ${string_length})
		set(${output} "${${output}} ")
		string(LENGTH ${${output}} string_length)
	endwhile(${wide} GREATER ${string_length})
	string(SUBSTRING ${${output}} 0 ${wide} ${output})
endmacro(tablify str wide output)

macro(append_pakage_configure package_name package_type build_flag install_flag)
	tablify(${package_name} 40 tab_package)
	tablify(${package_type} 16 tab_type)
	tablify(${build_flag} 12 tab_build)
	tablify(${install_flag} 12 tab_intall)
	set(CONF "${CONF}\n${tab_package} ${tab_type} ${tab_build} ${tab_intall}")
endmacro(append_pakage_configure package_name package_type build_flag install_flag)

add_custom_target(ptype-syntax-check ALL DEPENDS protoman)

macro(compile_protocol_type_files target_prefix indir)
	if(IS_DIRECTORY "${indir}")
		file(GLOB prototypes RELATIVE "${indir}" "${indir}/*.ptype")
		foreach(prototype ${prototypes})
		 	list(APPEND PROTO_FILES_TO_INSTALL "${indir}/${prototype}")
			add_custom_command(TARGET ptype-syntax-check POST_BUILD
				               COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bin/protoman 
				                       --syntax-check ${indir}/${prototype}
							   DEPENDS protoman)
		endforeach(prototype ${prototypes})
	endif(IS_DIRECTORY "${indir}")
endmacro(compile_protocol_type_files indir outdir)

macro(get_default_compiler_flags target compiler type)
	execute_process(COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_SOURCE_DIR}/misc/compiler_version.sh ${compiler}
		            RESULT_VARIABLE result
		            OUTPUT_VARIABLE compiler_version)
	if(NOT "${result}" STREQUAL "0")
		message(FATAL_ERROR "Cannot detect the compiler version: ${result}")
	endif(NOT "${result}" STREQUAL "0")
	file(GLOB configurations RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/build/compiler "${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/*.cmake")
	string(REGEX REPLACE "_.*" ""  compiler_type ${compiler_version})
	string(REGEX REPLACE "${compiler_type}_" ""  compiler_version_num ${compiler_version})
	string(REGEX REPLACE "_" "."  compiler_version_num ${compiler_version_num})
	set(best_match "0.0.0")
	set(best_conf_file "default.cmake")
	foreach(conf_file ${configurations})
		get_filename_component(conf ${conf_file} NAME_WE)
		string(REGEX REPLACE "_.*" ""  conf_type ${conf})
		string(REGEX REPLACE "${conf_type}_" ""  conf_version ${conf})
		string(REGEX REPLACE "_" "."  conf_version ${conf_version})
		if("${conf_type}" STREQUAL "${compiler_type}" AND NOT "${conf_version}" VERSION_GREATER "${compiler_version_num}")
			if(NOT "${best_match}" VERSION_GREATER "${conf_version}")
				set(best_match ${conf_version})
				set(best_conf_file ${conf_file})
			endif(NOT "${best_match}" VERSION_GREATER "${conf_version}")
		endif("${conf_type}" STREQUAL "${compiler_type}" AND NOT "${conf_version}" VERSION_GREATER "${compiler_version_num}")
	endforeach(conf_file ${configurations})
	message("Found best compiler configuration file for ${type}: ${best_conf_file}")
	include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/${best_conf_file}")
	set(${target} "${${target}} ${COMPILER_${type}FLAGS}")
endmacro(get_default_compiler_flags target compiler type)

macro(add_binary_test test_name test_command)
	set(time_out_factor 1)
	if("${MEMCHECK_TEST}" STREQUAL "yes" AND NOT "${VALGRIND_PROGRAM}" STREQUAL "VALGRIND_PROGRAM-NOTFOUND")
		add_test(${test_name} 
				 ${VALGRIND_PROGRAM} 
				 --leak-check=full 
				 --errors-for-leak-kinds=definite,possible,indirect 
				 --error-exitcode=1
				 ${test_command})
		set(time_out_factor 10)
 	else("${MEMCHECK_TEST}" STREQUAL "yes" AND NOT "${VALGRIND_PROGRAM}" STREQUAL "VALGRIND_PROGRAM-NOTFOUND")
		add_test(${test_name} ${test_command})
	endif("${MEMCHECK_TEST}" STREQUAL "yes" AND NOT "${VALGRIND_PROGRAM}" STREQUAL "VALGRIND_PROGRAM-NOTFOUND")
	if(NOT "${ARGN}" STREQUAL "")
		set(actual_test_timeout ${ARGN})
		math(EXPR actual_test_timeout "${actual_test_timeout}*${time_out_factor}")
		set_tests_properties(${test_name} PROPERTIES TIMEOUT ${actual_test_timeout})
	endif(NOT "${ARGN}" STREQUAL "")
endmacro(add_binary_test test_name test_command)

