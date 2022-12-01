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

"Unit tests for `ioctl_idlc.loader_ast.Loader`."

from __future__ import annotations

import unittest

import yaml
from parameterized import parameterized


from ioctl_idlc.loader_ast import Loader

from ioctl_idlc.description import Description
from ioctl_idlc import ast


class TestLoaderAst(unittest.TestCase):
    "Unit tests for `ioctl_idlc.loader_ast.Loader`."

    @parameterized.expand(
        [
            ("!builtin uint32_t", ast.TypeBuiltin("uint32_t")),
            (
                """
            !enum
            name: test_enum
            description: Test enum.
            underlying_type: !builtin uint32_t
            values:
              - name: value0
                description: Test value 0.
              - name: value1
                description: Test value 1.
            """,
                ast.TypeEnum(
                    "test_enum",
                    ast.Description("Test enum."),
                    ast.TypeBuiltin("uint32_t"),
                    (
                        ast.EnumValue("value0", Description("Test value 0.")),
                        ast.EnumValue("value1", Description("Test value 1.")),
                    ),
                ),
            ),
            (
                """!constant
            name: test_constant
            description: Test constant.
            type: !builtin uint32_t
            value: 42
            """,
                ast.Constant(
                    name="test_constant",
                    description=ast.Description("Test constant."),
                    type=ast.TypeBuiltin("uint32_t"),
                    value="42",
                ),
            ),
            (
                """!array
            element_type: !builtin uint32_t
            array_size: 42
            """,
                ast.TypeArray(
                    element_type=ast.TypeBuiltin("uint32_t"),
                    array_size="42",
                ),
            ),
            (
                "!pointer64 {value_type: !builtin uint32_t, const: yes}",
                ast.TypePointer64(ast.TypeBuiltin("uint32_t"), True),
            ),
            (
                """!offset-pointer
            value_type: !builtin uint64_t
            representation_type: !builtin uint32_t
            const: yes
            """,
                ast.TypeOffsetPointer(
                    ast.TypeBuiltin("uint64_t"), ast.TypeBuiltin("uint32_t"), True
                ),
            ),
            (
                """!forward-declaration
            name: test_type
            description: Test description.
            keyword: struct
            """,
                ast.TypeFwdDcl(
                    "test_type", ast.Description("Test description."), "struct"
                ),
            ),
            (
                """!struct
            name: test_struct
            description: Test struct.
            fields:
              - name: test_field
                description: Test field.
                type: !builtin uint32_t
            """,
                ast.TypeStruct(
                    name="test_struct",
                    description=ast.Description("Test struct."),
                    fields=(
                        ast.Field(
                            "test_field",
                            ast.Description("Test field."),
                            ast.TypeBuiltin("uint32_t"),
                        ),
                    ),
                ),
            ),
            (
                """!union
            name: test_union
            description: Test union.
            fields:
              - name: test_field
                description: Test field.
                type: !builtin uint32_t
            """,
                ast.TypeUnion(
                    name="test_union",
                    description=ast.Description("Test union."),
                    fields=(
                        ast.Field(
                            "test_field",
                            ast.Description("Test field."),
                            ast.TypeBuiltin("uint32_t"),
                        ),
                    ),
                ),
            ),
            (
                """!ioctl-command
            name: test_command
            description: Test description.
            number: 42
            command_type: _IOW
            arg_type: !builtin uint32_t
            """,
                ast.IoctlCommand(
                    name="test_command",
                    description=ast.Description("Test description."),
                    number=ast.Number(42),
                    command_type="_IOW",
                    arg_type=ast.TypeBuiltin("uint32_t"),
                ),
            ),
            (
                """!ioctl-iface
            name: test_iface
            description: Test iface.
            number: 42
            items: []
            commands: []
            """,
                ast.IoctlIface(
                    name="test_iface",
                    description=Description("Test iface."),
                    number=ast.Number(42),
                    items=tuple(),
                    commands=tuple(),
                ),
            ),
        ]
    )
    def test(self, yaml_str: str, expected):
        """
        Test ast nodes parsing.

        :param yaml_str: yaml string to parse.
        :param expected: expected object parsed.
        """
        got = yaml.load(yaml_str, Loader)

        self.assertEqual(expected, got)
