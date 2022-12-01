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

"Unit tests for `ioctl_idlc.loader_dataclass.Loader`."

from __future__ import annotations

import unittest

from dataclasses import dataclass
import typing

import yaml
from parameterized import parameterized

from ioctl_idlc.loader_dataclass import Loader


@dataclass
class TrivialBool:
    "Trivial dataclass with a bool field."
    field0: bool


@dataclass
class TrivialInt:
    "Trivial dataclass with an int field."
    field0: int


@dataclass
class TrivialStr:
    "Trivial dataclass with an str field."
    filed0: str


UnionOfTrivial = typing.Union[TrivialBool, TrivialInt, TrivialStr]


@dataclass
class TrivialAll:
    "dataclass with trivial fields."
    field0: bool
    field1: int
    field2: str


@dataclass
class TrivialCompound:
    "Comound dataclass with trivial dataclass fields."
    field0: TrivialBool
    field1: TrivialInt
    field2: TrivialStr


@dataclass
class TrivialUnion:
    "dataclass with union fields of trivial types."
    field0: typing.Union[bool, int]
    field1: typing.Union[int, str]


@dataclass
class DefaultValues:
    "Dataclass with default fields."
    field0: bool = False
    field1: int = 42
    field2: str = "abcd"


@dataclass
class OptionalValues:
    "Dataclass with optional fields."
    field0: typing.Optional[bool] = None
    field1: typing.Optional[int] = None
    field2: typing.Optional[str] = None
    field3: typing.Optional[TrivialAll] = None


@dataclass
class TupleTrivial:
    "Dataclass with a tuple of trivial types."
    field0: int
    field1: typing.Tuple[int, ...]


@dataclass
class TupleCompound:
    "Dataclass with a tuple of dataclass instances."
    field0: int
    field1: typing.Tuple[TrivialAll, ...]


@dataclass
class TupleUnionTrivial:
    "Dataclass with a tuple of union of trivial types."
    field0: int
    field1: typing.Tuple[typing.Union[int, str], ...]


class TestLoaderDataclass(unittest.TestCase):
    "Unit tests for `ioctl_idlc.loader_dataclass.Loader`."

    @parameterized.expand(
        [
            ("trivial_bool", TrivialBool(False), "!test-type false"),
            ("trivial_int", TrivialInt(42), "!test-type 42"),
            ("trivial_str", TrivialStr("abcd"), "!test-type abcd"),
            (
                "trivial_all",
                TrivialAll(False, 42, "abcd"),
                """!test-type
            field0: false
            field1: 42
            field2: abcd
            """,
            ),
            (
                "trivial_compound",
                TrivialCompound(TrivialBool(False), TrivialInt(42), TrivialStr("abcd")),
                """!test-type
            field0: false
            field1: 42
            field2: abcd
            """,
            ),
            (
                "trivial_union",
                TrivialUnion(False, "xyz"),
                "!test-type { field0: false, field1: xyz }",
            ),
            (
                "default_all_set",
                DefaultValues(),
                """!test-type
            field0: false
            field1: 42
            field2: abcd
            """,
            ),
            (
                "default_all_set_but_one",
                DefaultValues(),
                """!test-type
            field0: false
            field1: 42
            """,
            ),
            (
                "default_one_set",
                DefaultValues(),
                """!test-type
            field0: false
            """,
            ),
            ("default_one_set_short", DefaultValues(), "!test-type false"),
            ("default_none_set", DefaultValues(), "!test-type"),
            (
                "default_last_field_set",
                DefaultValues(field2="xyz"),
                "!test-type { field2: xyz }",
            ),
            # Optional will go here
            # Tuple
            (
                "tuple_trivial_empty",
                TupleTrivial(42, tuple()),
                """!test-type
            field0: 42
            field1: []
            """,
            ),
            (
                "tuple_trivial_non_empty",
                TupleTrivial(42, (5, 4, 3, 2, 1)),
                """!test-type
            field0: 42
            field1: [5, 4, 3, 2, 1]
            """,
            ),
            (
                "tuple_compound_empty",
                TupleCompound(42, tuple()),
                """!test-type
            field0: 42
            field1: []
            """,
            ),
            (
                "tuple_compound_non_empty",
                TupleCompound(
                    42,
                    (
                        TrivialAll(False, 43, "qwerty"),
                        TrivialAll(True, 44, "xyz"),
                    ),
                ),
                """!test-type
            field0: 42
            field1:
             - field0: false
               field1: 43
               field2: qwerty
             - field0: true
               field1: 44
               field2: xyz
            """,
            ),
            (
                "tuple_union_trivial_empty",
                TupleUnionTrivial(42, tuple()),
                "!test-type { field0: 42, field1: [] }",
            ),
            (
                "tuple_union_trivial_non_empty",
                TupleUnionTrivial(42, (42, "abcd", 43)),
                "!test-type { field0: 42, field1: [42, abcd, 43] }",
            ),
        ]
    )
    def test_all(self, _test_name, data, yaml_str):
        """
        Test dataclasses parsing.

        :param _test_name: Test name.
        :param data: Expected parsing result.
        :param yaml_str: yaml string to parse.
        """

        # pylint: disable=too-many-ancestors
        # pilint: disable=too-few-public-methods
        class DataLoader(Loader):
            "Test loader."

        DataLoader.register_type(type(data), "!test-type")
        parsed_data = yaml.load(yaml_str, DataLoader)

        self.assertEqual(data, parsed_data)

    def test_union(self):
        "Test dataclasses with a union field parsing."

        @dataclass
        class UnionField:
            "Test class with a tuple of unions."
            field0: int
            field1: UnionOfTrivial
            field2: UnionOfTrivial
            field3: typing.Tuple[UnionOfTrivial, ...]

        # pylint: disable=too-many-ancestors
        # pilint: disable=too-few-public-methods
        class DataLoader(Loader):
            "Test loader."

        DataLoader.register_type(UnionField, "!test-type")
        DataLoader.register_type(TrivialInt, "!trivial-int")
        DataLoader.register_type(TrivialStr, "!trivial-str")

        yaml_str = """!test-type
        field0: 42
        field1: !trivial-int 43
        field2: !trivial-str abcd
        field3:
          - !trivial-int 44
          - !trivial-str xyz
        """

        expected = UnionField(
            42, TrivialInt(43), TrivialStr("abcd"), (TrivialInt(44), TrivialStr("xyz"))
        )

        parsed_data = yaml.load(yaml_str, DataLoader)

        self.assertEqual(expected, parsed_data)

    def test_reuse_node(self):
        "Check that node is re-used when anchor is referenced."

        # pylint: disable=too-many-ancestors
        # pilint: disable=too-few-public-methods
        class DataLoader(Loader):
            "Test loader."

        DataLoader.register_type(TrivialStr, "!test-type")

        yaml_str = """
        field_one: &abcd !test-type abcd
        field_two: *abcd
        """

        parsed_data = yaml.load(yaml_str, DataLoader)
        self.assertIs(parsed_data["field_one"], parsed_data["field_two"])
