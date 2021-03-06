/**
 * Copyright (C) 2017-2018, Hao Hou
 **/
#include <constants.h>
#ifdef __DARWIN__
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/fcntl.h>

#include <error.h>

#include <os/os.h>
#include <utils/log.h>

/**
 * @brief The abstraction of the POSIX pipe
 **/
typedef struct {
	int in;                     /*!< The input side of the pipe */
	int out;                    /*!< The output side of the pipe */
} _posix_pipe_t;

/**
 * @brief  The actual poll structure
 **/
struct _os_event_poll_t {
	int            kqueue_fd;          /*!< The kqueue FD */
	struct kevent* kevent_el;          /*!< The KEvent event list */
	size_t         el_size;            /*!< The size of the event list */
	_posix_pipe_t* uenv_pipes;         /*!< The user event pipes*/
	size_t         nuenv;               /*!< How many user defined events */
};

os_event_poll_t* os_event_poll_new()
{
	os_event_poll_t* ret = (os_event_poll_t*)malloc(sizeof(*ret));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new poll object");

	ret->kevent_el = NULL;
	ret->el_size = 0;
	ret->uenv_pipes = NULL;
	ret->nuenv = 0;

	if((ret->kqueue_fd = kqueue()) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create KQueue FD for the poll object");

	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

int os_event_poll_free(os_event_poll_t* poll)
{
	int rc = 0;

	if(NULL == poll) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != poll->kevent_el) free(poll->kevent_el);

	if(NULL != poll->uenv_pipes)
	{
		size_t i;
		for(i = 0; i < poll->nuenv; i ++)
			if(close(poll->uenv_pipes[i].in) < 0)
			{
				LOG_ERROR_ERRNO("Cannot close the event pipe fd %d", poll->uenv_pipes[i].in);
				rc = ERROR_CODE(int);
			}
		free(poll->uenv_pipes);
	}

	if(close(poll->kqueue_fd) < 0)
	{
		LOG_ERROR_ERRNO("Cannot close the kqueue fd %d", poll->kqueue_fd);
		rc = ERROR_CODE(int);
	}

	free(poll);
	return rc;
}

static int _pipe2(int pipefd[2], int flags)
{
	if(pipe(pipefd) < 0)
	{
		pipefd[0] = -1;
		pipefd[1] = -1;
		ERROR_RETURN_LOG_ERRNO(int, "Cannot create pipe");
	}

	int origin_fl = fcntl(pipefd[0], F_GETFL);
	if(origin_fl < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the orignal file flag");
	if(fcntl(pipefd[0], F_SETFL, flags | origin_fl) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the pipe flag");

	origin_fl = fcntl(pipefd[1], F_GETFL);
	if(origin_fl < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the orignal file flag");
	if(fcntl(pipefd[1], F_SETFL, flags | origin_fl) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the pipe flag");

	return 0;
ERR:
	close(pipefd[0]);
	close(pipefd[1]);
	pipefd[0] = -1;
	pipefd[1] = -1;
	return ERROR_CODE(int);
}

int os_event_poll_modify(os_event_poll_t* poll, os_event_desc_t* desc)
{
	if(NULL != desc && desc->type != OS_EVENT_TYPE_KERNEL)
		ERROR_RETURN_LOG(int, "Only kernel event is allowed");

	/* For KQueue implementaiton, re-add the same event is sufficient */
	return os_event_poll_add(poll, desc);
}

int os_event_poll_add(os_event_poll_t* poll, os_event_desc_t* desc)
{
	if(NULL == poll) ERROR_RETURN_LOG(int, "Invalid arguments");

	int fd = -1;
	int flags = 0;
	int ret = ERROR_CODE(int);
	void* data = NULL;

	if(desc->type == OS_EVENT_TYPE_USER)
	{
		/* We actually use pipe simulates the eventfd */
		int pipe_fd[2];
		if(_pipe2(pipe_fd, O_CLOEXEC | O_NONBLOCK) == ERROR_CODE(int))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot create pipe for the user event");

		_posix_pipe_t* arr = NULL;

		if(NULL == poll->uenv_pipes)
			arr = (_posix_pipe_t*)malloc(sizeof(_posix_pipe_t));
		else
			arr = (_posix_pipe_t*)realloc(poll->uenv_pipes, sizeof(_posix_pipe_t) * (poll->nuenv + 1));

		if(NULL == arr)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the new user event");
		else
			poll->uenv_pipes = arr;

		poll->uenv_pipes[poll->nuenv].in = pipe_fd[0];
		poll->uenv_pipes[poll->nuenv].out = pipe_fd[1];
		poll->nuenv ++;
		poll->uenv_pipes = arr;

		fd = pipe_fd[0];
		flags = EVFILT_READ;
		ret = pipe_fd[1];
		data = desc->user.data;
	}
	else if(desc->type == OS_EVENT_TYPE_KERNEL)
	{
		fd = desc->kernel.fd;
		data = desc->kernel.data;

		if(desc->kernel.event == OS_EVENT_KERNEL_EVENT_IN ||
		   desc->kernel.event == OS_EVENT_KERNEL_EVENT_CONNECT)
			flags = EVFILT_READ;
		else if(desc->kernel.event == OS_EVENT_KERNEL_EVENT_OUT)
			flags = EVFILT_WRITE;
		else if(desc->kernel.event == OS_EVENT_KERNEL_EVENT_BIDIR)
			flags = EVFILT_WRITE | EVFILT_READ;

		ret = fd;
	}


	if(fd >= 0)
	{
		struct kevent event;

		EV_SET(&event, fd, flags, EV_ADD, 0, 0, data);
		if(kevent(poll->kqueue_fd, &event, 1, NULL, 0, NULL) < 0)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot add target FD to the event queue");
	}
	else LOG_ERROR("Invalid event type");

	return ret;
ERR:
	if(desc->type == OS_EVENT_TYPE_USER)
	{
		close(fd);
		close(ret);
	}
	return ERROR_CODE(int);
}

int os_event_poll_del(os_event_poll_t* poll, int fd, int read)
{
	if(NULL == poll || fd < 0) ERROR_RETURN_LOG(int, "Invalid arguments");

	int flags;

	switch(read)
	{
		case 0:
			flags = EVFILT_WRITE;
			break;
		case 1:
			flags = EVFILT_READ;
			break;
		case 2:
			flags = EVFILT_READ | EVFILT_WRITE;
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid read flags");
	}

	size_t i;
	for(i = 0; i < poll->nuenv && poll->uenv_pipes[i].out != fd ; i ++);

	if(i < poll->nuenv)
	{
		LOG_DEBUG("Removing event fd, set flags to READ");
		flags = EVFILT_READ;
	}

	struct kevent event;
	EV_SET(&event, fd, flags, EV_DELETE, 0, 0, NULL);

	if(kevent(poll->kqueue_fd, &event, 1, NULL, 0, NULL) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot remove target FD to the KQqueue");

	return 0;
}

int os_event_poll_wait(os_event_poll_t* poll, size_t max_events, int timeout)
{
	if(NULL == poll || max_events == 0)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(max_events > poll->el_size)
	{
		if(NULL != poll->kevent_el) free(poll->kevent_el);
		poll->el_size = 0;
		if(NULL == (poll->kevent_el = (struct kevent*)calloc(max_events, sizeof(struct kevent))))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate the event buffer");
		poll->el_size = max_events;
	}

	struct timespec ts = {
		.tv_sec  = timeout / 1000,
		.tv_nsec = (timeout % 1000) * 1000000
	};

	int rc;

	if((rc = kevent(poll->kqueue_fd, NULL, 0, poll->kevent_el, (int)poll->el_size, timeout >= 0 ? &ts: NULL)) < 0)
	{
		if(errno == EINTR)
			return 0;
		ERROR_RETURN_LOG_ERRNO(int, "Cannot complete kevent call");
	}

	return rc;
}

void* os_event_poll_take_result(os_event_poll_t* poll, size_t idx)
{
	if(NULL == poll || idx > poll->el_size)
		return NULL;

	return poll->kevent_el[idx].udata;
}

int os_event_user_event_consume(os_event_poll_t* poll, int fd)
{
	size_t i;
	for(i = 0; i < poll->nuenv && poll->uenv_pipes[i].out != fd ; i ++)
		continue;

	if(i == poll->nuenv) ERROR_RETURN_LOG(int, "Invalid arguments: FD %d is not a user event FD", fd);
	fd = poll->uenv_pipes[i].in;

	for(;;)
	{
		uint64_t buf;
		ssize_t rc = read(fd, &buf, sizeof(buf));
		if(rc < 0)
		{
			if(errno == EWOULDBLOCK || errno == EAGAIN)
				break;
			ERROR_RETURN_LOG_ERRNO(int, "Cannot consume the user event %d", fd);
		}
	}

	return 0;
}

#endif /*__DARWIN__ */
