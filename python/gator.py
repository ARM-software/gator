## Copyright (C) Arm Limited 2010-2018. All rights reserved.
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License version 2 as
## published by the Free Software Foundation.

import ctypes
import getopt
import os
import platform
import socket
import sys
import code

##
##    Gator python module only supports Linux as a target as it calls clock_gettime to read 'CLOCK_MONOTONIC_RAW'
##
if not platform.system().startswith('Linux'):
    sys.stderr.write("Unsupported operating system\n")
    sys.exit(3)

##
##    Linux dependencies
##
class clock_gettime_timespec(ctypes.Structure):
    _fields_ = [
        ('tv_sec', ctypes.c_long),
        ('tv_nsec', ctypes.c_long)
    ]

__GATOR_CLOCK_MONOTONIC_RAW = 4
__GATOR_LIBRT = ctypes.CDLL('librt.so.1', use_errno=True)
__GATOR_LIBC = ctypes.CDLL('libc.so.6', use_errno=True)
__GATOR_clock_gettime = __GATOR_LIBRT.clock_gettime
__GATOR_clock_gettime.argtypes = [ctypes.c_int, ctypes.POINTER(clock_gettime_timespec)]
__GATOR_machine = platform.machine()
# this value varies by platform and is only correct for arm/x86. other targets will fail
__GATOR_NR_gettid = 178 if (__GATOR_machine.startswith("armv8") or __GATOR_machine.startswith("arm64") or __GATOR_machine.startswith("aarch64")) else (186 if (__GATOR_machine.startswith("x86_64") or __GATOR_machine.startswith("amd64")) else 224)

def gator_monotonic_time():
    """ Calls clock_gettime(CLOCK_MONOTONIC_RAW) and returns the time in nanoseconds """
    t = clock_gettime_timespec()
    if __GATOR_clock_gettime(__GATOR_CLOCK_MONOTONIC_RAW, ctypes.pointer(t)) != 0:
        errno_ = ctypes.get_errno()
        raise OSError(errno_, os.strerror(errno_))
    return t.tv_sec * 1000000000 + t.tv_nsec

def gator_gettid():
    """ Calls the gettid syscall to read back the linux thread ID """
    return __GATOR_LIBC.syscall(__GATOR_NR_gettid)

