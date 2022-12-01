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

#pragma once

#include <array>
#include <cstddef>

using vec4 = std::array<float, 4>;
using mat4 = std::array<vec4, 4>;

static inline vec4 operator*(const vec4 &v1, const vec4 &v2) {
    return {v1[0] * v2[0], v1[1] * v2[1], v1[2] * v2[2], v1[3] * v2[3]};
}

static inline vec4 operator+(const vec4 &v1, const vec4 &v2) {
    return {v1[0] + v2[0], v1[1] + v2[1], v1[2] + v2[2], v1[3] + v2[3]};
}

static inline vec4 operator*(const vec4 &v, float scalar) {
    return {v[0] * scalar, v[1] * scalar, v[2] * scalar, v[3] * scalar};
}

static inline mat4 operator*(const mat4 &m1, const mat4 &m2) {
    mat4 result;

    // column major matrix multiplication
    for (size_t c = 0; c < m1[0].size(); c++) {
        for (size_t r = 0; r < m2.size(); r++) {
            float sum = 0.0f;
            for (size_t i = 0; i < m2.size(); i++)
                sum += m1[i][r] * m2[c][i];
            result[c][r] = sum;
        }
    }
    return result;
}

/**
 * Convert degree to randian value.
 *
 * @param[in] degree Input degree.
 * @return Converted radian value.
 */
static inline float radians(float degree) { return degree * 0.017453f; }

/**
 * Create a translation matrix.
 *
 * @param[in] m Input matrix.
 * @param[in] v Input vector of 4 components, but the last element is ignored.
 * @return Translation matrix.
 */
mat4 translate(const mat4 &m, const vec4 &v);

/**
 * Create a rotation matrix in Y axis.
 *
 * @param[in] m Input matrix.
 * @param[in] rad Radian value for rotation.
 * @return Rotation matrix.
 */
mat4 rotate_y(const mat4 &m, float rad);

/**
 * Create a frustum matrix.
 *
 * @param[in] left Left position.
 * @param[in] right Right position.
 * @param[in] bottom Bottom position.
 * @param[in] top Top position.
 * @param[in] near Near position.
 * @param[in] far Far position.
 * @return Frustum matrix based on the input values.
 */
mat4 frustum(float left, float right, float bottom, float top, float near, float far);

/**
 * Create a perspective matrix.
 *
 * @param[in] fovrad Field of view in radians.
 * @param[in] aspect Aspect ratio.
 * @param[in] near Near position.
 * @param[in] far Far position.
 * @return Perspectived matrix based on the input values.
 */
mat4 perspective(float fovrad, float aspect, float near, float far);
