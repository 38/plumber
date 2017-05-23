class PlumberNativeException(Exception):
    def __init__(self, msg):
        self._msg = msg
    def __str__(self):
        return "Plumber Native Exception: %s"%self._msg
class PipeTypeException(Exception):
	def __init__(self, pipe):
		self._pipe = pipe
	def __str__(self):
		return "Unexpected Pipe Type: %s"% "Input" if self._input else "Output"