##
##    GatorProfiler
##
class GatorProfiler:
    """Gator profiler class.

    This class provides the main profiling functionality and is intended to implement a similar interface to the building 'trace' and 'profile' modules.

    Usage:

        * Invoke the module from the command line like so: `python -m gator [gator_args...] <your_module> [your_module_args...]`
        * Import into your code and explicitly instantiate and use:

                import gator

                g = gator.GatorProfiler()
                g.runctx(some_function, args=(arg1, arg2, arg3), kwargs={'kwarg1': kwarg1})

    Command line arguments:

        Run `python gator --help` for more details

    """

    def __init__(self, trace_mode = False, debug_enabled = False, ignored_paths=[]):
        """
        @param trace_mode     Enables trace mode (uses sys.settrace instead of sys.setprofile) and traces every line executed rather than just function entry or exit
        @param debug_enabled  Enabled output of verbose debug messages. Only useful for tracing gator module behaviour and reporting bugs.
        @param ignored_paths  List of python script paths to ignore
        """
        self.__trace_mode = trace_mode
        self.__debug_enabled = debug_enabled
        self.__search_paths = sys.path
        self.__python_language_cookie = 1
        self.__ignored_paths = []
        self.__ignored_paths.append(self.__strip_pyc(__file__))
        self.__ignored_paths.append(self.__make_abs(__file__))
        self.__ignored_paths.extend([self.__strip_pyc(x) for x in ignored_paths])
        self.__ignored_paths = set(self.__ignored_paths)
        self.__debug("f = %s, i = %s" % (__file__, self.__ignored_paths,))

        try:
            import threading
        except ImportError:
            if self.__trace_mode:
                self.__setprofile = lambda func: sys.settrace(func)
                self.__unsetprofile = lambda: sys.settrace(None)
            else:
                self.__setprofile = lambda func: sys.setprofile(func)
                self.__unsetprofile = lambda: sys.setprofile(None)
            tls_data = self
            self.__get_tls_data = lambda: self.__init_tls_data(tls_data)
        else:
            if self.__trace_mode:
                self.__setprofile = lambda func: [x(func) for x in [threading.settrace, sys.settrace]]
                self.__unsetprofile = lambda: [x(None) for x in [sys.settrace, threading.settrace]]
            else:
                self.__setprofile = lambda func: [x(func) for x in [threading.setprofile, sys.setprofile]]
                self.__unsetprofile = lambda: [x(None) for x in [sys.setprofile, threading.setprofile]]
            tls_data = threading.local()
            self.__get_tls_data = lambda: self.__init_tls_data(tls_data)

    def __init_tls_data(self, object):
        """Initialise per-thread data.
           Socket and cookie map are kept per-thread to avoid the need for any kind of locking when sending or manipulating the cookie map"""

        initialized = getattr(object, 'initialized', None)
        if initialized is None:
            self.__debug("Initializing per thread data for %s:%s" % (os.getpid(), gator_gettid(), ))
            # Create the socket
            try:
                object.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                object.socket.connect("\0streamline-annotate")
                # send the protocol identifier
                object.socket.sendall(bytearray("SCRIPT STACK 1\n", 'utf8'))
                # send python language cookie string
                version_string = bytearray("Python %s.%s.%s" % (sys.version_info.major, sys.version_info.minor, sys.version_info.micro), 'utf8')
                data = [0x00]
                data.extend(self.__pack_int(self.__python_language_cookie))
                data.extend(self.__pack_int(len(version_string)))
                data.extend(version_string)
                object.socket.sendall(bytearray(data))
            except (socket.error) as e:
                self.__debug("gator: Cannot connect to gatord for thread %s:%s\n" % (os.getpid(), gator_gettid(), ))
                object.socket = None
            # configure cookies
            object.cookie_counter = 1
            object.cookie_map = {}
            # Done with initialization
            object.initialized = True
        return object

    def __debug(self, message):
        """Output a debug message"""
        if (self.__debug_enabled):
            sys.stderr.write("%s\n" % (message,))

    def __strip_pyc(self, filename):
        """Remove the .pyc suffix and replace with .py"""
        return filename[:-1] if filename.endswith(".pyc") else filename

    def __is_ignored(self, filename):
        """Check if some script is ignored"""
        # Always ignore this script
        if __file__ == filename:
            return True
        # Is it in user supplied list
        if filename in self.__ignored_paths:
            return True
        # Everything else is profiled
        return False

    def __is_traced_event(self, event):
        return (self.__trace_mode and (event == 'line')) or ((not self.__trace_mode) and (event == 'call'))

    def __profile_handler(self, frame, event, arg):
        """Main profiling function, called by either sys.settrace or sys.setprofile action"""
        self.__debug("__profile_handler('%s', %s, '%s')" % (frame.f_code.co_filename, frame.f_lineno, event))
        if self.__is_ignored(self.__make_abs(frame.f_code.co_filename)):
            return None
        tls_data = self.__get_tls_data()
        if (self.__is_traced_event(event)):
            stack = []
            while (frame is not None):
                abs_filename = self.__make_abs(frame.f_code.co_filename)
                if self.__is_ignored(abs_filename):
                    break
                stack.append((self.__cookie_for(tls_data, abs_filename), frame.f_lineno))
                frame = frame.f_back
            if (len(stack) > 0):
                self.__emit(tls_data, os.getpid(), gator_gettid(), gator_monotonic_time(), stack)
        return self.__profile_handler if tls_data.socket is not None else None

    def __emit(self, tls_data, pid, tid, timestamp, stack):
        """Transmit a call stack message to gatord"""
        self.__debug("__emit(%s, %s, %s, %s)" % (pid, tid, timestamp, stack, ))
        if tls_data.socket is not None:
            data = [0x2]
            data.extend(self.__pack_int(timestamp))
            data.extend(self.__pack_int(pid))
            data.extend(self.__pack_int(tid))
            data.extend(self.__pack_int(len(stack)))
            for cookie, line in stack:
                data.extend(self.__pack_int(cookie))
                data.extend(self.__pack_int(line))
            try:
                tls_data.socket.sendall(bytearray(data))
            except (socket.error) as e:
                self.__debug("gator: Lost connection to socket: %s:%s %s\n" % (os.getpid(), gator_gettid(), e, ))
                tls_data.socket = None

    def __emit_cookie(self, tls_data, filename, cookie):
        """Transmit a new source file cookie message to gatord"""
        self.__debug("__emit_cookie('%s', %s)" % (filename, cookie, ))
        if tls_data.socket is not None:
            filename_bytes = bytearray(filename, 'utf8')
            data = [0x1]
            data.extend(self.__pack_int(self.__python_language_cookie))
            data.extend(self.__pack_int(cookie))
            data.extend(self.__pack_int(len(filename_bytes)))
            data.extend(filename_bytes)
            try:
                tls_data.socket.sendall(bytearray(data))
            except (socket.error) as e:
                self.__debug("gator: Lost connection to socket: %s:%s %s\n" % (os.getpid(), gator_gettid(), e, ))
                tls_data.socket = None

    def __make_abs(self, filename):
        """Convert the filename given to an absolute path"""
        if (filename is None):
            return None
        result = filename
        # make sure relative paths are mapped to an actual file
        if not os.path.isabs(filename):
            for path in self.__search_paths:
                testpath = os.path.join(path, filename)
                if os.path.exists(testpath):
                    result = testpath
                    break
        # must send an absolute path
        result = self.__strip_pyc(os.path.abspath(result))
        self.__debug("__make_abs(%s) = %s" % (filename, result,))
        return result

    def __cookie_for(self, tls_data, filename):
        """Get (or create) the cookie for a given script"""
        # map the cookie
        if filename in tls_data.cookie_map:
            return tls_data.cookie_map[filename]
        result = tls_data.cookie_counter
        tls_data.cookie_counter += 1
        tls_data.cookie_map[filename] = result
        self.__emit_cookie(tls_data, filename, result)
        return result

    def __pack_int(self, n):
        """leb128 encode some integer and return the encoded bytes"""
        result = []
        more = True
        while more:
            b = n & 0x7f
            n = n >> 7
            if ((n == 0) and ((b & 0x40) == 0)) or ((n == -1) and ((b & 0x40) != 0)):
                more = False
            else:
                b = b | 0x80
            result.append(b)
        return result

    def run(self, script_or_code):
        """Equivalent to `runctx(script_or_code, __main__.__dict__, __main__.__dict__)`"""
        self.runctx(script_or_code, __main__.__dict__, __main__.__dict__)

    def runctx(self, script_or_code, globals = {}, locals = {}):
        """
        Execute some code object using `eval`.

        @param script_or_code The main value to pass to exec (a code object or some python source)
        @param globals        The dictionary to pass to `eval` as the globals
        @param locals         The dictionary to pass to `eval` as the locals
        """
        self.__setprofile(self.__profile_handler)
        try:
            exec(script_or_code, globals, locals)
        finally:
            self.__unsetprofile()

    def call(self, callable, args=(), kwargs={}):
        """
        Profile some callable object.

        Executes `callable(*args, **kwargs)`

        @param callable    The callable object
        @param args        A tuple of argument values
        @param kwargs      A map containing keyword argument values
        @return Whatever the callable returned
        """
        self.__setprofile(self.__profile_handler)
        try:
            return callable(*args, **kwargs)
        finally:
            self.__unsetprofile()

