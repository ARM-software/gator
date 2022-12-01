#
# Copyright (c) 2022 ARM Limited.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

"yaml loader for ast nodes."

from . import loader_dataclass
from . import ast


class Loader(loader_dataclass.Loader):  # pylint: disable=too-many-ancestors
    "yaml loader class for ast nodes."


Loader.register_type(ast.TypeBuiltin, "!builtin")
Loader.register_type(ast.TypeEnum, "!enum")
Loader.register_type(ast.TypeBitmask, "!bitmask")
Loader.register_type(ast.Constant, "!constant")
Loader.register_type(ast.TypeArray, "!array")
Loader.register_type(ast.TypeFwdDcl, "!forward-declaration")
Loader.register_type(ast.TypePointer64, "!pointer64")
Loader.register_type(ast.TypeOffsetPointer, "!offset-pointer")
Loader.register_type(ast.TypeStruct, "!struct")
Loader.register_type(ast.TypeUnion, "!union")
Loader.register_type(ast.IoctlCommand, "!ioctl-command")
Loader.register_type(ast.IoctlIface, "!ioctl-iface")
