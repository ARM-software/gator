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

#include <workload/workload.hpp>

#include <iostream>

#include <getopt.h>

static uint32_t nr_frame = 20;
static bool dump_image;

static const char help_msg[] = "\noptional arguments:\n"
                               "  -h, --help                    show this help message and exit\n"
                               "  -d, --dump-img                dump each frame in image files\n"
                               "  -f NUMBER, --frame NUMBER     set NUMBER of frames to draw (default: 20)\n";

void opt_parsing(int argc, char *argv[]) {
    while (true) {
        static struct option long_options[] = {{"help", no_argument, nullptr, 'h'},
                                               {"dump-img", no_argument, nullptr, 'd'},
                                               {"frame", required_argument, nullptr, 'f'},
                                               {nullptr, 0, nullptr, 0}};

        int key = getopt_long(argc, argv, "hdf:", long_options, nullptr);

        if (key == -1)
            break;

        switch (key) {
        case 'd':
            dump_image = true;
            break;
        case 'f':
            nr_frame = std::stoul(optarg);
            break;
        case 'h':
        default:
            std::cout << help_msg << std::endl;
            std::exit(-1);
        }
    }
}

int main(int argc, char *argv[]) {
    opt_parsing(argc, argv);

    auto w = workload::create();
    w->set_dump_image(dump_image);

    w->start_async(nr_frame);
    w->wait_async_complete();

    return 0;
}
