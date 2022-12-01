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

"Unit tests for `ioctl_idlc.description.Description`."

from __future__ import annotations

import unittest

from parameterized import parameterized

from ioctl_idlc.description import Description, DescriptionLine


class TestDescription(unittest.TestCase):
    "Unit tests for `ioctl_idlc.description.Description`."

    @parameterized.expand(
        [
            ("empty", "", tuple()),
            ("one_line", "Line.", (DescriptionLine("Line."),)),
            (
                "two_lines",
                "Line 1.\nLine 2.",
                (DescriptionLine("Line 1."), DescriptionLine("Line 2.")),
            ),
            ("keep_tabs", "Word\tword", (DescriptionLine("Word\tword"),)),
            ("keep_whitespace", "Word   word", (DescriptionLine("Word   word"),)),
            (
                "empty_line",
                "Line 1.\n\nLine 2.",
                (
                    DescriptionLine("Line 1."),
                    DescriptionLine(""),
                    DescriptionLine("Line 2."),
                ),
            ),
            ("long_line", "Longword" * 50, (DescriptionLine("Longword" * 50),)),
        ]
    )
    def test_all(self, _test_name, text, expected_lines):
        """
        Test for all parameters.

        :param _test_name: Test name.
        :param text: Description text.
        :param expected_lines: Expected description lines.
        """
        description = Description(text)

        expected_is_multiline = len(expected_lines) > 1

        self.assertEqual(expected_is_multiline, description.is_multiline)
        self.assertEqual(expected_lines, description.lines)
