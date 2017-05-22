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
	tablify(${package_name} 20 tab_package)
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

