/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#include <error.h>
#include <constants.h>

#include <utils/log.h>
#include <utils/static_assertion.h>

#include <lang/prop.h>

#include <sched/daemon.h>

/**
 * @brief The command used to control the daemon
 **/
typedef enum {
	_DAEMON_STOP,    /*!< Stop current daemon */
	_DAEMON_OP_COUNT /*!< The number of deamon operations */
} _daemon_op_t;

/**
 * @brief The size of the data section for each command 
 **/
static const size_t _daemon_op_data_size[_DAEMON_OP_COUNT] = {
	[_DAEMON_STOP] = 0
};


/**
 * @brief The actual command packet 
 **/
typedef struct __attribute__((packed)) {
	_daemon_op_t opcode;   /*!< The operation code */
	char data[0];  /*!< The data section */
} _daemon_cmd_t;
STATIC_ASSERTION_SIZE(_daemon_cmd_t, data, 0);
STATIC_ASSERTION_LAST(_daemon_cmd_t, data);


/**
 * @brief The daemon identifier
 **/
static char _id[SCHED_DAEMON_MAX_ID_LEN];

/**
 * @brief The controlling socket FD
 **/
static int _sock_fd = -1;

/**
 * @brief The FD to the lock file
 **/
static int _lock_fd = -1;

/**
 * @brief The major PID 
 **/
static pid_t _major_pid = -1;

