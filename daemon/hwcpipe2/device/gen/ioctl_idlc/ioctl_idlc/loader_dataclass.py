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

"Yaml loader for dataclasses."

from dataclasses import dataclass, fields, is_dataclass
import typing

import yaml

from .forward_ref_fixup import get_type_hints


@dataclass
class _NodeCls:
    "Node and class pair."

    node: yaml.Node
    cls: type

    def __str__(self):
        return f"While constructing {self.cls} here:\n  {self.node.start_mark}"


class LoaderException(Exception):
    "Loader exception."

    def __init__(self, node: yaml.Node, cls: type):
        """
        :param node: yaml node where exception occurred.
        :param cls: type we attempted to construct, but failed.
        """
        super().__init__()
        self.parsing_stack = [_NodeCls(node, cls)]

    def add_context(self, node, cls):
        """
        Add parsing context to the parsing stack.
        """
        self.parsing_stack.append(_NodeCls(node, cls))

    def __str__(self):
        """
        :return: String representation of the exception.
        """
        return "\n".join([str(item) for item in self.parsing_stack])


def _is_union(cls) -> typing.Optional[typing.Tuple[type, ...]]:
    """
    Check if type is a Union type.

    :param cls: Type to check.
    :return: Union arguments when type is a Union, or None.
    """
    origin = typing.get_origin(cls)
    if origin is not typing.Union:
        return None

    return typing.get_args(cls)


def _is_optional(cls) -> typing.Optional[type]:
    """
    Check if type is an Optional type.

    :param cls: Type to check.
    :return: Optional argument when cls is an optional, None otherwise.
    """
    if (args := _is_union(cls)) is None:
        return None

    if len(args) != 2:
        return None

    if type(None) not in args:
        return None

    return next(arg for arg in args if not isinstance(None, arg))


def _is_monotype_tuple(cls) -> typing.Optional[type]:
    """
    Check if a type is a monotype tuple.

    :param cls: Type to check.
    :return: Tuple argument when cls is a Tuple, None otherwise.
    """
    if typing.get_origin(cls) is not tuple:
        return None

    args = typing.get_args(cls)
    if len(args) != 2:
        return None

    if args[1] is not Ellipsis:
        return None

    return args[0]


# pylint: disable=too-many-ancestors
# pilint: disable=too-few-public-methods
class Loader(yaml.SafeLoader):
    "Yaml loader for dataclasses."

    def __init__(self, stream):
        super().__init__(stream)
        # When a node is re-used with yaml anchor, we want to re-use
        # the object that corresponds to it too. The approach to that
        # is to cache mapping from the yaml nodes to the objects loaded.
        # So that if an object is present in cache, we just re-use it.
        self._node_to_object_cache = {}

    def _cache_load(self, node: yaml.Node) -> typing.Optional[typing.Any]:
        """
        Load object from cache.

        :param node: yaml node being parsed.
        :return: Object if found, None otherwise.
        """
        return self._node_to_object_cache.get(id(node), None)

    def _cache_store(self, node: yaml.Node, obj: typing.Any) -> typing.Any:
        """
        Cache object.

        :param node: yaml node being parsed.
        :param obj: Object to cache.
        :return: Object cached.
        """
        self._node_to_object_cache[id(node)] = obj
        return obj

    def _construct(self, node: yaml.Node, cls):
        """
        Construct a instance of a type.

        :param node: yaml node.
        :param cls: Type to construct.
        :return: Instance constructed.
        """
        try:
            # Avoid objects duplication when node is re-used.
            if result := self._cache_load(node):
                return result

            if cls == str:
                result = self.construct_yaml_str(node)
            elif cls == int:
                result = self.construct_yaml_int(node)
            elif cls == bool:
                result = self.construct_yaml_bool(node)
            elif item_type := _is_monotype_tuple(cls):
                result = self._construct_tuple(node, item_type)
            elif optional_type := _is_optional(cls):
                result = self._construct(node, optional_type)
            elif is_dataclass(cls):
                result = self._construct_dataclass(node, cls)
            elif union_types := _is_union(cls):
                result = self.construct_object(node)
                if not isinstance(result, union_types):
                    raise ValueError(
                        f"Unexpected type {type(result)}. Types {union_types} were expected."
                    )
            else:
                raise ValueError(f"{cls} is not supported.")

            return self._cache_store(node, result)
        except ValueError as err:
            raise LoaderException(node, cls) from err
        except LoaderException as err:
            err.add_context(node, cls)
            raise err

    def _construct_tuple(self, node, item_cls):
        """
        Construct tuple.

        :param node: yaml node.
        :param item_cls: Item's type.
        :return: Tuple constructed.
        """
        if not isinstance(node, yaml.SequenceNode):
            raise ValueError("Must be a sequence node.")

        return tuple(self._construct(item_node, item_cls) for item_node in node.value)

    def _construct_dataclass_from_scalar(self, node: yaml.ScalarNode, cls):
        """
        Construct a dataclass instance from a scalar node.

        :param node: yaml node.
        :param cls: Class to construct.
        :return: Instance constructed.
        """
        if node.value == "":
            return cls()

        cls_fields = fields(cls)

        if len(cls_fields) == 0:
            raise ValueError(f"{cls} does not have fields.")

        # Note, we cannot use cls_fields[0].type since it's a string.
        # The type has to be evaluated at run-time using typing.get_type_hints.
        field_name2type = get_type_hints(cls)
        field_type = field_name2type[cls_fields[0].name]

        arg = self._construct(node, field_type)
        return cls(arg)

    def _construct_dataclass_from_mapping(self, node: yaml.MappingNode, cls):
        """
        Construct a dataclass instance from a mapping node.

        :param node: yaml node.
        :param cls: Class to construct.
        :return: Instance constructed.
        """
        field_name2type = get_type_hints(cls)

        args = {}

        for key, value in node.value:
            key_str = self._construct(key, str)

            if key_str not in field_name2type:
                raise ValueError(f"Unexpected key {key_str}.")

            args[key_str] = self._construct(value, field_name2type[key_str])

        return cls(**args)

    def _construct_dataclass(self, node, cls):
        """
        Construct a dataclass instance.

        :param node: yaml node.
        :param cls: Class to construct.
        :return: Instance constructed.
        """

        if isinstance(node, yaml.MappingNode):
            return self._construct_dataclass_from_mapping(node, cls)

        if isinstance(node, yaml.ScalarNode):
            return self._construct_dataclass_from_scalar(node, cls)

        raise ValueError(f"Unexpected node type {type(node)}")

    @classmethod
    def register_type(cls, tag_cls: type, tag: str):
        """
        Register new type with a yaml tag.

        :param tag_cls: Type to register.
        :param tag: yaml tag to assign for this type.
        """

        def __constructor(loader, node):
            # # pylint: disable=protected-access
            return loader._construct(node, tag_cls)

        cls.add_constructor(tag, __constructor)
