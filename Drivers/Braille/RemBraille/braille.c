/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2023 by The BRLTTY Developers.
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

#include "prologue.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "scr.h"
#include "cmd_brlapi.h"
#include "charset.h"

#define BRLAPI_NO_DEPRECATED
#include "brlapi.h"

typedef enum {
  PARM_ADDRESS=0,
  PARM_PORT=1
} DriverParameter;
#define BRLPARMS "address", "port"

#include "brl_driver.h"

/* \FROM BRLAPI\ */

//from brlAPI driver
static int displaySize;
static unsigned char *prevData;
static wchar_t *prevText;
static int prevCursor;
static int prevShown;
static int restart;
/* /FROM BRLAPI/ */

static char *host;
static int port;

/* Function : brl_construct */
/* Opens a connection with BrlAPI's server */
static int brl_construct(BrailleDisplay *brl, char **parameters, const char *device)
{
  
  host = parameters[PARM_ADDRESS];
  port = atoi(parameters[PARM_PORT]);
  if (port == NULL) {
    logMessage(LOG_CATEGORY(BRAILLE_DRIVER),
               "Invalid Port '%s'!", parameters[PARM_PORT]);
  }

  

  prevData = malloc(displaySize);
  memset(prevData, 0, displaySize);

  prevText = malloc(displaySize * sizeof(wchar_t));
  wmemset(prevText, WC_C(' '), displaySize);

  prevShown = 0;
  prevCursor = BRL_NO_CURSOR;
  restart = 0;

  return 1; //TODO 0 on error
}

/* Function : brl_destruct */
/* Frees memory and closes the connection with BrlAPI */
static void brl_destruct(BrailleDisplay *brl)
{
  free(prevData);
  free(prevText);
}

/* function : brl_writeWindow */
/* Displays a text on the braille window, only if it's different from */
/* the one already displayed */
static int brl_writeWindow(BrailleDisplay *brl, const wchar_t *text)
{
  setClientPriority(brl);

  brlapi_writeArguments_t arguments = BRLAPI_WRITEARGUMENTS_INITIALIZER;
  int vt = currentVirtualTerminal();

  if (vt == SCR_NO_VT) {
    /* should leave display */
    if (prevShown) {
      brlapi_write(&arguments);
      prevShown = 0;
    }
  } else {
    if (prevShown &&
        (memcmp(prevData,brl->buffer,displaySize) == 0) &&
        (!text || (wmemcmp(prevText,text,displaySize) == 0)) &&
        (brl->cursor == prevCursor)) {
      return 1;
    }

    unsigned char and[displaySize];
    memset(and, 0, sizeof(and));
    arguments.andMask = and;
    arguments.orMask = brl->buffer;

    if (text) {
      arguments.text = (char*) text;
      arguments.textSize = displaySize * sizeof(wchar_t);
      arguments.charset = (char*) getWcharCharset();
    }

    arguments.regionBegin = 1;
    arguments.regionSize = displaySize;
    arguments.cursor = (brl->cursor != BRL_NO_CURSOR)? (brl->cursor + 1): BRLAPI_CURSOR_OFF;

    if (brlapi_write(&arguments)==0) {
      memcpy(prevData,brl->buffer,displaySize);
      if (text)
	wmemcpy(prevText,text,displaySize);
      else
	wmemset(prevText,0,displaySize);
      prevCursor = brl->cursor;
      prevShown = 1;
    } else {
      logMessage(LOG_ERR, "write: %s", brlapi_strerror(&brlapi_error));
      restart = 1;
    }
  }

  return 1;
}

/* Function : brl_readCommand */
/* Reads a command from the braille keyboard */
static int brl_readCommand(BrailleDisplay *brl, KeyTableCommandContext context)
{
  brlapi_keyCode_t keycode;
  if (restart) return BRL_CMD_RESTARTBRL;
  switch (brlapi_readKey(0, &keycode)) {
    case 0: return EOF;
    case 1: return cmdBrlapiToBrltty(keycode);
    default: return BRL_CMD_RESTARTBRL;
  }
}
