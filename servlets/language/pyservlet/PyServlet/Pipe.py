import re
import pservlet
import PlumberExceptions
class _PipeState:
	def __init__(self, unread = "", user_defined = None):
		self._unread = unread
		self.ustate = user_defined
	def read(self, n = None):
		if n > len(self._unread): 
			n = None
		if n == None:
			ret = self._unread
			self._unread = ""
			return ret
		else:
			ret = self._unread[:n]
			self._unread = self._unread[n:]
			return ret
	def unread(self, buf):
		self._unread = buf + self._unread
class Pipe:
	def __init__(self, pd, line_delimitor = r"\r\n"):
		"""Create a new pipe object from the given pipe descriptor"""
		self._pipe_desc = pd
		if 0 == (pservlet.pipe_get_flags(pd) & pservlet.PIPE_OUTPUT):
			self._state = pservlet.pipe_pop_state(pd)
			self._input = True
		else:
			self._state = None
			self._input = False
		if self._input and self._state == None:
			self._state = _PipeState()
		self._nl_pattern = re.compile(line_delimitor)
	def __del__(self):
		"""In the destructor, we need to push the state"""
		if self.ispersist() and self._input:
				pservlet.pipe_push_state(self._pipe_desc, self._state)
	def read(self, n = None):
		"""Read n bytes from the pipe"""
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		saved = self._state.read(n)
		if n == None:
			return saved + pservlet.pipe_read(self._pipe_desc)
		elif n == len(saved): 
			return saved
		else:
			return saved + pservlet.pipe_read(self._pipe_desc, n - len(saved))
	def write(self, s):
		"""Write to the pipe"""
		if self._input: raise PlumberExceptions.PipeTypeException(self)
		return pservlet.pipe_write(self._pipe_desc, s)
	def unread(self, s):
		"""Return the data to buffer"""
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		self._state.unread(s)
	def getstate(self):
		"""Get current user-defined state"""
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		return self._state.ustate
	def setstate(self, state):
		"""Set the use-defined state"""
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		self._state.ustate = state
	def eof(self):
		"""Check if the pipe has not more data"""
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		result = pservlet.pipe_eof(self._pipe_desc)
		if result > 0: return True
		elif result == 0: return False
		raise PlumberExceptions.PlumberNativeException("Cannot finish the API call to pipe_eof")
	def readline(self):
		"""Read a signle line from the pipe, the pipe delimiter is used""" 
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		if self.eof(): return None
		ret = ""
		while not self.eof():
			buf = self.read()
			if not buf:
				if not self.eof():
					self._state.unread(buf)
					return ""
				else:
					return None
			nl = self._nl_pattern.search(buf)
			if nl:
				ret = ret + buf[:nl.span()[1]]
				self.unread(buf[nl.span()[1]:])
				return ret
			else:
				ret = ret + buf
		return ret
	def readlines(self, n = None):
		"""Read all lines from the pipe"""
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		result = []
		while n == None or n > 0:
			line = self.readline()
			if not line: return result
			result.append(line)
			n -= 1
		return result
	def __iter__(self):
		"""Get the iterator"""
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		return self
	def next(self):
		"""Get next line"""
		if not self._input: raise PlumberExceptions.PipeTypeException(self)
		line = self.readline()
		if line: 
			return line
		else: 
			raise StopIteration()
	def close(self):
		"""The dummy close"""
		return None
	def setflag(self, flag):
		"""Set a pipe flag"""
		return pservlet.pipe_set_flag(self._pipe_desc, flag)
	def getflag(self, flag):
		"""Get a pipe flag"""
		return (pservlet.pipe_get_flags(self._pipe_desc) & flag) != 0
	def ispersist(self):
		"""Check if the pipe is a long connection"""
		return self.getflag(pservlet.PIPE_PERSIST)
	def setpersist(self, v = True):
		"""Set the value of the persist bit"""
		if v:
			pservlet.pipe_set_flag(self._pipe_desc, pservlet.PIPE_PERSIST)
		else:
			pservlet.pipe_clr_flag(self._pipe_desc, pservlet.PIPE_PERSIST)
