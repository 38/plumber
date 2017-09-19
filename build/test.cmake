if(NOT "${build_testenv}" STREQUAL "no")
	enable_testing()

	add_custom_target(copy_test_data ALL)
	file(GLOB_RECURSE DataFile RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/*.in")
	foreach(input ${DataFile})
		get_filename_component(data_dir ${input} DIRECTORY)
		get_filename_component(data_name ${input} NAME_WE)
		file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${test_dir})
		add_custom_command(TARGET copy_test_data PRE_BUILD
			               COMMAND ${CMAKE_COMMAND} -E 
						   copy ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/${input} 
						        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${data_dir})
	endforeach(input ${DataFile})

	if(NOT "${build_pservlet}" STREQUAL "no")
		file(GLOB_RECURSE TestServlets RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/serv_*.c")
		foreach(servlet ${TestServlets})
			get_filename_component(serv_dir ${servlet} DIRECTORY)
			get_filename_component(serv_name ${servlet} NAME_WE)
			file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${serv_dir})
			set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/${servlet} PROPERTIES COMPILE_FLAGS ${CFLAGS})
			add_library(${serv_name} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/${servlet})
			target_link_libraries(${serv_name} pservlet)
			set_target_properties(${serv_name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${serv_dir})
		endforeach(servlet ${TestServlets})
	else(NOT "${build_pservlet}" STREQUAL "no")
		message("Warning: Test case may fail because the servlets used by test won't compile")
	endif(NOT "${build_pservlet}" STREQUAL "no")

	file(GLOB_RECURSE unit_tests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/test_*.c")
	foreach(test ${unit_tests})
		get_filename_component(test_dir ${test} DIRECTORY)
		get_filename_component(test_name ${test} NAME_WE)
		string(REGEX REPLACE "^test_" "" test_name "${test_name}")
		set(test_name "${test_dir}_${test_name}")
		file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${test_dir})
		set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/${test} PROPERTIES COMPILE_FLAGS ${CFLAGS})
		add_executable(${test_name} ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/${test})
		set_target_properties(${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${test_dir})
		target_compile_definitions(${test_name} PRIVATE TESTDIR=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${test_dir}\" TESTING_CODE=1)
		target_include_directories(${test_name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/${TOOL_DIR}/testenv/include")
		target_link_libraries(${test_name} testenv plumber dl ${GLOBAL_LIBS} ${EXEC_LIBS})
		add_test(${test_name} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${test_dir}/${test_name})
		set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
	endforeach(test)

	add_custom_target(copy_testing_script ALL)
	file(GLOB_RECURSE shell_tests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/script_test_*")
	foreach(test ${shell_tests})
		get_filename_component(test_dir ${test} DIRECTORY)
		get_filename_component(test_name ${test} NAME_WE)
		get_filename_component(test_name_full ${test} NAME) 
		file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${test_dir})
		add_custom_command(TARGET copy_testing_script PRE_BUILD
		                   COMMAND ${CMAKE_COMMAND} -E
						   copy ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/${test}
						        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${test_dir})
		add_test(${test_dir}_${test_name} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/${test_dir}/${test_name_full})
		set_tests_properties(${test_dir}_${test_name} PROPERTIES ENVIRONMENT "OBJDIR=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
		set_tests_properties(${test_dir}_${test_name} PROPERTIES TIMEOUT 300)
	endforeach(test ${shell_tests})

	add_custom_target(install_testing_ptypes ALL DEPENDS protoman)
	file(GLOB_RECURSE prototypes "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_DIR}/*.ptype")
	foreach(prototype ${prototypes})
		file(MAKE_DIRECTORY ${TESTING_PROTODB_ROOT})
		add_custom_command(TARGET install_testing_ptypes PRE_BUILD
						   COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bin/protoman
						           --db-prefix ${TESTING_PROTODB_ROOT}
								   --update
								   --yes
								   --quiet
								   ${prototype})
	endforeach(prototype ${prototypes})

else(NOT "${build_testenv}" STREQUAL "no")
	message("Test could not run without testenv enabled")
endif(NOT "${build_testenv}" STREQUAL "no")
