/**
 * Copyright (c) 2014-2016, Arm Limited
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

package com.arm.streamline;

import java.io.BufferedOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;

public class StreamlineAnnotate {
    public static final Color               RED                     = new Color(0xff, 0, 0);
    public static final Color               BLUE                    = new Color(0, 0, 0xff);
    public static final Color               GREEN                   = new Color(0, 0xff, 0);
    public static final Color               PURPLE                  = new Color(0xff, 0, 0xff);
    public static final Color               YELLOW                  = new Color(0xff, 0xff, 0);
    public static final Color               CYAN                    = new Color(0, 0xff, 0xff);
    public static final Color               WHITE                   = new Color(0xff);
    public static final Color               LTGRAY                  = new Color(0xbb);
    public static final Color               DKGRAY                  = new Color(0x55);
    public static final Color               BLACK                   = new Color(0);

    private static final byte               ESCAPE_CODE             = 0x1c;
    private static final byte               STRING_ANNOTATION       = 0x06;
    private static final byte               NAME_CHANNEL_ANNOTATION = 0x07;
    private static final byte               NAME_GROUP_ANNOTATION   = 0x08;
    private static final byte               VISUAL_ANNOTATION       = 0x04;
    private static final byte               MARKER_ANNOTATION       = 0x05;
    private static final FileOutputStream   FILE;
    private static final OutputStream       OUT;

    static {
        FileOutputStream file = null;
        OutputStream out = null;
        try {
            file = new FileOutputStream("/dev/gator/annotate"); //$NON-NLS-1$
            out = new BufferedOutputStream(file);
        } catch (Exception exception) {
            // Gator may not be installed. Ignore.
        }
        OUT = out;
        FILE = file;
    }

    /**
     * Emit a textual annotation.
     *
     * @param msg The message to emit.
     */
    public static final void annotate(String msg) {
        annotate(0, null, msg);
    }

    /**
     * Emit a textual annotation.
     *
     * @param channel The channel the message is on
     * @param msg The message to emit.
     */
    public static final void annotate(int channel, String msg) {
        annotate(channel, null, msg);
    }

    /**
     * Emit a colored textual annotation.
     *
     * @param color The color to use.
     * @param msg The message to emit.
     */
    public static final void annotate(Color color, String msg) {
        annotate(0, color, msg);
    }

    /**
     * Emit a colored textual annotation.
     *
     * @param channel The channel the message is on
     * @param color The color to use.
     * @param msg The message to emit.
     */
    public static final void annotate(int channel, Color color, String msg) {
        if (OUT != null) {
            try {
                int msgLength;
                if (msg != null) {
                    byte[] msgData = msg.getBytes();
                    msgLength = msgData.length;
                } else {
                    msgLength = 0;
                }
                if (color != null) {
                    msgLength += 4;
                }
                OUT.write(ESCAPE_CODE);
                OUT.write(STRING_ANNOTATION);
                OUT.write((byte) (channel & 0xff));
                OUT.write((byte) (channel >> 8 & 0xff));
                OUT.write((byte) (channel >> 16 & 0xff));
                OUT.write((byte) (channel >>> 24 & 0xff));
                OUT.write((byte) (msgLength & 0xff));
                OUT.write((byte) (msgLength >> 8 & 0xff));
                if (color != null) {
                    OUT.write(0x1b);
                    OUT.write(color.red);
                    OUT.write(color.green);
                    OUT.write(color.blue);
                }
                if (msg != null) {
                    OUT.write(msg.getBytes());
                }
                OUT.flush();
                // Force the bytes to reach the file system, as flush() may not
                FILE.getFD().sync();
            } catch (IOException exception) {
                // Failed, but don't log
            }
        }
    }

    /**
     * End an existing annotation. Rather than calling this method, you can instead emit a new
     * annotation.
     */
    public static final void end() {
        annotate(0, null, null);
    }

    /**
     * End an existing annotation. Rather than calling this method, you can instead emit a new
     * annotation.
     *
     * @param channel The channel the message is on
     */
    public static final void end(int channel) {
        annotate(channel, null, null);
    }

    /**
     * Assign a name and group to a channel. Channels are defined per thread. This means that if the
     * same channel number is used on different threads they are in fact separate channels. A
     * channel can belong to only one group per thread. This means channel 1 cannot be part of both
     * group 1 and group 2 on the same thread.
     *
     * @param channel The channel being name
     * @param group The group the channel belongs to
     * @param name The name of the channel
     */
    public static void nameChannel(int channel, int group, String name) {
        if (OUT != null) {
            try {
                int nameLength;
                if (name != null) {
                    byte[] msgData = name.getBytes();
                    nameLength = msgData.length;
                } else {
                    nameLength = 0;
                }
                OUT.write(ESCAPE_CODE);
                OUT.write(NAME_CHANNEL_ANNOTATION);
                OUT.write((byte) (channel & 0xff));
                OUT.write((byte) (channel >> 8 & 0xff));
                OUT.write((byte) (channel >> 16 & 0xff));
                OUT.write((byte) (channel >>> 24 & 0xff));
                OUT.write((byte) (group & 0xff));
                OUT.write((byte) (group >> 8 & 0xff));
                OUT.write((byte) (group >> 16 & 0xff));
                OUT.write((byte) (group >>> 24 & 0xff));
                OUT.write((byte) (nameLength & 0xff));
                OUT.write((byte) (nameLength >> 8 & 0xff));
                if (name != null) {
                    OUT.write(name.getBytes());
                }
                OUT.flush();
                // Force the bytes to reach the file system, as flush() may not
                FILE.getFD().sync();
            } catch (IOException exception) {
                // Failed, but don't log
            }
        }
    }

    /**
     * Assign a name to a group. Groups are defined per thread
     *
     * @param group The group being named
     * @param name The name of the group
     */
    public static void nameGroup(int group, String name) {
        if (OUT != null) {
            try {
                int nameLength;
                if (name != null) {
                    byte[] msgData = name.getBytes();
                    nameLength = msgData.length;
                } else {
                    nameLength = 0;
                }
                OUT.write(ESCAPE_CODE);
                OUT.write(NAME_GROUP_ANNOTATION);
                OUT.write((byte) (group & 0xff));
                OUT.write((byte) (group >> 8 & 0xff));
                OUT.write((byte) (group >> 16 & 0xff));
                OUT.write((byte) (group >>> 24 & 0xff));
                OUT.write((byte) (nameLength & 0xff));
                OUT.write((byte) (nameLength >> 8 & 0xff));
                if (name != null) {
                    OUT.write(name.getBytes());
                }
                OUT.flush();
                // Force the bytes to reach the file system, as flush() may not
                FILE.getFD().sync();
            } catch (IOException exception) {
                // Failed, but don't log
            }
        }
    }

    /**
     * Emit a visual annotation.
     *
     * @param imageData The bytes of a supported image format: PNG, GIF, TIFF, JPEG, BMP.
     */
    public static final void visualAnnotate(byte[] imageData) {
        visualAnnotate(null, imageData);
    }

    /**
     * Emit a visual annotation with a text label.
     *
     * @param msg The message to emit.
     * @param imageData The bytes of a supported image format: PNG, GIF, TIFF, JPEG, BMP.
     */
    public static final void visualAnnotate(String msg, byte[] imageData) {
        if (OUT != null) {
            try {
                int msgLength;
                if (msg != null) {
                    byte[] msgData = msg.getBytes();
                    msgLength = msgData.length;
                } else {
                    msgLength = 0;
                }
                byte[] header = new byte[4];
                header[0] = ESCAPE_CODE;
                header[1] = VISUAL_ANNOTATION;
                header[2] = (byte) (msgLength & 0xff);
                header[3] = (byte) (msgLength >> 8 & 0xff);
                OUT.write(header);
                if (msg != null) {
                    OUT.write(msg.getBytes());
                }
                int imgLength = imageData != null ? imageData.length : 0;
                header[0] = (byte) (imgLength & 0xff);
                header[1] = (byte) (imgLength >> 8 & 0xff);
                header[2] = (byte) (imgLength >> 16 & 0xff);
                header[3] = (byte) (imgLength >>> 24 & 0xff);
                OUT.write(header);
                if (imgLength > 0) {
                    OUT.write(imageData);
                }
                OUT.flush();
                // Force the bytes to reach the file system, as flush() may not
                FILE.getFD().sync();
            } catch (IOException exception) {
                // Failed, but don't log
            }
        }
    }

    /**
     * Emit an annotation marker.
     */
    public static final void marker() {
        marker(null, null);
    }

    /**
     * Emit a textual annotation marker.
     *
     * @param msg The message to emit.
     */
    public static final void marker(String msg) {
        marker(null, msg);
    }

    /**
     * Emit a color annotation marker.
     *
     * @param color The color to use for the marker.
     */
    public static final void marker(Color color) {
        marker(color, null);
    }

    /**
     * Emit a color textual annotation marker.
     *
     * @param color The color to use for the marker.
     * @param msg The message to emit.
     */
    public static final void marker(Color color, String msg) {
        if (OUT != null) {
            try {
                int msgLength;
                if (msg != null) {
                    byte[] msgData = msg.getBytes();
                    msgLength = msgData.length;
                } else {
                    msgLength = 0;
                }
                if (color != null) {
                    msgLength += 4;
                }
                byte[] header = new byte[4];
                header[0] = ESCAPE_CODE;
                header[1] = MARKER_ANNOTATION;
                header[2] = (byte) (msgLength & 0xff);
                header[3] = (byte) (msgLength >> 8 & 0xff);
                OUT.write(header);
                if (color != null) {
                    OUT.write(0x1b);
                    OUT.write(color.red);
                    OUT.write(color.green);
                    OUT.write(color.blue);
                }
                if (msg != null) {
                    OUT.write(msg.getBytes());
                }
                OUT.flush();
                // Force the bytes to reach the file system, as flush() may not
                FILE.getFD().sync();
            } catch (IOException exception) {
                // Failed, but don't log
            }
        }
    }

    public static class Color {
        public int  red;
        public int  green;
        public int  blue;

        public Color(int redByte, int greenByte, int blueByte) {
            red = redByte;
            green = greenByte;
            blue = blueByte;
        }

        public Color(int gray) {
            this(gray, gray, gray);
        }
    }
}
