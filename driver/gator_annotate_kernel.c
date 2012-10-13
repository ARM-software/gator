/**
 * Copyright (C) ARM Limited 2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define ESCAPE_CODE 0x1c
#define STRING_ANNOTATION 0x03
#define VISUAL_ANNOTATION 0x04
#define MARKER_ANNOTATION 0x05

static void kannotate_write(const char* ptr, unsigned int size)
{
	int retval;
	int pos = 0;
	loff_t offset = 0;
	while (pos < size) {
		retval = annotate_write(NULL, &ptr[pos], size - pos, &offset);
		if (retval < 0) {
			printk(KERN_WARNING "gator: kannotate_write failed with return value %d\n", retval);
			return;
		}
		pos += retval;
	}
}

static void gator_annotate_code(char code)
{
	int header = ESCAPE_CODE | (code << 8);
	kannotate_write((char*)&header, sizeof(header));
}

static void gator_annotate_code_str(char code, const char* string)
{
	int str_size = strlen(string) & 0xffff;
	int header = ESCAPE_CODE | (code << 8) | (str_size << 16);
	kannotate_write((char*)&header, sizeof(header));
	kannotate_write(string, str_size);
}

static void gator_annotate_code_color(char code, int color)
{
	long long header = (ESCAPE_CODE | (code << 8) | 0x00040000 | ((long long)color << 32));
	kannotate_write((char*)&header, sizeof(header));
}

static void gator_annotate_code_color_str(char code, int color, const char* string)
{
	int str_size = (strlen(string) + 4) & 0xffff;
	long long header = ESCAPE_CODE | (code << 8) | (str_size << 16) | ((long long)color << 32);
	kannotate_write((char*)&header, sizeof(header));
	kannotate_write(string, str_size - 4);
}

// String annotation
void gator_annotate(const char* string)
{
	gator_annotate_code_str(STRING_ANNOTATION, string);
}
EXPORT_SYMBOL(gator_annotate);

// String annotation with color
void gator_annotate_color(int color, const char* string)
{
	gator_annotate_code_color_str(STRING_ANNOTATION, color, string);
}
EXPORT_SYMBOL(gator_annotate_color);

// Terminate an annotation
void gator_annotate_end(void)
{
	gator_annotate_code(STRING_ANNOTATION);
}
EXPORT_SYMBOL(gator_annotate_end);

// Image annotation with optional string
void gator_annotate_visual(const char* data, unsigned int length, const char* string)
{
	int str_size = strlen(string) & 0xffff;
	int visual_annotation = ESCAPE_CODE | (VISUAL_ANNOTATION << 8) | (str_size << 16);
	kannotate_write((char*)&visual_annotation, sizeof(visual_annotation));
	kannotate_write(string, str_size);
	kannotate_write((char*)&length, sizeof(length));
	kannotate_write(data, length);
}
EXPORT_SYMBOL(gator_annotate_visual);

// Marker annotation
void gator_annotate_marker(void)
{
	gator_annotate_code(MARKER_ANNOTATION);
}
EXPORT_SYMBOL(gator_annotate_marker);

// Marker annotation with a string
void gator_annotate_marker_str(const char* string)
{
	gator_annotate_code_str(MARKER_ANNOTATION, string);
}
EXPORT_SYMBOL(gator_annotate_marker_str);

// Marker annotation with a color
void gator_annotate_marker_color(int color)
{
	gator_annotate_code_color(MARKER_ANNOTATION, color);
}
EXPORT_SYMBOL(gator_annotate_marker_color);

// Marker annotation with a string and color
void gator_annotate_marker_color_str(int color, const char* string)
{
	gator_annotate_code_color_str(MARKER_ANNOTATION, color, string);
}
EXPORT_SYMBOL(gator_annotate_marker_color_str);
