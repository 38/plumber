/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#include <constants.h>
#include <error.h>

#include <pss.h>
#include <module.h>

static pss_value_t _pscript_builtin_print(uint32_t argc, pss_value_t* argv)
{
	pss_value_t ret = {};
	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		char buf[4096];
		if(ERROR_CODE(size_t) == pss_value_strify_to_buf(argv[i], buf, sizeof(buf)))
		{
			ret.kind = PSS_VALUE_KIND_ERROR;
			break;
		}

		printf("%s", buf);
	}

	putchar('\n');

	return ret;
}

int builtin_init(pss_vm_t* vm)
{
	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "print", _pscript_builtin_print))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function print");

	return 0;
}
