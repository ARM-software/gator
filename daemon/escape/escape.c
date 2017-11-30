/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * The Makefile in the daemon folder builds and executes 'escape'
 * 'escape' creates configuration_xml.h from configuration.xml and events_xml.h from events-*.xml
 * these genereated xml files are then #included and built as part of the gatord binary
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void print_escaped_path(FILE *out, char *path)
{
    if (isdigit(*path)) {
        fprintf(out, "__");
    }
    for (; *path != '\0'; ++path) {
        fprintf(out, "%c", isalnum(*path) ? *path : '_');
    }
}

int main(int argc, char *argv[])
{
    int i;
    int help = 0;
    char * constant_name;
    char * input_path;
    char * output_path;
    FILE *in = NULL;
    FILE *out = NULL;
    int ch;
    unsigned int len = 0;

    /* Parse / validate program arguments */
    for (i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            help = 1;
            break;
        }
    }

    if (help || (argc != 4)) {
        fprintf(stderr, "Usage: %s <constant_name> <xml_input_filename> <c_output_filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    constant_name = argv[1];
    input_path = argv[2];
    output_path = argv[3];

    /* Open the input file for reading */
    errno = 0;
    if ((in = fopen(input_path, "r")) == NULL) {
        fprintf(stderr, "Unable to open '%s': %s\n", input_path, strerror(errno));
        return EXIT_FAILURE;
    }

    /* Open the output file for writing */
    errno = 0;
    if ((out = fopen(output_path, "w")) == NULL) {
        fprintf(stderr, "Unable to open '%s': %s\n", output_path, strerror(errno));
        fclose(in);
        return EXIT_FAILURE;
    }

    /* Escape the output */
    fprintf(out, "static const unsigned char ");
    print_escaped_path(out, constant_name);
    fprintf(out, "[] = {");
    for (;;) {
        ch = fgetc(in);
        if (len != 0) {
            fprintf(out, ",");
        }
        if (len % 12 == 0) {
            fprintf(out, "\n ");
        }
        // Write out a null character after the contents of the file but do not increment len
        fprintf(out, " 0x%.2x", (ch == EOF ? 0 : ch));
        if (ch == EOF) {
            break;
        }
        ++len;
    }
    fprintf(out, "\n};\nstatic const unsigned int ");
    print_escaped_path(out, constant_name);
    fprintf(out, "_len = %i;\n", len);

    fclose(in);
    fclose(out);

    return EXIT_SUCCESS;
}
