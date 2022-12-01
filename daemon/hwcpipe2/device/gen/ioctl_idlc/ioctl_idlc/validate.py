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

"Type validation helpers."

from __future__ import annotations

import re
from typing import Iterable, Dict, TYPE_CHECKING

if TYPE_CHECKING:
    from .ast import Named


_identifier_regex = re.compile(r"^[^\d\W]\w*\Z", re.UNICODE)


def identifier(name: str):
    """
    Check if name is a valid identifier.

    :param name: Name to check.
    :raises ValueError: if name is a valid identifier.
    """
    if not _identifier_regex.match(name):
        raise ValueError(f"{name} is not a valid identifier.")


_known_builtin_types = {
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "size_t",
}


def known_builtin_type(name: str):
    """
    Check if type name is a know built-in type.

    :param name: Name to check.
    :raise ValueError: if name is not a valid built-in type.
    """
    if name not in _known_builtin_types:
        raise ValueError(f"{name} is not a valid built-in type.")


_known_keywords = {"struct", "union", "enum"}


def known_keyword(keyword: str):
    """
    Check if a keyword is known.

    :param keyword: Keyword to check.
    :raise ValueError: If keyword is not known.
    """
    if keyword not in _known_keywords:
        raise ValueError(f"Keyword '{keyword}' is not known.")


def not_known_buitin_type(name: str):
    """
    Check if type name is not a know built-in type.

    :param name: Name to check.
    :raise ValueError: if name is a valid built-in type.
    """
    if name in _known_builtin_types:
        raise ValueError(f"{name} is not a valid built-in type.")


def uniq_names(collection: Iterable[Named]):
    """
    Check if all names are unique for a collection.

    :param collection: Collection to validate.
    :raises ValueError: if names are not unique in the collection.
    """

    name2idx: Dict[str, int] = {}

    for idx, item in enumerate(collection):
        name = item.name
        if name in name2idx:
            prev_idx = name2idx[name]
            raise ValueError(
                f"Two entries with the same name '{idx}' and '{prev_idx}'."
            )
        name2idx[name] = idx

    return collection


def name_not_in(name: str, collection: Iterable[Named]):
    """
    Check that no nodes have a given name.

    :param name: Name that must not be present in the collection.
    :param collection: The collcetion to check.
    :raises ValueError: If a node has a bad name.
    """
    for item in collection:
        if item.name == name:
            raise ValueError(
                "Inner types / fields must differ from the type name being defined."
            )
