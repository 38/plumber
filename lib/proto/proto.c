/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <proto.h>

int proto_init()
{
	return proto_db_init();
}

int proto_finalize()
{
	int rc = proto_db_finalize();

	proto_err_clear();

	return rc;
}

