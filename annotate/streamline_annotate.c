/**
 * Copyright (c) 2014-2015, ARM Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "streamline_annotate.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* The pthreads library is required to setup a post fork callback. If no annotations are emitted after a fork and before an exec (if any) then USE_PTHREADS can be set to 0 to disable the callback. */
#define USE_PTHREADS 1

#define ARRAY_SIZE(A) (sizeof(A)/sizeof((A)[0]))

#if USE_PTHREADS

#include <pthread.h>

#define ANNOTATE_DECL \
	static void gator_annotate_destructor(void *value) \
	{ \
		close((intptr_t)(value) - 1); \
	} \
 \
	static pthread_key_t gator_annotate_key
#define ANNOTATE_CLEAR_FD() pthread_setspecific(gator_annotate_key, NULL)
/* Store fd + 1 so that the default value NULL maps to -1 */
#define ANNOTATE_SAVE_FD(fd) \
	if (pthread_setspecific(gator_annotate_key, (void *)(intptr_t)(fd + 1)) != 0) { \
		goto fail_close_fd; \
	}
#define ANNOTATE_GET_FD(fd) fd = (intptr_t)(pthread_getspecific(gator_annotate_key)) - 1
#define ANNOTATE_INIT_FD() \
	if (pthread_key_create(&gator_annotate_key, gator_annotate_destructor) != 0) { \
		return; \
	}

#else /* USE_PTHREADS */

#define ANNOTATE_DECL static int gator_annotate_fd
#define ANNOTATE_CLEAR_FD() gator_annotate_fd = -1
#define ANNOTATE_SAVE_FD(fd) gator_annotate_fd = fd
#define ANNOTATE_GET_FD(fd) fd = gator_annotate_fd
#define ANNOTATE_INIT_FD() gator_annotate_fd = -1

#endif /* USE_PTHREADS */

struct buffer {
	uint8_t data[1<<12];
	size_t length;
};

struct counter {
	struct counter *next;
	const char *title;
	const char *name;
	const char *units;
	const char *description;
	const char **activities;
	uint32_t *activity_colors;
	size_t activity_count;
	int per_cpu;
	int average_selection;
	int average_cores;
	int percentage;
	enum gator_annotate_counter_class counter_class;
	enum gator_annotate_display display;
	enum gator_annotate_series_composition series_composition;
	enum gator_annotate_rendering_type rendering_type;
	uint32_t id;
	uint32_t modifier;
	uint32_t cores;
	uint32_t color;
};

struct cam_track {
	struct cam_track *next;
	const char *name;
	uint32_t view_uid;
	uint32_t track_uid;
	uint32_t parent_track;
};

struct cam_name {
	struct cam_name *next;
	const char *name;
	uint32_t view_uid;
};

static const char STREAMLINE_ANNOTATE_PARENT[] = "\0streamline-annotate-parent";
static const char STREAMLINE_ANNOTATE[] = "\0streamline-annotate";

static const char gator_annotate_handshake[] = "ANNOTATE 3\n";
static const int gator_minimum_version = 22;

static const uint8_t HEADER_UTF8            = 0x01;
static const uint8_t HEADER_UTF8_COLOR      = 0x02;
static const uint8_t HEADER_CHANNEL_NAME    = 0x03;
static const uint8_t HEADER_GROUP_NAME      = 0x04;
static const uint8_t HEADER_VISUAL          = 0x05;
static const uint8_t HEADER_MARKER          = 0x06;
static const uint8_t HEADER_MARKER_COLOR    = 0x07;
static const uint8_t HEADER_COUNTER         = 0x08;
static const uint8_t HEADER_COUNTER_VALUE   = 0x09;
static const uint8_t HEADER_ACTIVITY_SWITCH = 0x0a;
static const uint8_t HEADER_CAM_TRACK       = 0x0b;
static const uint8_t HEADER_CAM_JOB         = 0x0c;
static const uint8_t HEADER_CAM_VIEW_NAME   = 0x0d;

#define SIZE_COLOR         4
#define MAXSIZE_PACK_INT   5
#define MAXSIZE_PACK_LONG 10

ANNOTATE_DECL;
static int gator_annotate_parent_fd;

