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

"Node description helpers."

from __future__ import annotations

from typing import Tuple
from dataclasses import dataclass

import textwrap
import itertools


_MAX_LINE_LENGTH = 80
"Maximum length of a description line."


@dataclass
class DescriptionLine:
    """
    Description line.

    :param content: Line's content.
    """

    content: str


@dataclass
class Description:
    "Description of a node."

    description: str

    def __str__(self):
        return self.description

    @property
    def is_multiline(self) -> bool:
        ":return: True, if it is a multiline description, False otherwise."
        return len(self.lines) > 1

    @property
    def lines(self) -> Tuple[DescriptionLine, ...]:
        ":return: Description split by lines."
        if not self.description:
            return tuple()

        lines = self.description.split("\n")

        lines_wrapped = []
        for line in lines:
            wrapped = textwrap.wrap(
                line,
                _MAX_LINE_LENGTH,
                break_long_words=False,
                expand_tabs=False,
                replace_whitespace=False,
            )

            # When wrapping an empty string, an empty list is returned.
            # But we want to preserve empty lines in the final result.
            if not wrapped:
                lines_wrapped.append([""])
                continue

            lines_wrapped.append(wrapped)

        lines_flat = itertools.chain(*lines_wrapped)

        return tuple((DescriptionLine(line) for line in lines_flat))
