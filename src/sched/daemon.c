/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/un.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>

#include <error.h>
#include <constants.h>

#include <utils/log.h>
#include <utils/static_assertion.h>
#include <utils/thread.h>

#include <itc/module_types.h>
#include <itc/module.h>

#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>
#include <lang/prop.h>

#include <sched/service.h>
#include <sched/loop.h>
#include <sched/daemon.h>

/**
 * @brief The actual data strcuture for the daemon info iterator
 **/
struct _sched_daemon_iter_t {
	DIR*            dir;   /*!< The directory object */
};

/**
 * @brief The command used to control the daemon
 **/
typedef enum {
	_DAEMON_PING,    /*!< Ping a daemon */
	_DAEMON_STOP,    /*!< Stop current daemon */
	_DAEMON_RELOAD,  /*!< Reload current daemon */
	_DAEMON_OP_COUNT /*!< The number of deamon operations */
} _daemon_op_t;

/**
 * @brief The size of the data section for each command
 **/
static const size_t _daemon_op_data_size[_DAEMON_OP_COUNT] = {
	[_DAEMON_STOP] = 0,
	[_DAEMON_PING] = 0,
	[_DAEMON_RELOAD] = 0
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
 * @brief If this thread is the dispatcher thread
 **/
__thread int _is_dispatcher = 0;

/**
 * @brief The lock suffix
 **/
static const char _lock_suffix[] = SCHED_DAEMON_LOCK_SUFFIX;

/**
 * @brief The default group who has the access permission to this daemon
 * @note If the GID is error code, use the default group of current user instead
 **/
static uint32_t _admin_gid = ERROR_CODE(uint32_t);


/**
 * @brief The thread object for the reload thread
 **/
static thread_t* _reload_thread = NULL;

/**
 * @brief Check if the given name is a running daemon
 * @param lockfile The name of the lock file
 * @param suffix The suffix we should append after this lockfile
 * @param pid The buffer for the master process ID for this daemon
 * @return The result or error code
 **/
static inline int _is_running_daemon(const char* lockfile, const char* suffix, int* pid)
{
	int rc = ERROR_CODE(int);
	char pathbuf[PATH_MAX];
	size_t sz = (size_t)snprintf(pathbuf, sizeof(pathbuf), "%s/%s/%s%s", INSTALL_PREFIX, SCHED_DAEMON_FILE_PREFIX, lockfile, suffix);
	if(sz > sizeof(pathbuf) - 1)
		return 0;


	/* Since we have the terminating \0, thus the last letter is at position -2*/
	const char* p = _lock_suffix + sizeof(_lock_suffix) - 2;

	/* If the full path is shorter than the lock suffix, this must be a non-lock file name */
	if(sz < sizeof(_lock_suffix) - 1)
		return 0;

	/* Then we match the lock suffix */
	const char* q = pathbuf + sz - 1;
	for(;p - _lock_suffix >= 0 && *p == *q; p--, q--);

	/* If the suffix doesn't match, then this is not a lock file */
	if(p - _lock_suffix >= 0) return 0;

	/* Open the file */
	int fd = open(pathbuf, O_RDONLY);
	if(fd < 0)
	{
		if(errno == EPERM)
			LOG_WARNING_ERRNO("Cannot access the lockfile %s", pathbuf);
		return 0;
	}

	/* Then we need to examine if the lock file is actually locked by other process */
	errno = 0;
	if(flock(fd, LOCK_EX | LOCK_NB) < 0)
	{
		if(errno == EWOULDBLOCK)
		{
			if(pid != NULL)
			{
				char buf[32];
				ssize_t rdsz;
				if((rdsz = read(fd, buf, sizeof(buf) - 1)) < 0)
					ERROR_LOG_ERRNO_GOTO(RET, "Cannot read the lock file %s", pathbuf);
				buf[rdsz] = 0;
				*pid = (pid_t)atoi(buf);
			}
			rc = 1;
		}
		else ERROR_LOG_ERRNO_GOTO(RET, "Cannot test the flock for %s", pathbuf);
	}
	else if(flock(fd, LOCK_UN | LOCK_NB) < 0)
		ERROR_LOG_ERRNO_GOTO(RET, "Cannot release the flock for %s", pathbuf);
	else
		rc = 0;
RET:
	close(fd);
	return rc;
}

static inline int _set_prop(const char* symbol, lang_prop_value_t value, const void* data)
{
	(void)data;
	if(strcmp(symbol, "id") == 0)
	{
		if(value.type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mismatch");
		snprintf(_id, sizeof(_id), "%s", value.str);
	}
	else if(strcmp(symbol, "admin_group") == 0)
	{
		if(value.type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mismatch");
		struct group* g_info = getgrnam(value.str);

		if(NULL == g_info)
			ERROR_RETURN_LOG_ERRNO(int, "Group %s not found", value.str);

		_admin_gid = g_info->gr_gid;
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
	else if(strcmp(symbol, "admin_group") == 0)
	{
		uint32_t gid = _admin_gid == ERROR_CODE(uint32_t) ? getgid() : _admin_gid;
		struct group* g_info = getgrgid(gid);
		ret.type = LANG_PROP_TYPE_ERROR;
		if(NULL == g_info)
		{
			LOG_ERROR_ERRNO("Cannot get infor for GID %u", gid);
			return ret;
		}

		if(NULL == (ret.str = strdup(g_info->gr_name)))
		{
			LOG_ERROR_ERRNO("Cannot allocate memory for the groupname");
			return ret;
		}

		ret.type = LANG_PROP_TYPE_STRING;
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

static void* _reload_main(void* param)
{
	static int running = 0;
	int status = 0;
	int fd = *(int*)param;
	int switched_namespace = 0;

	int started = 0;

	if(running || !__sync_bool_compare_and_swap(&running, 0, 1))
		ERROR_LOG_ERRNO_GOTO(ERR, "Another deployment process is undergoing");

	started = 1;


	if(ERROR_CODE(int) == runtime_stab_switch_namespace())
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot switch the servlet table namespace");

	switched_namespace = 1;

	sched_service_t* new_service = sched_service_from_fd(fd);
	if(NULL == new_service)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot load new service from the control socket");

	if(ERROR_CODE(int) == sched_loop_deploy_service_object(new_service))
		ERROR_LOG_GOTO(ERR, "Cannot deploy the new service object to the scheduler");

	while(!sched_loop_deploy_completed())
	{
		LOG_DEBUG("Deployment is in process, wait 100ms");
		usleep(100000);
	}

	if(ERROR_CODE(int) == runtime_stab_dispose_unused_namespace())
		ERROR_LOG_GOTO(ERR, "Cannot dispose the previous namespace");

	LOG_NOTICE("Service graph has been successfully reloaded");
	if(write(fd, &status, sizeof(status)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot send the operation result ot client");
	close(fd);
	running = 0;
	return NULL;
ERR:
	if(switched_namespace)
		runtime_stab_revert_current_namespace();
	status = -1;
	if(write(fd, &status, sizeof(status)))
		LOG_ERROR_ERRNO("Cannot send the failure status code to client");
	close(fd);
	if(started) running = 0;
	return NULL;
}

int sched_daemon_read_control_sock()
{
	if(_sock_fd < 0 || !_is_dispatcher) return 0;

	LOG_DEBUG("Trying to read the control socket");

	struct sockaddr_un caddr = {};
	int client_fd;
	socklen_t slen = sizeof(caddr);
	if((client_fd = accept(_sock_fd, (struct sockaddr*)&caddr, &slen)) < 0)
	{
		if(errno == EWOULDBLOCK || errno == EAGAIN)
		{
			LOG_DEBUG("Control socket gets nothing");
			return 0;
		}
		if(errno == EMFILE || errno == ENFILE)
		{
			LOG_DEBUG("The system has used up FDs, do not react to the command socket right now");
			return 0;
		}
		ERROR_RETURN_LOG_ERRNO(int, "Cannot accept command from the control socket");
	}

	LOG_NOTICE("Incoming command socket");

	_daemon_cmd_t* cmd = _read_cmd(client_fd);
	if(NULL == cmd)
		ERROR_LOG_GOTO(ERR, "Cannot read the command socket");

	int status = 0;
	static int input_fd = -1;

	switch(cmd->opcode)
	{
		case _DAEMON_STOP:
			LOG_NOTICE("Got DAEMON_STOP command");
			if(kill(0, SIGINT) < 0)
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot send stop signal to deamon");
			if(write(client_fd, &status, sizeof(status)) < 0)
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot send the operation result to client");
			break;
		case _DAEMON_PING:
			LOG_NOTICE("Got DAEMON_PING Command");
			if(write(client_fd, &status, sizeof(status)) < 0)
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot send the operation result ot client");
			break;
		case _DAEMON_RELOAD:
			LOG_NOTICE("Not DAEMON_RELOAD Command");
			if(NULL != _reload_thread && ERROR_CODE(int) == thread_free(_reload_thread, NULL))
				ERROR_LOG_GOTO(ERR, "Cannot dispose the previously used reload thread");
			input_fd = client_fd;
			if(NULL == (_reload_thread = thread_new(_reload_main, &input_fd, THREAD_TYPE_GENERIC)))
				ERROR_LOG_GOTO(ERR, "Cannot create reload thread");
			LOG_NOTICE("Starting reload process");
			goto RET;
		default:
			ERROR_LOG_GOTO(ERR, "Invalid opcode");
	}

	LOG_DEBUG("Command has been processed successfully");

	close(client_fd);

RET:
	free(cmd);
	return 0;
ERR:

	LOG_DEBUG("Command cannot be processed");

	status = -1;
	if(write(client_fd, &status, sizeof(status)))
		LOG_ERROR_ERRNO("Cannot send the failure status code to client");
	close(client_fd);
	if(NULL != cmd) free(cmd);

	return ERROR_CODE(int);
}

static void _sighup_handle(int sigid)
{
	(void)sigid;
	if(ERROR_CODE(int) == sched_daemon_read_control_sock())
		LOG_WARNING("Could not execute command");
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

	if(NULL != _reload_thread && ERROR_CODE(int) == thread_free(_reload_thread, NULL))
		rc = ERROR_CODE(int);

	return rc;
}

int sched_daemon_daemonize(int fork_twice)
{
	int null_fd = -1, starter_pid;
	if(_id[0] == 0) return 0;

	if(fork_twice && (starter_pid = fork()) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot fork the daemon starter");

	if(fork_twice && starter_pid > 0) return 2;

	char lock_path[PATH_MAX];
	char sock_path[PATH_MAX];
	snprintf(lock_path, sizeof(lock_path), "%s/%s/%s%s", INSTALL_PREFIX, SCHED_DAEMON_FILE_PREFIX, _id, SCHED_DAEMON_LOCK_SUFFIX);
	snprintf(sock_path, sizeof(sock_path), "%s/%s/%s%s", INSTALL_PREFIX, SCHED_DAEMON_FILE_PREFIX, _id, SCHED_DAEMON_SOCKET_SUFFIX);

	/* Then we need to make sure that the directory is there */
	char buf[PATH_MAX];
	char *p, *q;
	struct stat st;
	for(p = lock_path, q = buf; *p; *(q++) = *(p++))
		if(*p == '/' && q - buf > 0 && (stat((*q = 0, buf), &st) < 0 || !S_ISDIR(st.st_mode)))
		{
			LOG_INFO("Creating directory %s", buf);
			if(mkdir(buf, 0775) < 0)
				ERROR_RETURN_LOG_ERRNO(int, "Cannot create directory %s", buf);
		}

		if((_lock_fd = open(lock_path, O_RDWR | O_CREAT, 0640)) < 0)
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

	sid = setsid();
	if(sid < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the new SID for the child process");

	if(chdir("/") < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot change current working directory to root");

	null_fd = open("/dev/null", O_RDWR);

	if(null_fd < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open /dev/null");

	if(dup2(null_fd, STDIN_FILENO) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the null fd to STDIN");

	if(dup2(null_fd, STDOUT_FILENO) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the null fd to STDOUT");

	if(dup2(null_fd, STDERR_FILENO) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicae the null fd to STDERR");

	close(null_fd);
	null_fd = -1;

	char pidbuf[32];
	int self_pid;
	int nbytes = snprintf(pidbuf, sizeof(pidbuf), "%d", self_pid = getpid());

	if(write(_lock_fd, pidbuf, (size_t)nbytes) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot write master PID to the lock file");

	/* At this time, we want the scoket accessible to the group and the user, but not anyone else */
	umask(0117);

	/* Then we should create the control socket */
	if((_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create control socket");

	struct sockaddr_un saddr = {};
	saddr.sun_family = AF_UNIX;
	snprintf(saddr.sun_path, sizeof(saddr.sun_path), "%s", sock_path);
	if(bind(_sock_fd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot bind the control socket");

	if(listen(_sock_fd, 128) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot listen to the control socket");

	int flags = fcntl(_sock_fd, F_GETFL, 0);
	if(flags < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the flag for control socket");

	flags |= O_NONBLOCK;
	if(fcntl(_sock_fd, F_SETFL, flags) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the control socket to nonblocking");

	umask(0);

	if(_admin_gid != ERROR_CODE(uint32_t))
	{
		if(fchown(_lock_fd, (uint32_t)-1, _admin_gid) < 0)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot change the group of lock fd to the admin group");

		if(chown(sock_path, (uint32_t)-1, _admin_gid) < 0)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot change the group of control socket to the admin group");
	}

	_is_dispatcher = 1;

	if(signal(SIGHUP, _sighup_handle) == SIG_ERR)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set SIGHUP handler for daemon communication");

	LOG_NOTICE("Plumber daemon %s started PID:%d lockfile:%s sockfile:%s", _id, self_pid, lock_path, sock_path);

	return 1;
ERR:
	if(_lock_fd >= 0)
	{
		flock(_lock_fd, LOCK_UN | LOCK_NB);
		close(_lock_fd);
		unlink(lock_path);
		_lock_fd = -1;
	}

	if(_sock_fd >= 0)
	{
		close(_sock_fd);
		unlink(sock_path);
		_sock_fd = -1;
	}

	if(null_fd >= 0)
		close(null_fd);

	return ERROR_CODE(int);
}

sched_daemon_iter_t* sched_daemon_list_begin()
{
	sched_daemon_iter_t* ret = (sched_daemon_iter_t*)malloc(sizeof(sched_daemon_iter_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the deamon list iterator");

	const char* pid_dir = INSTALL_PREFIX "/" SCHED_DAEMON_FILE_PREFIX;

	if(NULL == (ret->dir = opendir(pid_dir)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open the Plumber daemon directory %s", pid_dir);

	return ret;
ERR:
	if(NULL != ret->dir)
		closedir(ret->dir);

	free(ret);

	return NULL;
}

int sched_daemon_list_next(sched_daemon_iter_t* iter, char** name, int* pid)
{
	if(NULL == iter || NULL == name || NULL == pid)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	struct dirent* ent;

	errno = 0;

	while((ent = readdir(iter->dir)) != NULL)
	{
		int rc = _is_running_daemon(ent->d_name, "", pid);
		if(ERROR_CODE(int) == rc)
			ERROR_LOG_GOTO(ERR, "Cannot examine if the name is a name of Plumber daemon");
		if(rc == 1)
		{
			size_t namelen = strlen(ent->d_name) - sizeof(_lock_suffix) + 1;
			if(NULL == (*name = (char*)malloc(namelen + 1)))
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the name");

			memcpy(*name, ent->d_name, namelen);
			(*name)[namelen] = 0;

			return 1;
		}
	}

	if(errno != 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the directory entry");

	if(ent == NULL)
	{
		closedir(iter->dir);
		free(iter);
	}

	return 0;
ERR:
	if(NULL != iter->dir) closedir(iter->dir);
	free(iter);
	return ERROR_CODE(int);
}

static inline int _connect_control_sock(const char* daemon_name)
{
	int ret = socket(AF_UNIX, SOCK_STREAM, 0);
	if(ret < 0) ERROR_RETURN_LOG(int, "Cannot create UNIX domain socket");

	struct sockaddr_un dest;
	dest.sun_family = AF_UNIX;
	snprintf(dest.sun_path, sizeof(dest.sun_path), "%s/%s/%s%s", INSTALL_PREFIX, SCHED_DAEMON_FILE_PREFIX, daemon_name, SCHED_DAEMON_SOCKET_SUFFIX);
	if(connect(ret, (struct sockaddr*)&dest, sizeof(dest)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot connect the control socket at %s", dest.sun_path);
	return ret;
ERR:
	close(ret);
	return ERROR_CODE(int);
}

static inline int _simple_daemon_command(const char* daemon_name, _daemon_op_t op, int wait_response, int error_daemon_not_found)
{
	int pid, is_daemon;
	if(NULL == daemon_name || ERROR_CODE(int) == (is_daemon = _is_running_daemon(daemon_name, SCHED_DAEMON_LOCK_SUFFIX, &pid)))
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(!is_daemon)
	{
		if(error_daemon_not_found)
			ERROR_RETURN_LOG(int, "Cannot find daemon named %s", daemon_name);
		else
			return ERROR_CODE(int);
	}

	int conn_fd;
	if(ERROR_CODE(int) == (conn_fd = _connect_control_sock(daemon_name)))
		ERROR_LOG_GOTO(ERR, "Cannot connect the control socket");

	if(kill(pid, SIGHUP) < 0)
		LOG_WARNING_ERRNO("Cannot send signal to the daemon");

	_daemon_cmd_t cmd = {
		.opcode = op
	};

	if(write(conn_fd, &cmd, sizeof(cmd)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot write the command to the command socket connection");

	if(wait_response)
	{
		int status;
		if(read(conn_fd, &status, sizeof(status)) < 0)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the response from the socket connection");

		if(status < 0)
			ERROR_LOG_GOTO(ERR, "The daemon returns an error");

		close(conn_fd);
	}
	return wait_response ? 0 : conn_fd;
ERR:

	close(conn_fd);
	return ERROR_CODE(int);
}

int sched_daemon_stop(const char* daemon_name)
{
	return _simple_daemon_command(daemon_name, _DAEMON_STOP, 1, 1);
}

int sched_daemon_ping(const char* daemon_name)
{
	if(ERROR_CODE(int) == _simple_daemon_command(daemon_name, _DAEMON_PING, 1, 0))
		return 0;
	return 1;
}

int sched_daemon_reload(const char* daemon_name, const sched_service_t* service)
{
	int fd = _simple_daemon_command(daemon_name, _DAEMON_RELOAD, 0, 1);
	if(ERROR_CODE(int) == fd)
		return ERROR_CODE(int);

	if(ERROR_CODE(int) == sched_service_dump_fd(service, fd))
		ERROR_LOG_GOTO(ERR, "Cannot dump the service to the control socket");

	int status;
	if(read(fd, &status, sizeof(status)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the response from the socket connection");

	if(status < 0)
		ERROR_LOG_GOTO(ERR,  "The daemon returns an error");

	return 0;
ERR:
	close(fd);
	return ERROR_CODE(int);
}

