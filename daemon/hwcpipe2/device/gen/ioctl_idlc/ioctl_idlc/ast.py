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

"Abstract syntax tree types."

from __future__ import annotations

import typing
from dataclasses import field, dataclass

from .description import Description
from . import validate


class Introspect:
    "Helper class to query node type."

    @property
    def is_constant(self):
        ":return: True, if it is a `Constant` instance."
        return isinstance(self, Constant)

    @property
    def is_builtin(self):
        ":return: True, if it is a `TypeBuiltin` type"
        return isinstance(self, TypeBuiltin)

    @property
    def is_anonimous(self):
        ":return: True, if it is an `TypeAnonimous` type."
        return isinstance(self, TypeAnonimous)

    @property
    def is_array(self):
        ":return: True, if it is an `TypeArray` type."
        return isinstance(self, TypeArray)

    @property
    def is_pointer64(self):
        ":return: True, if it is an `TypePointer64` type."
        return isinstance(self, TypePointer64)

    @property
    def is_offset_pointer(self):
        ":return: True, if it is an `TypeOffsetPointer` type."
        return isinstance(self, TypeOffsetPointer)

    @property
    def is_enum(self):
        ":return: True, if it is a `TypeEnum` type."
        return isinstance(self, TypeEnum)

    @property
    def is_bitmask(self):
        ":return: True, if it is a `TypeBitmask` type."
        return isinstance(self, TypeBitmask)

    @property
    def is_enum_or_bitmask(self):
        ":return: True, if it is a `TypeEnum` or a `TypeBitmask` type."
        return self.is_enum or self.is_bitmask

    @property
    def is_struct(self):
        ":return: True, if it is a `TypeStruct` type."
        return isinstance(self, TypeStruct)

    @property
    def is_union(self):
        ":return: True, if it is a `TypeUnion` type."
        return isinstance(self, TypeUnion)

    @property
    def is_compound(self):
        ":return: True, if it is a `TypeCompound` type."
        return isinstance(self, TypeCompound)


class Node(Introspect):
    "Base class for all nodes."


@dataclass
class Named(Node):
    """
    A node with a name and a description.

    :param name: Node name. Must be a valid identifier.
    :param description: Node description.
    """

    # Parent node of this node. None for the root node.
    parent: typing.Optional["Named"] = field(
        default=None, init=False, compare=False, repr=False
    )
    name: str
    description: Description

    def __post_init__(self):
        validate.identifier(self.name)
        validate.not_known_buitin_type(self.name)

    def _set_parent_and_validate(self, collection: typing.Iterable[Named]) -> None:
        validate.uniq_names(collection)
        validate.name_not_in(self.name, collection)

        for item in collection:
            item.parent = self

    @property
    def full_name_cxx(self) -> str:
        ":return: Full name of this item for C++ language."
        return "::".join(self.__parents())

    @property
    def full_name_c(self) -> str:
        ":return: Full name of this item for C language."
        return "_".join(self.__parents())

    def __parents(self) -> typing.Iterable[str]:
        "return: All parents of this node up to the root node."
        if self.parent and self.parent.parent:
            # pylint: disable=protected-access
            yield from self.parent.__parents()

        yield self.name


_Type = typing.Union[
    "TypeArray",
    "TypeBitmask",
    "TypeBuiltin",
    "TypeCompound",
    "TypeEnum",
    "TypeOffsetPointer",
    "TypePointer64",
]


@dataclass
class TypeBuiltin(Node):
    """
    Built-in type node.

    :param name: Type name. Must be a valid built-in type.
    """

    name: str

    def __post_init__(self):
        validate.known_builtin_type(self.name)

    @property
    def full_name_cxx(self) -> str:
        ":return: Full name of this item for C++ language."
        return self.name

    @property
    def full_name_c(self) -> str:
        ":return: Full name of this item for C language."
        return self.name

    @property
    def print_as(self) -> typing.Optional[str]:
        """
        Type to cast to before printing.

        C++ ostream prints uint8_t and int8_t as chars. To avoid that,
        we cast them conditionally to a larger type, so they appear as
        numbers.

        :return: Type to cast to before printing. None if such cast is not needed.
        """

        print_as_dict = {
            "uint8_t": "uint32_t",
            "int8_t": "int32_t",
        }
        return print_as_dict.get(self.name, None)


_ScopeItemUnion = typing.Union[
    "Constant", "TypeEnum", "TypeBitmask", "TypeStruct", "TypeUnion"
]


@dataclass
class EnumValue(Named):
    """
    Enum value node.

    :param value: Enum's value.
    :param alias: If True, this value is an alias for some other value.
    """

    value: typing.Optional[str] = None
    alias: bool = False


@dataclass
class TypeEnum(Named):
    """
    Enum type node.

    :param underlying_type: Underlying type of the enum.
    :param values: Pared enum values.
    """

    underlying_type: TypeBuiltin
    values: typing.Tuple[EnumValue, ...]
    keyword: typing.ClassVar[str] = "enum"

    def __post_init__(self):
        super().__post_init__()

        if len(self.values) < 1:
            raise ValueError("A enum should contain at least one value.")

        self._set_parent_and_validate(self.values)