static struct {
	uint32_t initialized : 1,
		connected : 1,
		reserved_1 : 30;
} gator_annotate_state;

static struct counter *gator_annotate_counters;
static struct cam_track *gator_cam_tracks;
static struct cam_name *gator_cam_names;
/* Intentionally exported */
uint8_t gator_dont_mangle_keys;

static void gator_annotate_marshal_uint32(uint8_t *const buf, const uint32_t val)
{
	buf[0] = val & 0xff;
	buf[1] = (val >> 8) & 0xff;
	buf[2] = (val >> 16) & 0xff;
	buf[3] = (val >> 24) & 0xff;
}

static int gator_annotate_pack_int(uint8_t *const buf, int32_t val)
{
	int packed_bytes = 0;
	int more = 1;
	while (more) {
		/* low order 7 bits of val */
		char b = val & 0x7f;
		val >>= 7;

		if ((val == 0 && (b & 0x40) == 0) || (val == -1 && (b & 0x40) != 0)) {
			more = 0;
		} else {
			b |= 0x80;
		}

		buf[packed_bytes] = b;
		packed_bytes++;
	}

	return packed_bytes;
}

static int gator_annotate_pack_long(uint8_t *const buf, int64_t val)
{
	int packed_bytes = 0;
	int more = 1;
	while (more) {
		/* low order 7 bits of x */
		char b = val & 0x7f;
		val >>= 7;

		if ((val == 0 && (b & 0x40) == 0) || (val == -1 && (b & 0x40) != 0)) {
			more = 0;
		} else {
			b |= 0x80;
		}

		buf[packed_bytes] = b;
		packed_bytes++;
	}

	return packed_bytes;
}

static int gator_annotate_marshal_color(uint8_t *const buf, const uint32_t color)
{
	buf[0] = (color >> 8) & 0xff;
	buf[1] = (color >> 16) & 0xff;
	buf[2] = (color >> 24) & 0xff;
	buf[3] = (color >> 0) & 0xff;

	return 4;
}

static void gator_annotate_fail(const int fd)
{
	gator_annotate_state.connected = 0;
	ANNOTATE_CLEAR_FD();
	close(fd);
}

uint64_t gator_get_time(void)
{
	struct timespec ts;
	uint64_t curr_time;

#ifndef CLOCK_MONOTONIC_RAW
	/* Android doesn't have this defined but it was added in Linux 2.6.28 */
#define CLOCK_MONOTONIC_RAW 4
#endif
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		return ~0;
	}
	curr_time = ((uint64_t)1000000000)*ts.tv_sec + ts.tv_nsec;

	return curr_time;
}

static int gator_annotate_marshal_time(const int fd, uint8_t *const buf)
{
	uint64_t curr_time;

	curr_time = gator_get_time();
	if (curr_time == ~0ULL) {
		gator_annotate_fail(fd);
		return -1;
	}
	return gator_annotate_pack_long(buf, curr_time);
}

static int gator_annotate_write_unbuffered(const int fd, const void *const data, size_t length)
{
	size_t pos = 0;
	ssize_t bytes;

	while (pos < length) {
		/* Use MSG_NOSIGNAL to suppress SIGPIPE when writing to a socket */
		bytes = send(fd, (const uint8_t *)data + pos, length - pos, MSG_NOSIGNAL);
		if (bytes < 0) {
			gator_annotate_fail(fd);
			return -1;
		}
		pos += bytes;
	}

	return 0;
}

static int gator_annotate_write(const int fd, struct buffer *const buffer, const void *const data, size_t length)
{
	if (length == 0) {
		/* Do nothing */
	} else if (buffer->length + length <= sizeof(buffer->data)) {
		/* New data fits within the buffer */
		memcpy(buffer->data + buffer->length, data, length);
		buffer->length += length;
	} else if (buffer->length + length <= 2*sizeof(buffer->data)) {
		/* After one write, new data fits within the buffer */
		memcpy(buffer->data + buffer->length, data, sizeof(buffer->data) - buffer->length);
		if (gator_annotate_write_unbuffered(fd, buffer->data, sizeof(buffer->data)) != 0) {
			return -1;
		}

		memcpy(buffer->data, (const uint8_t *)data + buffer->length, length - buffer->length);
		buffer->length = length - buffer->length;
	} else {
		/* Two writes are necessary */
		if (gator_annotate_write_unbuffered(fd, buffer->data, buffer->length) != 0) {
			return -1;
		}
		buffer->length = 0;
		if (gator_annotate_write_unbuffered(fd, data, length) != 0) {
			return -1;
		}
	}

	return 0;
}

