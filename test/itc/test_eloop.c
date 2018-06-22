/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <module/tcp/pool.h>
#include <sys/wait.h>
#include <signal.h>
module_tcp_pool_configure_t* context;
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

	if(recv(sock, buffer, sizeof(buffer), 0) < 0)
	{
		perror("recv");
		shutdown(sock, 2);
		return -1;
	}

	shutdown(sock, 2);

	if(strcmp(response, buffer) != 0)
	    return -1;

	return 0;
}
static inline void sighand(int signo)
{
	(void)signo;
	usleep(10000);
	int rc = do_request();
	exit(rc);
}
int eloop_test(void)
{
	context->port = 9000;
	pid_t pid;
	itc_module_pipe_t *in = NULL, *out = NULL;
	int status;

	pid = fork();

	LOG_DEBUG("%d", pid);

	if(pid == 0)
	{
		port = context->port;
#ifndef __DARWIN__
		/* TODO: figure out why OSX fails if we finalize libplumber */
		plumber_finalize();
#endif
		signal(SIGUSR2, sighand);
		pause();
		exit(1);
	}

	itc_equeue_event_mask_t mask = ITC_EQUEUE_EVENT_MASK_NONE;
	ITC_EQUEUE_EVENT_MASK_ADD(mask, ITC_EQUEUE_EVENT_TYPE_IO);

	itc_equeue_token_t token = itc_equeue_scheduler_token();

	ASSERT_RETOK(itc_equeue_token_t, token, CLEANUP_NOP);

	ASSERT_OK(itc_eloop_start(NULL), CLEANUP_NOP);

	usleep(10000);

	kill(pid, SIGUSR2);

	ASSERT_OK(itc_equeue_wait(token, NULL, NULL), CLEANUP_NOP);

	itc_equeue_event_t e;

	ASSERT_RETOK(uint32_t, itc_equeue_take(token, mask, &e, 1), CLEANUP_NOP);

	ASSERT_PTR(e.io.in, CLEANUP_NOP);
	ASSERT_PTR(e.io.out, CLEANUP_NOP);

	in = e.io.in;
	out = e.io.out;

	static char buffer[4096];

	ASSERT_RETOK(size_t, itc_module_pipe_read(buffer, sizeof(buffer), in), goto ERR);

	ASSERT_RETOK(size_t, itc_module_pipe_write(response, sizeof(response) - 1, out), goto ERR);


	ASSERT_STREQ(buffer, request, goto ERR);

	ASSERT_OK(waitpid(pid, &status, 0), goto ERR);

	ASSERT_OK(itc_module_pipe_deallocate(in), goto ERR);
	in = NULL;
	ASSERT_OK(itc_module_pipe_deallocate(out), goto ERR);
	out = NULL;

	ASSERT(status == 0, goto ERR);

	return 0;
ERR:
	if(NULL != in) itc_module_pipe_deallocate(in);
	if(NULL != out) itc_module_pipe_deallocate(out);
	return -1;
}

/**
 * @todo fix the memory leak when exit
 **/
int setup(void)
{
	context = (module_tcp_pool_configure_t*)itc_module_get_context(itc_modtab_get_module_type_from_path("pipe.tcp.port_8888"));
	expected_memory_leakage();
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(eloop_test)
TEST_LIST_END;
