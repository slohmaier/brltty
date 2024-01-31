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

//includes for socket
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

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

/* \REMBRAILLE-COM\  */
typedef enum {
    RB_CMD_NONE,
    RB_CMD_DISPSIZE,
    RB_CMD_INVALUD
} RemBrailleCommand;
/* /REMBRAILLE-COM/  */

//socket connection information
static char *host = NULL;
static int port = -1;
//socket
static int socketfd = -1;
//threads
pthread_t threadSocket = NULL;
pthread_t threadReadSocket = NULL;

//constantly read from socket
static void thread_readsocket(void)
{
    char buffer[1024];
    char *message;
    uint16_t messageLength;
    int n, bufferOffset = 0;
    while (socketfd != -1) {
        n = read(socketfd, buffer+bufferOffset, 1024-bufferOffset);
        if (n < 0) {
            logMessage(LOG_ERR, "read: %s", strerror(errno));
            close(socketfd);
            socketfd = -1;
        } else {
            logMessage(LOG_DEBUG, "read: %s", buffer);
            //correct for remainder of previous message
            n += bufferOffset;
            bufferOffset = 0;
            
            while (n > 0) {
                //find next message
                for (message = buffer; *message != '\x1b' && message < buffer + 1024; message++);
                //is message-header complete?
                if (message - buffer < 3) {
                    logMessage(LOG_ERR, "Invalid message received");
                    memcpy(buffer, message, buffer - message);
                    bufferOffset = buffer - message;
                    continue;
                }
                messageLength = message[1] | (message[2] << 8);

                if (messageLength > n - (message - buffer)) {
                    logMessage(LOG_ERR, "Message too long");
                    memcpy(buffer, message, buffer - message);
                    bufferOffset = buffer - message;
                    continue;
                }

                if (message[3] < RB_CMD_NONE || message[3] > RB_CMD_INVALUD) {
                    logMessage(LOG_ERR, "Invalid command received");
                    memcpy(buffer, message, buffer - message);
                    bufferOffset = buffer - message;
                    continue;
                }

                switch (message[3]) {
                    case RB_CMD_DISPSIZE:
                        displaySize = message[4] | (message[5] << 8);
                        free(prevData);
                        free(prevText);
                        prevData = malloc(displaySize);
                        prevText = malloc(displaySize * sizeof(wchar_t));
                        break;
                    default:
                        logMessage(LOG_ERR, "Invalid command received");
                        memcpy(buffer, message, buffer - message);
                        bufferOffset = buffer - message;
                        continue;
                }

                n -= messageLength - (message - buffer) - 3;
                message += messageLength + 3;
            }
        }
    }
}


// constantly try opening the socket, if it's closed with a delay.
// the delay is incrementing up to 5 seconds
// if the socket is open, the delay is reset to 0
//
static void thread_socket(void *)
{
    while (1) {
        while (socketfd == -1)
        {
            socketfd = socket(AF_INET, SOCK_STREAM, 0);
            if (socketfd == -1)
            {
                logMessage(LOG_ERR, "socket: %s", strerror(errno));
                sleep(5);
            }
            else
            {
                struct sockaddr_in server;
                server.sin_addr.s_addr = inet_addr(host);
                server.sin_family = AF_INET;
                server.sin_port = htons(port);
                if (connect(socketfd, (struct sockaddr *)&server, sizeof(server)) < 0)
                {
                    logMessage(LOG_ERR, "connect: %s", strerror(errno));
                    close(socketfd);
                    socketfd = -1;
                    sleep(5);
                }
                else
                {
                    logMessage(LOG_DEBUG, "Connected to %s:%d", host, port);
                    //start readsocket thread
                    if (pthread_create(&threadReadSocket, NULL, &thread_readsocket, NULL) != 0)
                    {
                        logMessage(LOG_ERR, "pthread_create: %s", strerror(errno));
                        close(socketfd);
                        socketfd = -1;
                        sleep(5);
                    }
                }
            }
        }
        sleep(1);
    }
}

/* Function : brl_construct */
/* Opens a connection with BrlAPI's server */
static int brl_construct(BrailleDisplay *brl, char **parameters, const char *device)
{
    
    host = parameters[PARM_ADDRESS];
    //try reading port as integer
    port = atof(parameters[PARM_PORT]);
    if (port < 1 || port > 65535) {
        logMessage(LOG_CATEGORY(BRAILLE_DRIVER),
                             "Invalid Port '%s'!", parameters[PARM_PORT]);
    }

    //start socket with pthreads
    if (pthread_create(&threadSocket, NULL, &thread_socket, NULL) != 0)
    {
        logMessage(LOG_ERR, "pthread_create: %s", strerror(errno));
        return 0;
    }

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
