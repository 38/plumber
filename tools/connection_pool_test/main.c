/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the connection pool testing program
 * @file connection_pool_test/main.c
 **/
#include <constants.h>
#ifdef __LINUX__
#include <inttypes.h>
#include <time.h>
#include <plumber.h>
#include <module/tcp/pool.h>
#include <utils/log.h>
#include <error.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
FILE* tl = NULL;
#define NTHREAD 32
module_tcp_pool_configure_t conf = {
	.port       = 8800,
	.bind_addr  = "0.0.0.0",
	.size       = 65536,
	.ttl        = 240,
	.event_size = 64,
	.min_timeout= 1
};
typedef struct {
	char data[1024];
	ssize_t size;
	ssize_t start;
} buffer_t;
pthread_t worker[NTHREAD];
pthread_mutex_t s_mutex;
pthread_cond_t  s_cond;
pthread_mutex_t r_mutex;
pthread_cond_t  r_cond;
pthread_mutex_t t_mutex[NTHREAD];
pthread_cond_t  t_cond[NTHREAD];
int       t_fd[NTHREAD];
buffer_t* t_bf[NTHREAD];
int       t_st[NTHREAD];
uint32_t    t_id[NTHREAD];
int       t_tid[NTHREAD];
module_tcp_pool_conninfo_t conn;
int       r_flag = 0;
module_tcp_pool_t* pool;

double get_ts(void)
{
	struct timespec time;
	clock_gettime(CLOCK_REALTIME, &time);

	double ts = (double)time.tv_sec + (double)time.tv_nsec / 1e+9;

	return ts;
}
void* worker_func(void* ptr_id)
{
	int tid = *(int*)ptr_id;
	int efd = epoll_create1(0);

	while(1)
	{
		pthread_mutex_lock(t_mutex + tid);

		while(t_st[tid] != 1)
		{
			fprintf(tl, "%16.6lf	W%d	0\n", get_ts() ,tid);
			pthread_cond_wait(t_cond + tid, t_mutex + tid);
		}
		fprintf(tl, "%16.6lf	W%d	1\n", get_ts() ,tid);
		int fd = t_fd[tid];
		buffer_t* buffer = t_bf[tid];

		int state = 0;
		for(;;)
		{
			if(buffer == NULL || buffer->start >= buffer->size)
			{
				if(buffer == NULL) buffer = (buffer_t*)malloc(sizeof(buffer_t));
				buffer->size = read(fd, buffer->data, 1024);
				if(buffer->size == 0)
				{
					LOG_DEBUG("Connection fd %d is about to close because it has been closed by peer", fd);
					break;
				}
				if(buffer->size < 0)
				{
					if(errno == EAGAIN)
					{
						LOG_DEBUG("Nothing to read, hungup");
						struct epoll_event event = {
							.events = EPOLLET | EPOLLIN
						};
						epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);
						epoll_wait(efd, &event, 1, -1);
						continue;
					}
					else
					{
						LOG_DEBUG("Asking connection pool close the connection fd %d because of unexpected error code: %d(%s)", fd, errno, strerror(errno));
						break;
					}
				}
				else buffer->start = 0;
			}

			for(; buffer->start < buffer->size && state != 4; buffer->start ++)
			{
				char ch = buffer->data[buffer->start];
				switch(state)
				{
					case 0:
					    if(ch == '\r') state = 1;
					    break;
					case 1:
					    if(ch == '\n') state = 2;
					    else if(ch == '\r') state = 1;
					    else state = 0;
					    break;
					case 2:
					    if(ch == '\r') state = 3;
					    else state = 0;
					    break;
					case 3:
					    if(ch == '\n') state = 4;
					    else if(ch == '\r') state = 1;
					    else state = 0;
					case 4:
					    break;
				}
			}
			if(state == 4)
			{
				if(buffer->start == buffer->size) free(buffer), buffer = NULL;
				break;
			}
		}
		int st;
		if(buffer == NULL || buffer->size > 0)
		{
			usleep(2000);
			const char * res =
			    "HTTP/1.1 200 OK \r\n"
			    "Content-Type: text/html\r\n"
			    "Connection: keep-alive\r\n"
			    "Content-Length: 82\r\n\r\n"
			    "<html><head><title>Hello World</title></head>"
			    "<body>Hi there, this is Plumber!<br/>";
			const char* ptr = res;
			for(;*ptr;)
			{
				ssize_t rc = write(fd, res, strlen(res));
				if(rc == -1)
				{
					if(errno == EAGAIN) continue;
					break;
				}
				ptr += rc;
			}
			st = 2;
			if(NULL == buffer)
			{
				LOG_DEBUG("Worker #%d is asking connection pool to deactivate the connection object %u, because request is done", tid, t_id[tid]);
			}
			else
			{
				LOG_DEBUG("Worker #%d is asking connection pool to close the connection object %u, because next rquest is coming", tid, t_id[tid]);
			}
		}
		else
		{
			free(buffer);
			LOG_DEBUG("Worker #%d is asking connection pool to close the connection object %u, because it's closed", tid, t_id[tid]);
			st = 3;
		}
		t_bf[tid] = buffer;

		pthread_mutex_lock(&s_mutex);
		t_st[tid] = st;
		pthread_mutex_unlock(&s_mutex);
		pthread_cond_signal(&s_cond);

		pthread_mutex_unlock(t_mutex + tid);
	}

	close(efd);

	return NULL;
}

