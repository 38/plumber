/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

#include <utils/utils.h>
#include <plumber.h>
#include <utils/init.h>
#include <utils/log.h>
#include <utils/mempool/objpool.h>
#include <utils/mempool/page.h>
#include <error.h>

#include <version.h>


INIT_VEC(modules) = {
	INIT_MODULE(utils),
	INIT_MODULE(lang),
	INIT_MODULE(runtime),
	INIT_MODULE(itc),
	INIT_MODULE(sched)
};

int plumber_init()
{
	int rc = INIT_DO_INITIALIZATION(modules);

	if(rc != ERROR_CODE(int))
	{
		LOG_INFO("_____________________________________________");
		LOG_INFO("| ______ _                 _                |");
		LOG_INFO("| | ___ \\ |               | |               |");
		LOG_INFO("| | |_/ / |_   _ _ __ ___ | |__   ___ _ __  |");
		LOG_INFO("| |  __/| | | | | '_ ` _ \\| '_ \\ / _ \\ '__| |");
		LOG_INFO("| | |   | | |_| | | | | | | |_) |  __/ |    |");
		LOG_INFO("| \\_|   |_|\\__,_|_| |_| |_|_.__/ \\___|_|    |");
		LOG_INFO("|___________________________________________|");
		LOG_INFO("libplumber %s is initialized", PLUMBER_VERSION);
	}

	return rc;

}

int plumber_finalize()
{
	int rc = 0;

	if(ERROR_CODE(int) == itc_modtab_on_exit())
	    rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == runtime_stab_dispose_all_namespaces())
	    rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == INIT_DO_FINALIZATION(modules))
	    rc = ERROR_CODE(int);

	return rc;
}

const char* plumber_version()
{
	return PLUMBER_VERSION;
}
