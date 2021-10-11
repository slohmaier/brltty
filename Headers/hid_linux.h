/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2021 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#ifndef BRLTTY_INCLUDED_HID_LINUX
#define BRLTTY_INCLUDED_HID_LINUX

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern ssize_t hidGetDeviceName (HidDevice *device, char *buffer, size_t size);
extern ssize_t hidGetPhysicalAddress (HidDevice *device, char *buffer, size_t size);
extern ssize_t hidGetUniqueIdentifier (HidDevice *device, char *buffer, size_t size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BRLTTY_INCLUDED_HID_LINUX */