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

"Unit tests for `ioctl_idlc.ast` types."

from __future__ import annotations

import unittest

from ioctl_idlc.description import Description
from ioctl_idlc import ast


class TestAstTypes(unittest.TestCase):
    "Unit tests for `ioctl_idlc.ast` types."

    test_name = "test_name"
    test_description = Description("Test description.")

    def __test_named(self, cls, *args, **kwargs):
        """
        Test named types.

        :param cls: Type to test.
        :param args: Arguments to pass to the type constructor.
        :param kwargs: Keyword arguments to pass to the type constructor.
        """

        self.assertRaises(ValueError, cls, "no spaces allowed", *args, **kwargs)
        self.assertRaises(ValueError, cls, "0must_start_with_letter", *args, **kwargs)

        if cls is ast.TypeBuiltin:
            return

        # Name must not conflict with a built-in type.
        self.assertRaises(ValueError, cls, "uint32_t", *args, **kwargs)

    def test_full_name(self):
        "Test `full_name_cxx` and `full_name_c` properties."
        type_a = ast.Named("a", TestAstTypes.test_description)
        type_b = ast.Named("b", TestAstTypes.test_description)
        type_c = ast.Named("c", TestAstTypes.test_description)

        type_b.parent = type_a
        type_c.parent = type_b

        self.assertEqual("b::c", type_c.full_name_cxx)
        self.assertEqual("b_c", type_c.full_name_c)

        self.assertEqual("b", type_b.full_name_cxx)
        self.assertEqual("b", type_b.full_name_c)

        self.assertEqual("a", type_a.full_name_cxx)
        self.assertEqual("a", type_a.full_name_c)

    def test_builtin(self):
        "Test `ast.TypeBuiltin` type."
        with self.subTest("as_named_node"):
            self.__test_named(ast.TypeBuiltin)

        with self.subTest("unknown_type"):
            self.assertRaises(ValueError, ast.TypeBuiltin, "unknwon_type_t")

        with self.subTest("valid_value"):
            node = ast.TypeBuiltin("uint32_t")

            self.assertEqual("uint32_t", node.name)

    def test_enum_value(self):
        "Test `ast.EnumValue` type."
        with self.subTest("as_named_node"):
            self.__test_named(ast.EnumValue, TestAstTypes.test_description)

        with self.subTest("valid_value"):
            node = ast.EnumValue(
                TestAstTypes.test_name, TestAstTypes.test_description, "42"
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)
            self.assertEqual("42", node.value)

    def test_bitmask_value(self):
        "Test `ast.EnumValue` type."
        with self.subTest("as_named_node"):
            self.__test_named(ast.BitmaskValue, TestAstTypes.test_description, 10)

        with self.subTest("valid_value"):
            node = ast.BitmaskValue(
                TestAstTypes.test_name, TestAstTypes.test_description, 10
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)
            self.assertEqual(10, node.bit_number)

    def __test_enum_or_bitmask(self, cls, test_value):
        """
        Test `ast.TypeEnum` or `ast.TypeBitmask` type.

        :param cls: Type to test.
        :param test_value: Enum of bitmask value.
        """

        with self.subTest("as_named_node"):
            self.__test_named(
                cls,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                (test_value,),
            )

        with self.subTest("must_contain_at_least_one_value"):
            self.assertRaises(
                ValueError,
                cls,
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                (),
            )

        with self.subTest("value_names_must_be_unique"):
            self.assertRaises(
                ValueError,
                cls,
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                (test_value, test_value),
            )

        with self.subTest("valid_value"):
            node = cls(
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                (test_value,),
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)

            self.assertEqual((test_value,), node.values)
            self.assertEqual(node.values[0].parent, node)

    def test_enum(self):
        "Test `ast.TypeEnum` type."
        test_value = ast.EnumValue("test_value", TestAstTypes.test_description)
        self.__test_enum_or_bitmask(ast.TypeEnum, test_value)

    def test_bitmask(self):
        "Test `ast.TypeBitmask` type."
        test_value = ast.BitmaskValue("test_value", TestAstTypes.test_description, 10)
        self.__test_enum_or_bitmask(ast.TypeBitmask, test_value)

    def test_constant(self):
        "Test `ast.Constant` type."
        with self.subTest("as_named_node"):
            self.__test_named(
                ast.Constant,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                42,
            )

        with self.subTest("valid_value"):
            node = ast.Constant(
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                "42",
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)
            self.assertEqual(ast.TypeBuiltin("uint32_t"), node.type)
            self.assertEqual("42", node.value)

    def test_array(self):
        "Test `ast.TypeArray` type."
        with self.subTest("valid_value"):
            node = ast.TypeArray(ast.TypeBuiltin("uint32_t"), "42")

            self.assertEqual(node.element_type, ast.TypeBuiltin("uint32_t"))
            self.assertEqual(node.array_size, "42")

    def test_pointer64(self):
        "Test `ast.TypePointer64` type."
        with self.subTest("valid_value"):
            node = ast.TypePointer64(ast.TypeBuiltin("uint32_t"), False)

            self.assertEqual(ast.TypeBuiltin("uint32_t"), node.value_type)
            self.assertEqual(False, node.is_constant)

    def test_offset_pointer(self):
        "Test `ast.TypeOffsetPointer` type."
        with self.subTest("valid_value"):
            node = ast.TypeOffsetPointer(
                ast.TypeBuiltin("uint64_t"), ast.TypeBuiltin("uint32_t"), False
            )

            self.assertEqual(ast.TypeBuiltin("uint64_t"), node.value_type)
            self.assertEqual(ast.TypeBuiltin("uint32_t"), node.representation_type)
            self.assertEqual(False, node.is_constant)

    def test_fwd_dcl(self):
        "Test `ast.TypeFwdDcl` type."
        with self.subTest("as_named_node"):
            self.__test_named(ast.TypeFwdDcl, TestAstTypes.test_description, "struct")

        with self.subTest("invalid_keyword"):
            self.assertRaises(
                ValueError,
                ast.TypeFwdDcl,
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                "bad_keyword",
            )

        with self.subTest("valid_value"):
            node = ast.TypeFwdDcl(
                TestAstTypes.test_name, TestAstTypes.test_description, "struct"
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)

    def test_namespace(self):
        "Test `ast.Namespace` type."
        fwd_dcl = ast.TypeFwdDcl("test_fwddcl", TestAstTypes.test_description, "struct")

        with self.subTest("as_named_node"):
            self.__test_named(ast.Namespace, TestAstTypes.test_description, (fwd_dcl,))

        with self.subTest("valid_value"):
            node = ast.Namespace(
                TestAstTypes.test_name, TestAstTypes.test_description, (fwd_dcl,)
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)
            self.assertEqual((fwd_dcl,), node.forward_declarations)

            self.assertEqual(node.forward_declarations[0].parent, node)

    def test_field(self):
        "Test `ast.Field` type."
        with self.subTest("as_named_node"):
            self.__test_named(
                ast.Field,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
            )

        with self.subTest("type_and_name_must_be_different"):
            test_field = ast.Field(
                "test_field",
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
            )
            test_struct = ast.TypeStruct(
                "non_uniq",
                TestAstTypes.test_description,
                (),
                (test_field,),
            )
            self.assertRaises(
                ValueError,
                ast.Field,
                "non_uniq",
                TestAstTypes.test_description,
                test_struct,
            )

        with self.subTest("valid_value"):
            node = ast.Field(
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)
            self.assertEqual(ast.TypeBuiltin("uint32_t"), node.type)

    def __test_compound(self, cls):
        """
        Test compound type.

        :param cls: Compound type to test.
        """
        with self.subTest("as_named_node"):
            test_field = ast.Field(
                "test_field",
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
            )

            test_enum = ast.TypeEnum(
                "test_enum",
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                (ast.EnumValue("value0", TestAstTypes.test_description),),
            )

            self.__test_named(cls, TestAstTypes.test_description, (), (test_field,))

        with self.subTest("must_have_at_least_one_field"):
            self.assertRaises(
                ValueError,
                cls,
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                (),
                (),
            )

        with self.subTest("fields_must_be_unique"):
            self.assertRaises(
                ValueError,
                cls,
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                (),
                (test_field, test_field),
            )

        with self.subTest("scope_item_names_must_be_unique"):
            self.assertRaises(
                ValueError,
                cls,
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                (test_enum, test_enum),
                (test_field,),
            )

        with self.subTest("field_name_must_differ_from_the_type_name"):
            bad_field = ast.Field(
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
            )
            self.assertRaises(
                ValueError,
                cls,
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                (),
                (bad_field,),
            )

        with self.subTest("inner_type_name_must_differ_from_the_type_name"):
            bad_item = ast.TypeEnum(
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                (ast.EnumValue("value0", TestAstTypes.test_description),),
            )
            self.assertRaises(
                ValueError,
                cls,
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                (bad_item,),
                (test_field,),
            )

        with self.subTest("valid_value"):
            test_enum = ast.TypeEnum(
                "test_enum",
                TestAstTypes.test_description,
                ast.TypeBuiltin("uint32_t"),
                (ast.EnumValue("value0", TestAstTypes.test_description),),
            )
            node = cls(
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                (test_enum,),
                (test_field,),
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)
            self.assertEqual((test_enum,), node.items)
            self.assertEqual((test_field,), node.fields)

            self.assertEqual(node.items[0].parent, node)
            self.assertEqual(node.fields[0].parent, node)

    def test_struct(self):
        "Test `ast.TypeStruct` type."
        self.__test_compound(ast.TypeStruct)

    def test_union(self):
        "Test `ast.TypeUnion` type."
        self.__test_compound(ast.TypeUnion)

    def test_ioctl_command(self):
        "Test `ast.IoctlCommand` type."
        with self.subTest("as_named_node"):
            self.__test_named(
                ast.IoctlCommand,
                TestAstTypes.test_description,
                ast.Number(42),
                "_IOW",
                ast.TypeBuiltin("uint32_t"),
            )

        with self.subTest("valid_value"):
            node = ast.IoctlCommand(
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.Number(42),
                "_IOW",
                ast.TypeBuiltin("uint32_t"),
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)
            self.assertEqual(ast.Number(42), node.number)
            self.assertEqual("_IOW", node.command_type)
            self.assertEqual(ast.TypeBuiltin("uint32_t"), node.arg_type)

    def test_ioctl_iface(self):
        "Test `ast.IoctlIface` type."
        test_value = ast.EnumValue("test_value", TestAstTypes.test_description)
        test_enum = ast.TypeEnum(
            "test_enum",
            TestAstTypes.test_description,
            ast.TypeBuiltin("uint32_t"),
            (test_value,),
        )
        test_command = ast.IoctlCommand(
            "test_command",
            TestAstTypes.test_description,
            ast.Number(42),
            "_IOW",
            ast.TypeBuiltin("uint32_t"),
        )

        test_fwd_dcl = ast.TypeFwdDcl(
            "test_fwddcl", TestAstTypes.test_description, "struct"
        )

        test_namespace = ast.Namespace(
            "test_namespace", TestAstTypes.test_description, (test_fwd_dcl,)
        )

        with self.subTest("as_named_node"):
            self.__test_named(
                ast.IoctlIface,
                TestAstTypes.test_description,
                ast.Number(42),
                (test_enum,),
                (test_command,),
                (test_namespace,),
                "some_code",
            )

        with self.subTest("valid_value"):
            node = ast.IoctlIface(
                TestAstTypes.test_name,
                TestAstTypes.test_description,
                ast.Number(42),
                (test_enum,),
                (test_command,),
                (test_namespace,),
                "some_code",
            )

            self.assertEqual(TestAstTypes.test_name, node.name)
            self.assertEqual(TestAstTypes.test_description, node.description)
            self.assertEqual(ast.Number(42), node.number)
            self.assertEqual((test_enum,), node.items)
            self.assertEqual((test_command,), node.commands)
            self.assertEqual((test_namespace,), node.namespaces)

            self.assertEqual(node.items[0].parent, node)
            self.assertEqual(node.commands[0].parent, node)
            self.assertEqual(node.namespaces[0].parent, node)
