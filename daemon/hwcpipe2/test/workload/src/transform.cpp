/*
 * Copyright (c) 2022 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "transform.hpp"

#include <cmath>

mat4 translate(const mat4 &m, const vec4 &v) {
    mat4 result{m};
    result[3] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
    return result;
}

mat4 rotate_y(const mat4 &m, float rad) {
    mat4 rotate = {
        vec4{cosf(rad), 0.0, -sinf(rad), 0.0},
        vec4{0.0, 1.0, 0.0, 0.0},
        vec4{sinf(rad), 0.0, cosf(rad), 0.0},
        vec4{0.0, 0.0, 0.0, 1.0},
    };

    mat4 result{};
    result[0] = m[0] * rotate[0][0] + m[1] * rotate[0][1] + m[2] * rotate[0][2];
    result[1] = m[0] * rotate[1][0] + m[1] * rotate[1][1] + m[2] * rotate[1][2];
    result[2] = m[0] * rotate[2][0] + m[1] * rotate[2][1] + m[2] * rotate[2][2];
    result[3] = m[3];

    return result;
}

mat4 frustum(float left, float right, float bottom, float top, float near, float far) {
    float rpl = right + left;
    float rml = right - left;
    float tpb = top + bottom;
    float tmb = top - bottom;
    float fpn = far + near;
    float fmn = far - near;

    mat4 result = {
        vec4{2.0f * near / rml, 0.0f, 0.0f, 0.0f},
        vec4{0.0f, 2.0f * near / tmb, 0.0f, 0.0f},
        vec4{rpl / rml, tpb / tmb, -(fpn / fmn), -1.0f},
        vec4{0.0f, 0.0f, -(2 * far * near / fmn), 0.0f},
    };
    return result;
}

mat4 perspective(float fovrad, float aspect, float near, float far) {
    float bottom = -near * tanf(fovrad / 2);
    float left = bottom * aspect;
    float top = near * tanf(fovrad / 2);
    float right = top * aspect;

    return frustum(left, right, bottom, top, near, far);
}