##
##    Command line interface
##

def _gator_help():
    sys.stderr.write("gator: Profile or trace your Python scripts using Streamline.\n"
                     "\n"
                     "Usage: 'python -m gator [-d|--debug] [-t|--trace] <python_script> ...\n"
                     "\n"
                     "Where:\n"
                     "       -h | --help         This message\n"
                     "       -d | --debug        Enable debug messages\n"
                     "       -t | --trace        Enable tracing line numbers rather than just function call entry and exits\n"
                     "       <python_script>     The script to execute followed by its arguments\n")

def _gator_main():
    trace_mode = False
    debug_enabled = False

    try:
        options, arguments = getopt.getopt(sys.argv[1:], "htd", ["help", "trace", "debug"])

        for opt, val in options:
            if (opt == '-h') or (opt == '--help'):
                _gator_help()
                sys.exit(0)

            elif (opt == '-t') or (opt == '--trace'):
                trace_mode = True

            elif (opt == '-d') or (opt == '--debug'):
                debug_enabled = True

    except (getopt.error) as e:
        sys.stderr.write("gator: %s\n" % (e,))
        sys.exit(1)

    if len(arguments) < 1:
        sys.stderr.write("gator: no <python_script> specified\n")
        sys.exit(2)

    # override the environment that the python script sees
    python_script = arguments[0]
    sys.argv = arguments
    sys.path[0] = os.path.split(python_script)[0]

    profiler = GatorProfiler(trace_mode=trace_mode,
                             debug_enabled=debug_enabled)

    try:
        with open(python_script) as fp:
            code = compile(fp.read(), python_script, 'exec')
        globals = {
            '__file__':     python_script,
            '__name__':     '__main__',
            '__package__':  None,
            '__cached__':   None,
        }
        profiler.runctx(code, globals, globals)
    except (IOError) as e:
        sys.stderr.write("gator: Failed to execute %s due to %s\n" % (python_script, e))
    except SystemExit:
        pass

if __name__=='__main__':
    _gator_main()
