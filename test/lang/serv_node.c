/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <string.h>
#include <pservlet.h>

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	(void)ctxbuf;

	uint32_t i;
	pipe_flags_t pf = PIPE_INPUT;
	for(i = 1; i < argc; i ++)
	{
		if(0 == strcmp(argv[i], "->"))
		{
			pf = PIPE_OUTPUT;
			continue;
		}

		if(ERROR_CODE(pipe_t) == pipe_define(argv[i], pf, NULL))
			ERROR_RETURN_LOG(int, "Cannot define pipe %s", argv[i]);
	}

	return 0;
}

SERVLET_DEF = {
	.desc = "Dummy servlet which can produce abitary pipe configuration",
	.size = 0,
	.version = 0,
	.init = _init
};