static inline int _set_prop(const char* symbol, lang_prop_value_t value, const void* data)
{
	(void)data;
	if(strcmp(symbol, "id") == 0)
	{
		if(value.type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mismatch");
		snprintf(_id, sizeof(_id), "%s", value.str);
	}
	else return 0;
	return 1;
}

static lang_prop_value_t _get_prop(const char* symbol, const void* param)
{
	(void)param;
	lang_prop_value_t ret = {
		.type = LANG_PROP_TYPE_NONE
	};
	if(strcmp(symbol, "id") == 0)
	{
		ret.type = LANG_PROP_TYPE_STRING;
		
		if(NULL == (ret.str = strdup(_id)))
		{
			LOG_WARNING_ERRNO("Cannot allocate memory for the path string");
			ret.type = LANG_PROP_TYPE_ERROR;
			return ret;
		}
	}

	return ret;
}

static _daemon_cmd_t* _read_cmd(int fd)
{
	_daemon_cmd_t header;
	if(read(fd, &header, sizeof(_daemon_cmd_t)) < 0)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot read data from command socket");

	if(header.opcode >= _DAEMON_OP_COUNT)
		ERROR_PTR_RETURN_LOG_ERRNO("Invalid opocde");

	size_t sz = _daemon_op_data_size[header.opcode];

	_daemon_cmd_t* ret = (_daemon_cmd_t*)malloc(sizeof(*ret) + sz);

	*ret = header;
	if(read(fd, ret->data, sz) < 0)
	{
		free(ret);
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot read data body from command socket");
	}

	return ret;
}

static void _sighup_handle(int sigid)
{
	(void)sigid;
	if(getpid() != _major_pid) return;

	struct sockaddr_un caddr = {};
	int client_fd;
	socklen_t slen = sizeof(caddr);
	if((client_fd = accept(_sock_fd, (struct sockaddr*)&caddr, &slen)))
	{
		LOG_WARNING_ERRNO("Cannot accept the command socket connection");
		return;
	}

	LOG_NOTICE("Incoming command socket");

	_daemon_cmd_t* cmd = _read_cmd(client_fd);
	if(NULL == cmd) 
		ERROR_LOG_GOTO(ERR, "Cannot read the command socket");

	switch(cmd->opcode)
	{
		case _DAEMON_STOP:
			if(kill(0, SIGINT) < 0)
				ERROR_LOG_GOTO(ERR, "Cannot send stop signal to deamon");
			break;
		default:
			ERROR_LOG_GOTO(ERR, "Invalid opcode");
	}

	free(cmd);
	close(client_fd);
	return;
ERR:
	close(client_fd);
	if(NULL != cmd) free(cmd);
	return;
}

int sched_daemon_init()
{
	lang_prop_callback_t cb = {
		.param = NULL,
		.get   = _get_prop,
		.set   = _set_prop,
		.symbol_prefix = "runtime.daemon"
	};

	if(ERROR_CODE(int) == lang_prop_register_callback(&cb))
	    ERROR_RETURN_LOG(int, "Cannot register callback for the runtime prop callback");

	return 0;
}

int sched_daemon_finalize()
{
	int rc = 0;
	char lock_path[PATH_MAX];
	char sock_path[PATH_MAX];

	snprintf(lock_path, sizeof(lock_path), "%s/%s/%s%s", INSTALL_PREFIX, SCHED_DAEMON_FILE_PREFIX, _id, SCHED_DAEMON_LOCK_SUFFIX);
	snprintf(sock_path, sizeof(sock_path), "%s/%s/%s%s", INSTALL_PREFIX, SCHED_DAEMON_FILE_PREFIX, _id, SCHED_DAEMON_SOCKET_SUFFIX);

	if(_sock_fd >= 0)
	{
		if(close(_sock_fd) < 0)
			rc = ERROR_CODE(int);
		if(unlink(sock_path) < 0)
			rc = ERROR_CODE(int);
	}

	if(_lock_fd >= 0)
	{
		if(flock(_lock_fd, LOCK_UN | LOCK_NB) < 0)
			rc = ERROR_CODE(int);
		if(close(_lock_fd) < 0)
			rc = ERROR_CODE(int);
		if(unlink(lock_path) < 0)
			rc= ERROR_CODE(int);
	}

	return rc;
}

int sched_daemon_daemonize()
{
	if(_id[0] == 0) return 0;

	char lock_path[PATH_MAX];
	char sock_path[PATH_MAX];
	snprintf(lock_path, sizeof(lock_path), "%s/%s/%s%s", INSTALL_PREFIX, SCHED_DAEMON_FILE_PREFIX, _id, SCHED_DAEMON_LOCK_SUFFIX);
	snprintf(sock_path, sizeof(sock_path), "%s/%s/%s%s", INSTALL_PREFIX, SCHED_DAEMON_FILE_PREFIX, _id, SCHED_DAEMON_SOCKET_SUFFIX);

	if((_lock_fd = open(lock_path, O_RDWR | O_CREAT, 0600)) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot create the lock file %s", lock_path);

	if(lseek(_lock_fd, 0, SEEK_SET) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot reset the file location to the begining");

	if(flock(_lock_fd, LOCK_EX | LOCK_NB) < 0)
	{
		close(_lock_fd);
		_lock_fd = -1;
		ERROR_RETURN_LOG_ERRNO(int, "Cannot lock the lock file");
	}

	pid_t pid = fork(), sid;
	if(pid < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot fork the PSS process");
	if(pid > 0) exit(EXIT_SUCCESS);
	
	umask(0);

	sid = setsid();
	if(sid < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the new SID for the child process");

	if(chdir("/") < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot change current working directory to root");

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	char pidbuf[32];
	int nbytes = snprintf(pidbuf, sizeof(pidbuf), "%d", _major_pid = getpid());

	if(write(_lock_fd, pidbuf, (size_t)nbytes) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot write master PID to the lock file");

	/* Then we should create the control socket */
	if((_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create control socket");

	struct sockaddr_un saddr = {};
	saddr.sun_family = AF_UNIX;
	snprintf(saddr.sun_path, sizeof(saddr.sun_path), "%s", sock_path);
	if(bind(_sock_fd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot bind the control socket");
	
	int flags = fcntl(_sock_fd, F_GETFL, 0);
	if(-1 == flags)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the flag for control socket");

	flags |= O_NONBLOCK;
	if(-1 == fcntl(_sock_fd, F_SETFL, flags)) 
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the control socket to nonblocking");

	if(signal(SIGHUP, _sighup_handle) == SIG_ERR)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set SIGHUP handler for daemon communication");

	LOG_NOTICE("Plumber daemon %s started PID:%d lockfile:%s sockfile:%s", _id, _major_pid, lock_path, sock_path);

	return 0;
ERR:
	if(_lock_fd > 0) 
	{
		flock(_lock_fd, LOCK_UN | LOCK_NB);
		close(_lock_fd);
		unlink(lock_path);
		_lock_fd = -1;
	}

	if(_sock_fd > 0)
	{
		close(_sock_fd);
		unlink(sock_path);
		_sock_fd = -1;
	}

	return ERROR_CODE(int);
}
