/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <itc/module_types.h>
#include <module/tcp/pool.h>
#include <module/tcp/module.h>
#include <sys/wait.h>
itc_module_type_t mod_tcp;
struct {
	module_tcp_pool_configure_t pool_conf;            /*!< the TCP pool configuration */
	int                         pool_initialized;     /*!< indicates if the pool has been initialized */
	int                         sync_write_attempt;   /*!< If do a synchronized write attempt before initialize a async operation */
	uint32_t                    async_buf_size;       /*!< The size of the async write buffer */
	module_tcp_pool_t*          conn_pool;            /*!< The TCP connection pool object */
} *context;

uint16_t port;

static const char response[] = "HTTP/1.1 200 OK \r\n"
                               "Content-Type: text/html\r\n"
                               "Content-Length: 91\r\n\r\n"
                               "<html><head><title>Hello World</title></head><body>Hi there, this is Plumber!</body></html>";
static const char request[] =  "GET / HTTP/1.1\r\n"
                               "Host: 127.0.0.1\r\n"
                               "\r\n";
int do_request(void)
{

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	if(sock == -1)
	{
		perror("socket");
		return -1;
	}

	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		perror("connect");
		shutdown(sock, 2);
		return -1;
	}

	if(send(sock, request, sizeof(request) - 1, 0) < 0)
	{
		perror("send");
		shutdown(sock, 2);
		return -1;
	}

	static char buffer[4096];
	ssize_t ptr = 0;

	for(;(size_t)ptr < strlen(response);)
	{
		ssize_t rc = recv(sock, buffer + ptr, sizeof(buffer) - (size_t)ptr, 0);
		fprintf(stderr, "read %zd bytes from the socket\n", rc);
		if(rc < 0)
		{
			perror("recv");
			shutdown(sock, 2);
			return -1;
		}
		else if(rc == 0) break;
		ptr += rc;
	}

	fputs("connection closed", stderr);

	shutdown(sock, 2);

	if(strcmp(response, buffer) != 0)
		return -1;

	return 0;
}

int accept_test(void)
{
	srand((unsigned)time(NULL));

	itc_module_pipe_param_t param = {
		.input_flags = RUNTIME_API_PIPE_INPUT,
		.output_flags = RUNTIME_API_PIPE_OUTPUT | RUNTIME_API_PIPE_ASYNC,
		.args = NULL
	};

	context->pool_conf.port = (uint16_t)(rand() % (0xffff - 10000) + 10000);
	context->async_buf_size = 4;
	context->sync_write_attempt = 0;
	pid_t pid;
	itc_module_pipe_t *in = NULL, *out = NULL;
	int status;
	pid = fork();

	if(pid == 0)
	{
		sleep(1);
		port = context->pool_conf.port;
		plumber_finalize();
		int rc = do_request();
		exit(rc);
		return 0;
	}

	ASSERT_OK(itc_module_pipe_accept(mod_tcp, param, &in, &out), goto ERR);

	static char buffer[4096];

	ASSERT_RETOK(size_t, itc_module_pipe_read(buffer, sizeof(buffer), in), goto ERR);

	ASSERT_RETOK(size_t, itc_module_pipe_write(response, sizeof(response) - 1, out), goto ERR);

	ASSERT_OK(itc_module_pipe_deallocate(in), goto ERR);
	in = NULL;
	ASSERT_OK(itc_module_pipe_deallocate(out), goto ERR);
	out = NULL;

	sleep(2);
	ASSERT_OK(module_tcp_pool_poll_event((module_tcp_pool_t*)module_tcp_module_get_pool(itc_module_get_context(mod_tcp))), goto ERR);

	ASSERT_OK(waitpid(pid, &status, 0), goto ERR);

	ASSERT_STREQ(buffer, request, goto ERR);

	ASSERT(status == 0, goto ERR);

	return 0;
ERR:
	if(NULL != in) itc_module_pipe_deallocate(in);
	if(NULL != out) itc_module_pipe_deallocate(out);
	return -1;
}

int setup(void)
{
	mod_tcp = itc_modtab_get_module_type_from_path("pipe.tcp.port_8888");
	ASSERT(ERROR_CODE(itc_module_type_t) != mod_tcp, CLEANUP_NOP);

	context = itc_module_get_context(mod_tcp);
	expected_memory_leakage();
	return 0;
}
DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(accept_test)
TEST_LIST_END;
