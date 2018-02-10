/**
 * Copyright (C) 2017-2018, Hao Hou
 **/
#include <constants.h>
#ifdef __LINUX__
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <error.h>

#include <os/os.h>

#include <utils/log.h>

/**
 * @brief The actual data structure of the poll object
 **/
struct _os_event_poll_t {
	int                  epoll_fd;        /*!< The actual epoll FD */
	struct epoll_event*  event_buf;       /*!< The last event buffer */
	size_t               event_buf_size;  /*!< The event buffer size */
};

os_event_poll_t* os_event_poll_new()
{
	os_event_poll_t* ret = (os_event_poll_t*)malloc(sizeof(*ret));

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new poll object");

	ret->event_buf = NULL;
	ret->event_buf_size = 0;

	if((ret->epoll_fd = epoll_create1(0)) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create epoll FD for the poll object");

	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

int os_event_poll_free(os_event_poll_t* poll)
{
	int rc = 0;
	if(NULL == poll) ERROR_RETURN_LOG(int, "Invalid arguments");

	/* In this case any event buf do not occupies the ownership of the data pointer,
	 * so we can dipose the event buffer directly */
	if(NULL != poll->event_buf) free(poll->event_buf);

	if(close(poll->epoll_fd) < 0)
	{
		LOG_ERROR_ERRNO("Cannot close the epoll FD %d", poll->epoll_fd);
		rc = ERROR_CODE(int);
	}

	free(poll);

	return rc;
}
static inline unsigned _get_epoll_flags(os_event_kernel_event_desc_t* kev)
{
	unsigned epoll_flags = 0;
	switch(kev->event)
	{
		case OS_EVENT_KERNEL_EVENT_IN:
		case OS_EVENT_KERNEL_EVENT_CONNECT:
		    epoll_flags = EPOLLIN | EPOLLET;
		    break;
		case OS_EVENT_KERNEL_EVENT_OUT:
		    epoll_flags = EPOLLOUT | EPOLLET;
		    break;
		case OS_EVENT_KERNEL_EVENT_BIDIR:
		    epoll_flags = EPOLLIN | EPOLLOUT | EPOLLET;
		    break;
		default:
		    ERROR_RETURN_LOG(unsigned, "Invalid kernel event type");
	}
	return epoll_flags;
}

int os_event_poll_modify(os_event_poll_t* poll, os_event_desc_t* desc)
{
	if(NULL == poll || NULL == desc)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(desc->type != OS_EVENT_TYPE_KERNEL)
	    ERROR_RETURN_LOG(int, "Only kernel event is allowed");

	unsigned epoll_flags = _get_epoll_flags(&desc->kernel);

	if(ERROR_CODE(unsigned) == epoll_flags)
	    ERROR_RETURN_LOG(int, "Cannot determine the epoll flags");

	struct epoll_event event = {
		.events = epoll_flags,
		.data = {
			.ptr = desc->kernel.data
		}
	};

	if(epoll_ctl(poll->epoll_fd, EPOLL_CTL_MOD, desc->kernel.fd, &event) >= 0)
	    return 0;

	if(errno == ENOENT)
	{
		/* If this epoll event is not exist, we could add a new one */
		if(epoll_ctl(poll->epoll_fd, EPOLL_CTL_ADD, desc->kernel.fd, &event) < 0)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot add FD to the epoll list");
		return 0;
	}

	ERROR_RETURN_LOG_ERRNO(int, "epoll_ctl returns an error");
}

int os_event_poll_add(os_event_poll_t* poll, os_event_desc_t* desc)
{
	if(NULL == poll || NULL == desc)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	int fd = -1;
	unsigned epoll_flags = 0;
	void* data = NULL;
	switch(desc->type)
	{
		case OS_EVENT_TYPE_KERNEL:
		    fd = desc->kernel.fd;
		    data = desc->kernel.data;
		    if(ERROR_CODE(unsigned) == (epoll_flags = _get_epoll_flags(&desc->kernel)))
		        ERROR_RETURN_LOG(int, "Cannot determine the epoll flags");
		    break;
		case OS_EVENT_TYPE_USER:
		    if((fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) < 0)
		        ERROR_RETURN_LOG_ERRNO(int, "Cannot create eventfd for the user space event");
		    data = desc->user.data;
		    epoll_flags = EPOLLIN | EPOLLET;
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid event type");
	}

	struct epoll_event event = {
		.events = epoll_flags,
		.data = {
			.ptr = data
		}
	};

	if(epoll_ctl(poll->epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot add target FD to the epoll");

	return fd;
ERR:
	if(desc->type == OS_EVENT_TYPE_USER && fd >= 0) close(fd);
	return ERROR_CODE(int);
}

int os_event_poll_del(os_event_poll_t* poll, int fd, int read)
{
	(void)read;
	if(NULL == poll || fd < 0) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(epoll_ctl(poll->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot delete the target FD from epoll");

	return 0;
}

int os_event_poll_wait(os_event_poll_t* poll, size_t max_events, int timeout)
{
	if(NULL == poll || max_events == 0)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(max_events > poll->event_buf_size)
	{
		if(NULL != poll->event_buf) free(poll->event_buf);
		poll->event_buf_size = 0;
		if(NULL == (poll->event_buf = (struct epoll_event*)calloc(max_events, sizeof(struct epoll_event))))
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate the event buffer");
		poll->event_buf_size = max_events;
	}

	int ret = epoll_wait(poll->epoll_fd, poll->event_buf, (int)max_events, timeout);

	if(ret < 0)
	{
		if(errno != EINTR)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot finish epoll syscall");
		return 0;
	}

	return ret;
}

void* os_event_poll_take_result(os_event_poll_t* poll, size_t idx)
{
	if(NULL == poll || idx > poll->event_buf_size)
	    return NULL;

	return poll->event_buf[idx].data.ptr;
}

int os_event_user_event_consume(os_event_poll_t* poll, int fd)
{
	(void)poll;
	uint64_t next;
	ssize_t rc = read(fd, &next, sizeof(next));
	if(rc < 0) ERROR_RETURN_LOG_ERRNO(int, "Cannot read event fd");

	return 0;
}

#endif /*__LINUX__ */
