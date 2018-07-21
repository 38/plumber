#!/bin/bash
detect_stdlib() {
	$@ -x c++ -E - \
	| sed -n -e '/^[^#].*[^ \t]/s/^[ \t]*//gp' \
	| awk ' {
		if($0 == "VERSION_END")
			state = 0;
		if(state)
			print $0;
		if($0 == "VERISON_BEGIN")
			state = 1;
	}'
}

detect_stdlib $@ << END_OF_CODE
#include <string>
VERISON_BEGIN
#if defined(__GLIBCXX__)
	glibc 
#elif defined(_LIBCPP_VERSION)
	libc++ 
#else
	unknown
#endif
VERSION_END
END_OF_CODE