void* event_loop(void* param)
{
	(void)param;
	for(;;)
	{
		pthread_mutex_lock(&r_mutex);
		while(r_flag == 1)
		{
			fprintf(tl, "%16.6lf	E	0\n", get_ts());
			pthread_cond_wait(&r_cond, &r_mutex);
		}
		fprintf(tl, "%16.6lf	E	1\n", get_ts());

		if(module_tcp_pool_connection_get(pool, &conn) < 0)
		{
			LOG_ERROR("cannot get requeest");
		}
		pthread_mutex_lock(&s_mutex);
		r_flag = 1;
		pthread_mutex_unlock(&s_mutex);
		pthread_mutex_unlock(&r_mutex);
		pthread_cond_signal(&s_cond);
	}
}
int main(void)
{
	tl = fopen("timeline.tsv", "w");
	signal(SIGPIPE, SIG_IGN);
	if(log_init() == ERROR_CODE(int) ||
	   (pool = module_tcp_pool_new()) == NULL ||
	   module_tcp_pool_configure(pool, &conf) == ERROR_CODE(int))
	{
		LOG_ERROR("cannot intialize");
		return -1;
	}


	//char buffer[1024];

	int i;
	for(i = 0; i < NTHREAD; i ++)
	{
		pthread_mutex_init(t_mutex + i, NULL);
		pthread_cond_init(t_cond + i, NULL);
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		t_tid[i] = i;
		pthread_create(worker + i, &attr, worker_func, t_tid + i);
	}

	pthread_mutex_init(&s_mutex, NULL);
	pthread_cond_init(&s_cond, NULL);

	pthread_mutex_init(&r_mutex, NULL);
	pthread_cond_init(&r_cond, NULL);

	pthread_t request;
	pthread_create(&request, NULL, event_loop, NULL);

	while(1)
	{
		pthread_mutex_lock(&s_mutex);
		int f;
		for(;;)
		{
			f = -1;
			for(i = 0; i < NTHREAD && f == -1; i ++)
			    if(t_st[i] != 1) f = i;
			if(f != -1 && t_st[f] == 0 && r_flag == 1) break;
			else if(f != -1 && t_st[f] >= 2) break;
			fprintf(tl, "%16.6lf	S	0\n", get_ts());
			pthread_cond_wait(&s_cond, &s_mutex);
		}
		pthread_mutex_unlock(&s_mutex);
		fprintf(tl, "%16.6lf	S	1\n", get_ts());

		pthread_mutex_lock(t_mutex + f);
		switch(t_st[f])
		{
			case 2:
			    module_tcp_pool_connection_release(pool, t_id[f], t_bf[f], MODULE_TCP_POOL_RELEASE_MODE_AUTO);
			    t_st[f] = 0;
			    break;
			case 3:
			    module_tcp_pool_connection_release(pool, t_id[f], NULL, MODULE_TCP_POOL_RELEASE_MODE_PURGE);
			    t_st[f] = 0;
			    break;
			case 0:
			    LOG_DEBUG("Assign connection object %"PRIu32" to thread %d", conn.idx , f);
			    t_st[f] = 1;
			    t_id[f] = conn.idx;
			    t_fd[f] = conn.fd;
			    t_bf[f] = (buffer_t*)conn.data;
			    pthread_mutex_lock(&r_mutex);
			    r_flag = 0;
			    pthread_mutex_unlock(&r_mutex);
		}
		pthread_mutex_unlock(t_mutex + f);
		pthread_cond_signal(t_cond + f);
		pthread_cond_signal(&r_cond);
	}


	module_tcp_pool_free(pool);
	log_finalize();
	return 0;
}
#else
int main()
{
	return 0;
}
#endif
