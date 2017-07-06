find_package(Java COMPONENTS Development Runtime REQUIRED)
get_filename_component(java_real_path ${Java_JAVAH_EXECUTABLE} REALPATH)
get_filename_component(java_bin_path ${java_real_path} DIRECTORY)
get_filename_component(JAVA_HOME ${java_bin_path}/../ ABSOLUTE)
find_package(JNI)
set(JNI_INCLUDE_FLAGS "")
foreach(incdir ${JNI_INCLUDE_DIRS})
	set(JNI_INCLUDE_FLAGS "${JNI_INCLUDE_FLAGS} -I${incdir}")
endforeach(incdir ${JNI_INCLUDE_DIRS})

include(UseJava)
set(CMAKE_JAVA_TARGET_OUTPUT_DIR ${GEN_PATH})
file(GLOB_RECURSE java_source "${SOURCE_PATH}/javalib/*.java")
add_jar(java_pservlet ${java_source}
	    OUTPUT_NAME pservlet)
create_javah(TARGET java_pservlet_jni_header
	         GENERATED_FILES jni_headers
			 CLASSES info.haohou.pservlet._Pservlet
			 CLASSPATH ${GEN_PATH}/pservlet.jar
			 DEPENDS java_pservlet
			 OUTPUT_DIR ${GEN_PATH})
file(GLOB_RECURSE jni_source "${SOURCE_PATH}/jni/*.c")
set_source_files_properties(${jni_source} PROPERTIES 
	COMPILE_FLAGS "${CFLAGS} ${JNI_INCLUDE_FLAGS} -I${GEN_PATH}")
add_library(java_pservlet_jni SHARED ${jni_source})
target_link_libraries(java_pservlet_jni pservlet ${JNI_LIBERARIES})
set(LOCAL_CFLAGS "-DSERVLET_JNI_PATH=\"${CMAKE_INSTALL_PREFIX}/lib/plumber/java\"")

set(INSTALL yes)
install_includes("${GEN_PATH}" "lib/plumber/java" "*.jar")
install(TARGETS java_pservlet_jni DESTINATION lib/plumber/java)
