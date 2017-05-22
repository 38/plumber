from pservlet import LOG_FATAL, LOG_ERROR, LOG_NOTICE, LOG_WARNING, LOG_INFO, LOG_TRACE, LOG_DEBUG, log

def error(message): 
	log(LOG_ERROR, message)
def warning(message): 
	log(LOG_WARNING, message)
def notice(message): 
	log(LOG_NOTICE, message)
def info(message): 
	log(LOG_INFO, message)
def trace(message): 
	log(LOG_TRACE, message)
def debug(message): 
	log(LOG_DEBUG, message)
