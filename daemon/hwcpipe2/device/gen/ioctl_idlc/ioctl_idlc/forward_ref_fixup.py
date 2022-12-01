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

"get_type_hints implementation with forward references resolution."

import typing
import importlib

_normalized_to_hint = {
    set: typing.Set,
    list: typing.List,
    tuple: typing.Tuple,
    dict: typing.Dict,
}


def _forward_ref_arg(ref: typing.ForwardRef) -> str:
    """
    Extract argument from a forward reference.

    :param ref: Forward reference to extract argument from.
    :return: The argument extracted.
    """
    # Since there is no API to extract this info from,
    # we use its string representation.
    as_str = repr(ref)
    # ForwardRef('SomeArgument')
    #             ^           ^
    #            12          -2
    return as_str[12:-2]


def _fixup_forward_ref(hint, module):
    """
    Scan a type hint recursively and resolve all forward refs from a given module.

    :param hint: Type hint to scan.
    :param module: Module to resolve forward references from.
    :return: Type hint with forward references resolved.
    """
    if isinstance(hint, typing.ForwardRef):
        return getattr(module, _forward_ref_arg(hint))

    origin = typing.get_origin(hint)
    if origin is None:
        return hint

    args = typing.get_args(hint)

    fixed_args = tuple((_fixup_forward_ref(arg, module) for arg in args))

    origin = _normalized_to_hint.get(origin, origin)

    if len(fixed_args) == 1:
        return origin[fixed_args[0]]

    return origin[fixed_args]


def get_type_hints(cls):
    """
    Get type hints.

    This function is similar to typing.get_type_hints, except it supports
    forward references resolution for older python versions.

    :param cls: Type to get hints for.
    :return: Type hints.
    """
    hints = typing.get_type_hints(cls)
    module = importlib.import_module(cls.__module__)

    fixed_hints = {
        name: _fixup_forward_ref(hint, module) for name, hint in hints.items()
    }

    return fixed_hints