static int gator_annotate_flush(const int fd, struct buffer *const buffer)
{
	return gator_annotate_write_unbuffered(fd, buffer->data, buffer->length);
}

static void gator_annotate_send_counter(const struct counter *const counter);
static void gator_cam_send_track(const struct cam_track *const track);
static void gator_cam_send_name(const struct cam_name *const name);

static int socket_cloexec(int domain, int type, int protocol) {
#ifdef SOCK_CLOEXEC
  return socket(domain, type | SOCK_CLOEXEC, protocol);
#else
  int sock = socket(domain, type, protocol);
  if (sock < 0) {
    return -1;
  }
  int fdf = fcntl(sock, F_GETFD);
  if ((fdf == -1) || (fcntl(sock, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
    close(sock);
    return -1;
  }
  return sock;
#endif
}

static int gator_annotate_connect(void)
{
	struct buffer buffer;
	struct sockaddr_un addr;
	struct counter *counter;
	struct cam_track *track;
	struct cam_name *name;
	int fd;
	uint8_t header[2*sizeof(uint32_t) + 1];

	fd = socket_cloexec(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		goto fail_clear_connected;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, STREAMLINE_ANNOTATE, sizeof(STREAMLINE_ANNOTATE));
	if (connect(fd, (struct sockaddr *)&addr, offsetof(struct sockaddr_un, sun_path) + sizeof(STREAMLINE_ANNOTATE) - 1) != 0) {
		goto fail_close_fd;
	}

	/* Send tid as gatord cannot autodiscover it and the per process unique id */
	buffer.length = 0;
	gator_annotate_marshal_uint32(header, syscall(__NR_gettid));
	gator_annotate_marshal_uint32(header + sizeof(uint32_t), getpid());
	header[2*sizeof(uint32_t)] = gator_dont_mangle_keys;
	if ((gator_annotate_write(fd, &buffer, gator_annotate_handshake, sizeof(gator_annotate_handshake) - 1) != 0) ||
	    (gator_annotate_write(fd, &buffer, header, sizeof(header)) != 0) ||
	    (gator_annotate_flush(fd, &buffer) != 0)) {
		/* gator_annotate_write/flush has already performed the cleanup */
		return -1;
	}

	/* fd is ready for use, share it */
	ANNOTATE_SAVE_FD(fd);
	gator_annotate_state.connected = 1;

	/* Send counters, must be after the fd is set as gator_annotate_send_counter expects it */
	for (counter = gator_annotate_counters; counter != NULL; counter = counter->next) {
		gator_annotate_send_counter(counter);
	}
	for (track = gator_cam_tracks; track != NULL; track = track->next) {
		gator_cam_send_track(track);
	}
	for (name = gator_cam_names; name != NULL; name = name->next) {
		gator_cam_send_name(name);
	}

	return fd;

fail_close_fd:
	close(fd);
fail_clear_connected:
	gator_annotate_state.connected = 0;

	return -1;
}

static int gator_annotate_get_file(void)
{
	int fd;

	ANNOTATE_GET_FD(fd);
	if (fd < 0) {
		if (gator_annotate_state.connected) {
			/* The connection is valid but this thread is not connected */
			fd = gator_annotate_connect();
		} else if (gator_annotate_parent_fd >= 0) {
			/* Has the gatord parent told us to try reconnecting? */
			ssize_t bytes;
			int temp;
			bytes = recv(gator_annotate_parent_fd, &temp, sizeof(temp), MSG_DONTWAIT);
			if (((bytes < 0) && (errno != EAGAIN)) || (bytes == 0)) {
				close(gator_annotate_parent_fd);
				gator_annotate_parent_fd = -1;
			} else if (bytes > 0) {
				/* Try to reconnect */
				fd = gator_annotate_connect();
			}
		}
	}

	return fd;
}

static void gator_annotate_parent_connect(void)
{
	struct sockaddr_un addr;

	gator_annotate_parent_fd = socket_cloexec(PF_UNIX, SOCK_STREAM, 0);
	if (gator_annotate_parent_fd < 0) {
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, STREAMLINE_ANNOTATE_PARENT, sizeof(STREAMLINE_ANNOTATE_PARENT));
	if (connect(gator_annotate_parent_fd, (struct sockaddr *)&addr, offsetof(struct sockaddr_un, sun_path) + sizeof(STREAMLINE_ANNOTATE_PARENT) - 1) != 0) {
		close(gator_annotate_parent_fd);
		gator_annotate_parent_fd = -1;
		fprintf(stderr, "Warning %s(%s:%i): Not connected to gatord, the application will run normally but Streamline will not collect annotations. To collect annotations, please verify you are running gatord 5.%i or later and that SELinux is disabled.\n", __func__, __FILE__, __LINE__, gator_minimum_version);
		return;
	}
}

void gator_annotate_fork_child(void)
{
	int fd;

	/* Close the current file handle as it is associated with a different tid */
	ANNOTATE_GET_FD(fd);
	if (fd >= 0) {
		/* Do not call gator_annotate_fail as the value of connected must not change */
		ANNOTATE_CLEAR_FD();
		close(fd);
	}

	if (gator_annotate_parent_fd >= 0) {
		close(gator_annotate_parent_fd);
		/* Open a new parent connection */
		gator_annotate_parent_connect();
	}
}

void gator_annotate_setup(void)
{
	if (!gator_annotate_state.initialized) {
		/* If a future version of the Android API supports pthread_atfork, check it */
#if USE_PTHREADS && !defined(__ANDROID_API__)
		if (pthread_atfork(NULL, NULL, gator_annotate_fork_child) != 0) {
			return;
		}
#endif
		gator_annotate_parent_fd = -1;
		ANNOTATE_INIT_FD();
		gator_annotate_state.initialized = 1;
	}

	/* Don't fail just because gator_annotate_parent_fd connection fails - some configurations like local capture may not have this socket */
	if (gator_annotate_parent_fd < 0) {
		gator_annotate_parent_connect();
	}

	if (!gator_annotate_state.connected) {
		if (gator_annotate_connect() < 0) {
			return;
		}
	}
}

static const int gator_annotate_header_size = 1 + sizeof(uint32_t);

#define GATOR_MARSHAL_TIME_START(HEADER_TYPE) \
	fd = gator_annotate_get_file(); \
	if (fd < 0) { \
		return; \
	} \
 \
	message[0] = HEADER_TYPE; \
	if ((message_size = gator_annotate_marshal_time(fd, message + gator_annotate_header_size)) < 0) { \
		return; \
	} \
	message_size += gator_annotate_header_size

#define GATOR_MARSHAL_START(HEADER_TYPE) \
	fd = gator_annotate_get_file(); \
	if (fd < 0) { \
		return; \
	} \
 \
	message[0] = HEADER_TYPE; \
	message_size = gator_annotate_header_size

void gator_annotate_str(const uint32_t channel, const char *const str)
{
	struct buffer buffer;
	int fd;
	int str_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG + MAXSIZE_PACK_INT];

	GATOR_MARSHAL_TIME_START(HEADER_UTF8);
	str_size = (str == NULL) ? 0 : strlen(str);
	message_size += gator_annotate_pack_int(message + message_size, channel);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + str_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, str, str_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_annotate_color(const uint32_t channel, const uint32_t color, const char *const str)
{
	struct buffer buffer;
	int fd;
	int str_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG + MAXSIZE_PACK_INT + SIZE_COLOR];

	GATOR_MARSHAL_TIME_START(HEADER_UTF8_COLOR);
	str_size = (str == NULL) ? 0 : strlen(str);
	message_size += gator_annotate_pack_int(message + message_size, channel);
	message_size += gator_annotate_marshal_color(message + message_size, color);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + str_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, str, str_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_annotate_name_channel(const uint32_t channel, const uint32_t group, const char *const str)
{
	struct buffer buffer;
	int fd;
	int str_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG + 2*MAXSIZE_PACK_INT];

	GATOR_MARSHAL_TIME_START(HEADER_CHANNEL_NAME);
	str_size = (str == NULL) ? 0 : strlen(str);
	message_size += gator_annotate_pack_int(message + message_size, channel);
	message_size += gator_annotate_pack_int(message + message_size, group);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + str_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, str, str_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_annotate_name_group(const uint32_t group, const char *const str)
{
	struct buffer buffer;
	int fd;
	int str_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG + MAXSIZE_PACK_INT];

	GATOR_MARSHAL_TIME_START(HEADER_GROUP_NAME);
	str_size = (str == NULL) ? 0 : strlen(str);
	message_size += gator_annotate_pack_int(message + message_size, group);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + str_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, str, str_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_annotate_visual(const void *const data, const uint32_t length, const char *const str)
{
	struct buffer buffer;
	int fd;
	int str_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG];
	uint8_t zero = 0;

	GATOR_MARSHAL_TIME_START(HEADER_VISUAL);
	str_size = (str == NULL) ? 0 : strlen(str);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + str_size + 1 + length);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, str, str_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, &zero, sizeof(zero)) == 0) &&
	    (gator_annotate_write(fd, &buffer, data, length) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_annotate_marker(const char *const str)
{
	struct buffer buffer;
	int fd;
	int str_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG];

	GATOR_MARSHAL_TIME_START(HEADER_MARKER);
	str_size = (str == NULL) ? 0 : strlen(str);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + str_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, str, str_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_annotate_marker_color(const uint32_t color, const char *const str)
{
	struct buffer buffer;
	int fd;
	int str_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG + SIZE_COLOR];

	GATOR_MARSHAL_TIME_START(HEADER_MARKER_COLOR);
	str_size = (str == NULL) ? 0 : strlen(str);
	message_size += gator_annotate_marshal_color(message + message_size, color);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + str_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, str, str_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

static void gator_annotate_send_counter(const struct counter *const counter)
{
	struct buffer buffer;
	size_t i;
	int fd;
	int title_size;
	int name_size;
	int units_size;
	int desc_size;
	int message_size;
	uint8_t message[1<<10];
	uint8_t zero = 0;

	GATOR_MARSHAL_START(HEADER_COUNTER);
	title_size = (counter->title == NULL) ? 0 : strlen(counter->title);
	name_size = (counter->name == NULL) ? 0 : strlen(counter->name);
	units_size = (counter->units == NULL) ? 0 : strlen(counter->units);
	desc_size = (counter->description == NULL) ? 0 : strlen(counter->description);
	message_size += gator_annotate_pack_int(message + message_size, counter->id);
	message_size += gator_annotate_pack_int(message + message_size, counter->per_cpu);
	message_size += gator_annotate_pack_int(message + message_size, counter->counter_class);
	message_size += gator_annotate_pack_int(message + message_size, counter->display);
	message_size += gator_annotate_pack_int(message + message_size, counter->modifier);
	message_size += gator_annotate_pack_int(message + message_size, counter->series_composition);
	message_size += gator_annotate_pack_int(message + message_size, counter->rendering_type);
	message_size += gator_annotate_pack_int(message + message_size, counter->average_selection);
	message_size += gator_annotate_pack_int(message + message_size, counter->average_cores);
	message_size += gator_annotate_pack_int(message + message_size, counter->percentage);
	message_size += gator_annotate_pack_int(message + message_size, counter->activity_count);
	message_size += gator_annotate_pack_int(message + message_size, counter->cores);
	message_size += gator_annotate_marshal_color(message + message_size, counter->color);
	for (i = 0; i < counter->activity_count; ++i) {
		int size = (counter->activities[i] == NULL) ? 0 : strlen(counter->activities[i]);
		if ((int)sizeof(message) <= message_size + size + 1 + MAXSIZE_PACK_INT) {
			/* Activities are too large */
			return;
		}
		if (size > 0) {
			memcpy(message + message_size, counter->activities[i], size);
			message_size += size;
		}
		message[message_size++] = '\0';
		message_size += gator_annotate_marshal_color(message + message_size, counter->activity_colors[i]);
	}
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + title_size + 1 + name_size + 1 + units_size + 1 + desc_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, counter->title, title_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, &zero, sizeof(zero)) == 0) &&
	    (gator_annotate_write(fd, &buffer, counter->name, name_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, &zero, sizeof(zero)) == 0) &&
	    (gator_annotate_write(fd, &buffer, counter->units, units_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, &zero, sizeof(zero)) == 0) &&
	    (gator_annotate_write(fd, &buffer, counter->description, desc_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_annotate_counter(const uint32_t id, const char *const title, const char *const name, const int per_cpu, const enum gator_annotate_counter_class counter_class, const enum gator_annotate_display display, const char *const units, const uint32_t modifier, const enum gator_annotate_series_composition series_composition, const enum gator_annotate_rendering_type rendering_type, const int average_selection, const int average_cores, const int percentage, const size_t activity_count, const char *const *const activities, const uint32_t *const activity_colors, const uint32_t cores, const uint32_t color, const char *const description)
{
	struct counter *counter;
	struct counter **storage;
	size_t j;

	counter = (struct counter *)malloc(sizeof(*counter));
	if (counter == NULL) {
		return;
	}

	/* Save off this counter so it can be resent if needed */
	counter->next = NULL;
	counter->id = id;
	counter->title = (title == NULL) ? NULL : strdup(title);
	counter->name = (name == NULL) ? NULL : strdup(name);
	counter->per_cpu = per_cpu;
	counter->counter_class = counter_class;
	counter->display = display;
	counter->units = (units == NULL) ? NULL : strdup(units);
	counter->modifier = modifier;
	counter->series_composition = series_composition;
	counter->rendering_type = rendering_type;
	counter->average_selection = average_selection;
	counter->average_cores = average_cores;
	counter->percentage = percentage;
	counter->activity_count = activity_count;
	if (activity_count == 0) {
		counter->activities = NULL;
		counter->activity_colors = NULL;
	} else {
		counter->activities = (const char **)malloc(activity_count*sizeof(activities[0]));
		if (counter->activities == NULL) {
			free(counter);
			return;
		}
		counter->activity_colors = (uint32_t *)malloc(activity_count*sizeof(activity_colors[0]));
		if (counter->activity_colors == NULL) {
			free(counter->activities);
			free(counter);
			return;
		}
		for (j = 0; j < activity_count; ++j) {
			counter->activities[j] = (activities[j] == NULL) ? NULL : strdup(activities[j]);
			counter->activity_colors[j] = activity_colors[j];
		}
	}
	counter->cores = cores;
	counter->color = color;
	counter->description = (description == NULL) ? NULL : strdup(description);

	storage = &gator_annotate_counters;
	while (*storage != NULL) {
		storage = &(*storage)->next;
	}
	*storage = counter;

	gator_annotate_send_counter(counter);
}

void gator_annotate_counter_value(const uint32_t core, const uint32_t id, const uint32_t value)
{
	struct buffer buffer;
	int fd;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG + 3*MAXSIZE_PACK_INT];

	GATOR_MARSHAL_TIME_START(HEADER_COUNTER_VALUE);
	message_size += gator_annotate_pack_int(message + message_size, core);
	message_size += gator_annotate_pack_int(message + message_size, id);
	message_size += gator_annotate_pack_int(message + message_size, value);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

static void gator_cam_send_track(const struct cam_track *const track)
{
	struct buffer buffer;
	int fd;
	int name_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + 3*MAXSIZE_PACK_INT];

	GATOR_MARSHAL_START(HEADER_CAM_TRACK);
	name_size = (track->name == NULL) ? 0 : strlen(track->name);
	message_size += gator_annotate_pack_int(message + message_size, track->view_uid);
	message_size += gator_annotate_pack_int(message + message_size, track->track_uid);
	message_size += gator_annotate_pack_int(message + message_size, track->parent_track);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + name_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, track->name, name_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_annotate_activity_switch(const uint32_t core, uint32_t id, uint32_t activity, uint32_t tid)
{
	struct buffer buffer;
	int fd;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_LONG + 4*MAXSIZE_PACK_INT];

	GATOR_MARSHAL_TIME_START(HEADER_ACTIVITY_SWITCH);
	message_size += gator_annotate_pack_int(message + message_size, core);
	message_size += gator_annotate_pack_int(message + message_size, id);
	message_size += gator_annotate_pack_int(message + message_size, activity);
	message_size += gator_annotate_pack_int(message + message_size, tid);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_cam_track(const uint32_t view_uid, const uint32_t track_uid, const uint32_t parent_track, const char *const name)
{
	struct cam_track *track;
	struct cam_track **storage;

	track = (struct cam_track *)malloc(sizeof(*track));
	if (track == NULL) {
		return;
	}

	/* Save off this track so it can be resent if needed */
	track->next = NULL;
	track->view_uid = view_uid;
	track->track_uid = track_uid;
	track->parent_track = parent_track;
	track->name = (name == NULL) ? NULL : strdup(name);

	storage = &gator_cam_tracks;
	while (*storage != NULL) {
		storage = &(*storage)->next;
	}
	*storage = track;

	gator_cam_send_track(track);
}

void gator_cam_job(const uint32_t view_uid, const uint32_t job_uid, const char *const name, const uint32_t track, const uint64_t start_time, const uint64_t duration, const uint32_t color, const uint32_t primary_dependency, const size_t dependency_count, const uint32_t *const dependencies)
{
	struct buffer buffer;
	size_t i;
	int fd;
	int name_size;
	int message_size;
	uint8_t message[1<<10];

	GATOR_MARSHAL_START(HEADER_CAM_JOB);
	name_size = (name == NULL) ? 0 : strlen(name);
	message_size += gator_annotate_pack_int(message + message_size, view_uid);
	message_size += gator_annotate_pack_int(message + message_size, job_uid);
	message_size += gator_annotate_pack_int(message + message_size, track);
	message_size += gator_annotate_pack_long(message + message_size, start_time);
	message_size += gator_annotate_pack_long(message + message_size, duration);
	message_size += gator_annotate_marshal_color(message + message_size, color);
	message_size += gator_annotate_pack_int(message + message_size, primary_dependency);
	message_size += gator_annotate_pack_int(message + message_size, dependency_count);
	for (i = 0; i < dependency_count; ++i) {
		if ((int)sizeof(message) <= message_size + MAXSIZE_PACK_INT) {
			/* Activities are too large */
			return;
		}
		message_size += gator_annotate_pack_int(message + message_size, dependencies[i]);
	}
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + name_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, name, name_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

static void gator_cam_send_name(const struct cam_name *const name)
{
	struct buffer buffer;
	int fd;
	int name_size;
	int message_size;
	uint8_t message[1 + sizeof(uint32_t) + MAXSIZE_PACK_INT];

	GATOR_MARSHAL_START(HEADER_CAM_VIEW_NAME);
	name_size = (name->name == NULL) ? 0 : strlen(name->name);
	message_size += gator_annotate_pack_int(message + message_size, name->view_uid);
	gator_annotate_marshal_uint32(message + 1, message_size - gator_annotate_header_size + name_size);

	buffer.length = 0;
	if ((gator_annotate_write(fd, &buffer, message, message_size) == 0) &&
	    (gator_annotate_write(fd, &buffer, name->name, name_size) == 0)) {
		gator_annotate_flush(fd, &buffer);
	}
}

void gator_cam_view_name(const uint32_t view_uid, const char *const name)
{
	struct cam_name *view_name;
	struct cam_name **storage;

	view_name = (struct cam_name *)malloc(sizeof(*view_name));
	if (view_name == NULL) {
		return;
	}

	/* Save off this name so it can be resent if needed */
	view_name->next = NULL;
	view_name->view_uid = view_uid;
	view_name->name = (name == NULL) ? NULL : strdup(name);

	storage = &gator_cam_names;
	while (*storage != NULL) {
		storage = &(*storage)->next;
	}
	*storage = view_name;

	gator_cam_send_name(view_name);
}
