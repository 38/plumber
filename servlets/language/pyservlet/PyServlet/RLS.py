"""
    Copyright (C) 2017, Hao Hou
"""
import PyServlet.Type
import pservlet

class String(PyServlet.Type.ModelBase):
    token = PyServlet.Type.ScopeToken()
    def read(self):
        rls_obj = pservlet.RLS_Object(pservlet.SCOPE_TYPE_STRING, self.token)
        return pservlet.RLS_String.get_value(rls_obj)
    def write(self, val):
        rls_obj = pservlet.RLS_Object(pservlet.SCOPE_TYPE_STRING, -1, val)
        self.token = rls_obj.get_token()
