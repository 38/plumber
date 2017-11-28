#!/bin/bash
find $1 -name '*.c' -or -name '*.cpp' -or -name '*.h' -or -name '*.hpp' > cscope.files
find -name 'config.h' -or -name 'package_config.h' >> cscope.files
cscope -b -q