@dataclass
class BitmaskValue(Named):
    """
    Bitmask value node.

    :param bit_number: The bit number.
    """

    bit_number: int


@dataclass
class TypeBitmask(Named):
    """
    Bitmask type node.

    :param underlying_type: Underlying type of the bitmask.
    :param values: Pared bitmask values.
    """

    underlying_type: TypeBuiltin
    values: typing.Tuple[BitmaskValue, ...]
    keyword: typing.ClassVar[str] = "enum"

    def __post_init__(self):
        super().__post_init__()

        if len(self.values) < 1:
            raise ValueError("A bitmask should contain at least one value.")

        self._set_parent_and_validate(self.values)


@dataclass
class Constant(Named):
    """
    Constant AST node.

    :param type: Type of the constant.
    :param value: Constant value.
    """

    type: TypeBuiltin
    value: str


class TypeAnonimous(Node):
    "Type defined inline with no name and no description."


@dataclass
class TypeArray(TypeAnonimous):
    """
    Array type AST node.

    :param element_type: Array's element type.
    :param array_size: Size of the array.
    """

    element_type: TypeBuiltin
    array_size: str


@dataclass
class FieldMeta(Node):
    """
    Field metadata.

    :param padding: True if it is a padding field.
    """

    padding: bool = False


@dataclass
class Field(Named):
    """
    Compound type field AST node.

    :param type: Parsed type of the field.
    :param type: Field metadata.
    """

    type: _Type
    meta: typing.Optional[FieldMeta] = None

    def __post_init__(self):
        super().__post_init__()

        if isinstance(self.type, (TypeArray, TypePointer64, TypeOffsetPointer)):
            return

        if self.name == self.type.name:
            raise ValueError(f"Field name {self.name} is the same as type name.")


@dataclass
class TypeCompound(Named):
    """
    Compound type node.

    :param items: Collection of inner types and constants.
    :param fields: Compound type fields.
    """

    items: typing.Tuple[_ScopeItemUnion, ...] = field(default_factory=tuple)
    fields: typing.Tuple[Field, ...] = field(default_factory=tuple)

    def __post_init__(self):
        super().__post_init__()

        if not self.fields:
            raise ValueError(f"{self.name} must have at least one field.")

        self._set_parent_and_validate(self.items)
        self._set_parent_and_validate(self.fields)

    @property
    def has_union_field(self) -> bool:
        ":return: True if a type has union field, False otherwise."
        for type_field in self.fields:
            if isinstance(type_field.type, TypeUnion):
                return True
        return False


@dataclass
class TypeStruct(TypeCompound):
    "Struct type node."

    keyword: typing.ClassVar[str] = "struct"


@dataclass
class TypeUnion(TypeCompound):
    "Union type node."

    keyword: typing.ClassVar[str] = "union"


@dataclass
class TypeFwdDcl(Named):
    """
    Forward type declaration node.

    :param keyword: 'struct' or 'union'.
    """

    keyword: str

    def __post_init__(self):
        super().__post_init__()

        validate.known_keyword(self.keyword)


@dataclass
class TypePointer64(TypeAnonimous):
    """
    Pointer64 type node.

    :param value_type: Value type of the pointer.
    :param const: True if it is a const pointer, False otherwise.
    """

    value_type: typing.Union[TypeFwdDcl, _Type]
    const: bool = False


@dataclass
class TypeOffsetPointer(TypeAnonimous):
    """
    Offset pointer type node.

    :param value_type: Value type of the pointer.
    :param representation_type: Type used to store the offset value.
    :param const: True if it is a const pointer, False otherwise.
    """

    value_type: typing.Union[TypeFwdDcl, _Type]
    representation_type: TypeBuiltin
    const: bool = False


@dataclass
class Namespace(Named):
    """
    Namespace node.

    External namespace node is a collection of forward type declarations.
    :param items: Forward declared types.
    """

    forward_declarations: typing.Tuple[TypeFwdDcl, ...]

    def __post_init__(self):
        super().__post_init__()

        self._set_parent_and_validate(self.forward_declarations)


@dataclass
class Number:
    """
    Interface or command number.

    :param number: Number field.
    """

    number: int

    def __str__(self):
        return str(self.number)

    @property
    def hex(self) -> str:
        ":return: number formatted as hex."

        return hex(self.number)


@dataclass
class IoctlCommand(Named):
    """
    ioctl command AST node.

    :param number: Command number.
    :param arg_type: Argument type of the command.
    """

    number: Number
    command_type: str
    arg_type: _Type


@dataclass
class IoctlIface(Named):
    """
    ioctl interface AST node.

    :param number: Interface number.
    :param comands: Interface command nodes.
    """

    number: Number
    items: typing.Tuple[_ScopeItemUnion, ...]
    commands: typing.Tuple[IoctlCommand, ...]
    namespaces: typing.Tuple[Namespace, ...] = field(default_factory=tuple)
    code: str = ""

    def __post_init__(self):
        super().__post_init__()

        self._set_parent_and_validate(self.items)
        self._set_parent_and_validate(self.commands)
        self._set_parent_and_validate(self.namespaces)
