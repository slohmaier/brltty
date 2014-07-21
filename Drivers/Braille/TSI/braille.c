/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2014 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version. Please see the file LICENSE-GPL for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

/* TSI/braille.c - Braille display driver for TSI displays
 *
 * Written by St�phane Doyon (s.doyon@videotron.ca)
 *
 * It attempts full support for Navigator 20/40/80 and Powerbraille 40/65/80.
 * It is designed to be compiled into BRLTTY version 3.5.
 *
 * History:
 * Version 2.74 apr2004: use message() to report low battery condition.
 * Version 2.73 jan2004: Fix key bindings for speech commands for PB80.
 *   Add CMD_SPKHOME to help.
 * Version 2.72 jan2003: brl->buffer now allocated by core.
 * Version 2.71: Added CMD_LEARN, BRL_CMD_NXPROMPT/CMD_PRPROMPT and CMD_SIXDOTS.
 * Version 2.70: Added CR_CUTAPPEND, BRL_BLK_CUTLINE, BRL_BLK_SETMARK, BRL_BLK_GOTOMARK
 *   and CR_SETLEFT. Changed binding for NXSEARCH.. Adjusted PB80 cut&paste
 *   bindings. Replaced CMD_CUT_BEG/CMD_CUT_END by CR_CUTBEGIN/CR_CUTRECT,
 *   and CMD_CSRJMP by CR_ROUTE+0. Adjusted cut_cursor for new cut&paste
 *   bindings (untested).
 * Version 2.61: Adjusted key bindings for preferences menu.
 * Version 2.60: Use TCSADRAIN when closing serial port. Slight API and
 *   name changes for BRLTTY 3.0. Argument to readbrl now ignore, instead
 *   of being validated. 
 * Version 2.59: Added bindings for CMD_LNBEG/LNEND.
 * Version 2.58: Added bindings for CMD_BACK and CR_MSGATTRIB.
 * Version 2.57: Fixed help screen/file for Nav80. We finally have a
 *   user who confirms it works!
 * Version 2.56: Added key binding for NXSEARCH.
 * Version 2.55: Added key binding for NXINDENT and NXBLNKLNS.
 * Version 2.54: Added key binding for switchvt.
 * Version 2.53: The IXOFF bit in the termios setting was inverted?
 * Version 2.52: Changed LOG_NOTICE to LOG_INFO. Was too noisy.
 * Version 2.51: Added CMD_RESTARTSPEECH.
 * Version 2.5: Added CMD_SPKHOME, sacrificed LNBEG and LNEND.
 * Version 2.4: Refresh display even if unchanged after every now and then so
 *   that it will clear up if it was garbled. Added speech key bindings (had
 *   to change a few bindings to make room). Added SKPEOLBLNK key binding.
 * Version 2.3: Reset serial port attributes at each detection attempt in
 *   initbrl. This should help BRLTTY recover if another application (such
 *   as kudzu) scrambles the serial port while BRLTTY is running.
 * Unnumbered version: Fixes for dynmically loading drivers (declare all
 *   non-exported functions and variables static).
 * Version 2.2beta3: Option to disable CTS checking. Apparently, Vario
 *   does not raise CTS when connected.
 * Version 2.2beta1: Exploring problems with emulators of TSI (PB40): BAUM
 *   and mdv mb408s. See if we can provide timing options for more flexibility.
 * Version 2.1: Help screen fix for new keys in preferences menu.
 * Version 2.1beta1: Less delays in writing braille to display for
 *   nav20/40 and pb40, delays still necessary for pb80 on probably for nav80.
 *   Additional routing keys for navigator. Cut&paste binding that combines
 *   routing key and normal key.
 * Version 2.0: Tested with Nav40 PB40 PB80. Support for functions added
 *   in BRLTTY 2.0: added key bindings for new fonctions (attributes and
 *   routing). Support for PB at 19200baud. Live detection of display, checks
 *   both at 9600 and 19200baud. RS232 wire monitoring. Ping when idle to 
 *   detect when display turned off and issue a CMD_RESTARTBRL.
 * Version 1.2 (not released) introduces support for PB65/80. Rework of key
 *   binding mechanism and readbrl(). Slight modifications to routing keys
 *   support, + corrections. May have broken routing key support for PB40.
 * Version 1.1 worked on nav40 and was reported to work on pb40.
 */

#include "prologue.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "log.h"
#include "parse.h"
#include "io_generic.h"
#include "async_wait.h"
#include "message.h"

typedef enum {
  PARM_HIGHBAUD
} DriverParameter;
#define BRLPARMS "highbaud"

#include "brl_driver.h"
#include "braille.h"
#include "brldefs-ts.h"

BEGIN_KEY_NAME_TABLE(routing)
  KEY_GROUP_ENTRY(TS_GRP_RoutingKeys, "RoutingKey"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLE(nav_small)
  KEY_NAME_ENTRY(TS_KEY_CursorLeft, "CursorLeft"),
  KEY_NAME_ENTRY(TS_KEY_CursorRight, "CursorRight"),
  KEY_NAME_ENTRY(TS_KEY_CursorUp, "CursorUp"),
  KEY_NAME_ENTRY(TS_KEY_CursorDown, "CursorDown"),

  KEY_NAME_ENTRY(TS_KEY_NavLeft, "NavLeft"),
  KEY_NAME_ENTRY(TS_KEY_NavRight, "NavRight"),
  KEY_NAME_ENTRY(TS_KEY_NavUp, "NavUp"),
  KEY_NAME_ENTRY(TS_KEY_NavDown, "NavDown"),

  KEY_NAME_ENTRY(TS_KEY_ThumbLeft, "ThumbLeft"),
  KEY_NAME_ENTRY(TS_KEY_ThumbRight, "ThumbRight"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLE(nav_large)
  KEY_NAME_ENTRY(TS_KEY_CursorLeft, "CursorLeft"),
  KEY_NAME_ENTRY(TS_KEY_CursorRight, "CursorRight"),
  KEY_NAME_ENTRY(TS_KEY_CursorUp, "CursorUp"),
  KEY_NAME_ENTRY(TS_KEY_CursorDown, "CursorDown"),

  KEY_NAME_ENTRY(TS_KEY_NavLeft, "LeftOuter"),
  KEY_NAME_ENTRY(TS_KEY_NavRight, "RightOuter"),
  KEY_NAME_ENTRY(TS_KEY_NavUp, "LeftInner"),
  KEY_NAME_ENTRY(TS_KEY_NavDown, "RightInner"),

  KEY_NAME_ENTRY(TS_KEY_ThumbLeft, "LeftThumb"),
  KEY_NAME_ENTRY(TS_KEY_ThumbRight, "RightThumb"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLE(pb_small)
  KEY_NAME_ENTRY(TS_KEY_CursorUp, "LeftRockerUp"),
  KEY_NAME_ENTRY(TS_KEY_CursorDown, "LeftRockerDown"),

  KEY_NAME_ENTRY(TS_KEY_NavLeft, "Backward"),
  KEY_NAME_ENTRY(TS_KEY_NavRight, "Forward"),
  KEY_NAME_ENTRY(TS_KEY_NavUp, "RightRockerUp"),
  KEY_NAME_ENTRY(TS_KEY_NavDown, "RightRockerDown"),

  KEY_NAME_ENTRY(TS_KEY_ThumbLeft, "Convex"),
  KEY_NAME_ENTRY(TS_KEY_ThumbRight, "Concave"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLE(pb_large)
  KEY_NAME_ENTRY(TS_KEY_Button1, "Button1"),
  KEY_NAME_ENTRY(TS_KEY_Button2, "Button2"),
  KEY_NAME_ENTRY(TS_KEY_Button3, "Button3"),
  KEY_NAME_ENTRY(TS_KEY_Button4, "Button4"),

  KEY_NAME_ENTRY(TS_KEY_Bar1, "Bar1"),
  KEY_NAME_ENTRY(TS_KEY_Bar2, "Bar2"),
  KEY_NAME_ENTRY(TS_KEY_Bar3, "Bar3"),
  KEY_NAME_ENTRY(TS_KEY_Bar4, "Bar4"),

  KEY_NAME_ENTRY(TS_KEY_Switch1Up, "Switch1Up"),
  KEY_NAME_ENTRY(TS_KEY_Switch1Down, "Switch1Down"),
  KEY_NAME_ENTRY(TS_KEY_Switch2Up, "Switch2Up"),
  KEY_NAME_ENTRY(TS_KEY_Switch2Down, "Switch2Down"),
  KEY_NAME_ENTRY(TS_KEY_Switch3Up, "Switch3Up"),
  KEY_NAME_ENTRY(TS_KEY_Switch3Down, "Switch3Down"),
  KEY_NAME_ENTRY(TS_KEY_Switch4Up, "Switch4Up"),
  KEY_NAME_ENTRY(TS_KEY_Switch4Down, "Switch4Down"),

  KEY_NAME_ENTRY(TS_KEY_LeftRockerUp, "LeftRockerUp"),
  KEY_NAME_ENTRY(TS_KEY_LeftRockerDown, "LeftRockerDown"),
  KEY_NAME_ENTRY(TS_KEY_RightRockerUp, "RightRockerUp"),
  KEY_NAME_ENTRY(TS_KEY_RightRockerDown, "RightRockerDown"),

  KEY_NAME_ENTRY(TS_KEY_Convex, "Convex"),
  KEY_NAME_ENTRY(TS_KEY_Concave, "Concave"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLES(nav20)
  KEY_NAME_TABLE(nav_small),
END_KEY_NAME_TABLES

BEGIN_KEY_NAME_TABLES(nav40)
  KEY_NAME_TABLE(nav_small),
END_KEY_NAME_TABLES

BEGIN_KEY_NAME_TABLES(nav80)
  KEY_NAME_TABLE(nav_large),
  KEY_NAME_TABLE(routing),
END_KEY_NAME_TABLES

BEGIN_KEY_NAME_TABLES(pb40)
  KEY_NAME_TABLE(pb_small),
  KEY_NAME_TABLE(routing),
END_KEY_NAME_TABLES

BEGIN_KEY_NAME_TABLES(pb65)
  KEY_NAME_TABLE(pb_large),
  KEY_NAME_TABLE(routing),
END_KEY_NAME_TABLES

BEGIN_KEY_NAME_TABLES(pb80)
  KEY_NAME_TABLE(pb_large),
  KEY_NAME_TABLE(routing),
END_KEY_NAME_TABLES

DEFINE_KEY_TABLE(nav20)
DEFINE_KEY_TABLE(nav40)
DEFINE_KEY_TABLE(nav80)
DEFINE_KEY_TABLE(pb40)
DEFINE_KEY_TABLE(pb65)
DEFINE_KEY_TABLE(pb80)

BEGIN_KEY_TABLE_LIST
  &KEY_TABLE_DEFINITION(nav20),
  &KEY_TABLE_DEFINITION(nav40),
  &KEY_TABLE_DEFINITION(nav80),
  &KEY_TABLE_DEFINITION(pb40),
  &KEY_TABLE_DEFINITION(pb65),
  &KEY_TABLE_DEFINITION(pb80),
END_KEY_TABLE_LIST

/* Stabilization delay after changing baud rate */
#define BAUD_DELAY (100)

#define FULL_FRESHEN_EVERY 12 /* do a full update every nth writeWindow(). This
				 should be a little over every 0.5secs. */
/* for routing keys */
#define ROUTING_BYTES_VERTICAL 4
#define ROUTING_BYTES_MAXIMUM 11
#define ROUTING_BYTES_40 9
#define ROUTING_BYTES_80 14
#define ROUTING_BYTES_81 15

// for writeWindow()
#define DIM_BRL_SEND 8

/* Description of reply to query */
#define IDENTITY_H1 0X00
#define IDENTITY_H2 0X05

/* Routing keys information (2 bytes header) */
#define ROUTING_H1 0x00
#define ROUTING_H2 0x08

/* input codes signaling low battery power (2bytes) */
#define BATTERY_H1 0x00
#define BATTERY_H2 0x01

/* Bit definition of key codes returned by the display.
 * Navigator and pb40 return 2 bytes, pb65/80 returns 6. Each byte has a
 * different specific mask/signature in the 3 most significant bits.
 * Other bits indicate whether a specific key is pressed.
 * See readbrl().
 */

/* We combine all key bits into one KeyNumberSet. Each byte is masked by the
 * corresponding "mask" to extract valid bits then those are shifted by
 * "shift" and or'ed into the 32bits "code".
 */

/* bits to take into account when checking each byte's signature */
#define KEYS_BYTE_SIGNATURE_MASK 0XE0

/* how we describe each byte */
typedef struct {
  unsigned char signature; /* it's signature */
  unsigned char mask; /* bits that do represent keys */
  unsigned char shift; /* where to shift them into "code" */
} KeysByteDescriptor;

/* Description of bytes for navigator and pb40. */
static const KeysByteDescriptor keysDescriptor_Navigator[] = {
  {.signature=0X60, .mask=0X1F, .shift=0},
  {.signature=0XE0, .mask=0X1F, .shift=5}
};

/* Description of bytes for pb65/80 */
static const KeysByteDescriptor keysDescriptor_PowerBraille[] = {
  {.signature=0X40, .mask=0X0F, .shift=10},
  {.signature=0XC0, .mask=0X0F, .shift=14},
  {.signature=0X20, .mask=0X05, .shift=18},
  {.signature=0XA0, .mask=0X05, .shift=21},
  {.signature=0X60, .mask=0X1F, .shift=24},
  {.signature=0XE0, .mask=0X1F, .shift=5}
};

/* Some special case input codes */
/* Global variables */

typedef struct {
  const char *modelName;
  const KeyTableDefinition *keyTableDefinition;

  unsigned char routingBytes;
  signed char routingKeyCount;

  unsigned slowUpdate:2;
  unsigned highBaudSupported:1;
  unsigned isPB40:1;
} ModelEntry;

static const ModelEntry modelNavigator20 = {
  .modelName = "Navigator 20",

  .routingBytes = ROUTING_BYTES_40,
  .routingKeyCount = 20,

  .keyTableDefinition = &KEY_TABLE_DEFINITION(nav20)
};

static const ModelEntry modelNavigator40 = {
  .modelName = "Navigator 40",

  .routingBytes = ROUTING_BYTES_40,
  .routingKeyCount = 40,

  .slowUpdate = 1,

  .keyTableDefinition = &KEY_TABLE_DEFINITION(nav40)
};

static const ModelEntry modelNavigator80 = {
  .modelName = "Navigator 80",

  .routingBytes = ROUTING_BYTES_80,
  .routingKeyCount = 80,

  .slowUpdate = 2,

  .keyTableDefinition = &KEY_TABLE_DEFINITION(nav80)
};

static const ModelEntry modelPowerBraille40 = {
  .modelName = "Power Braille 40",

  .routingBytes = ROUTING_BYTES_40,
  .routingKeyCount = 40,

  .highBaudSupported = 1,
  .isPB40 = 1,

  .keyTableDefinition = &KEY_TABLE_DEFINITION(pb40)
};

static const ModelEntry modelPowerBraille65 = {
  .modelName = "Power Braille 65",

  .routingBytes = ROUTING_BYTES_81,
  .routingKeyCount = 65,

  .slowUpdate = 2,
  .highBaudSupported = 1,

  .keyTableDefinition = &KEY_TABLE_DEFINITION(pb65)
};

static const ModelEntry modelPowerBraille80 = {
  .modelName = "Power Braille 80",

  .routingBytes = ROUTING_BYTES_81,
  .routingKeyCount = 81,

  .slowUpdate = 2,
  .highBaudSupported = 1,

  .keyTableDefinition = &KEY_TABLE_DEFINITION(pb80)
};

typedef enum {
  IPT_IDENTITY,
  IPT_ROUTING,
  IPT_BATTERY,
  IPT_KEYS
} InputPacketType;

typedef struct {
  union {
    unsigned char bytes[1];

    struct {
      unsigned char header[2];
      unsigned char columns;
      unsigned char dots;
      char version[4];
      unsigned char checksum[4];
    } identity;

    struct {
      unsigned char header[2];
      unsigned char count;
      unsigned char vertical[ROUTING_BYTES_VERTICAL];
      unsigned char horizontal[0X100 - 4];
    } routing;

    unsigned char keys[6];
  } fields;

  InputPacketType type;

  union {
    struct {
      unsigned char count;
    } routing;

    struct {
      const KeysByteDescriptor *descriptor;
      unsigned char count;
    } keys;
  } data;
} InputPacket;

struct BrailleDataStruct {
  const ModelEntry *model;
  SerialParameters serialParameters;
  unsigned char routingKeys[ROUTING_BYTES_MAXIMUM];

  /* version of the hardware */
  char hardwareVersion[3];

  /* number of cells available for text */
  unsigned char textCells;

  /* total number of cells - text + status */
  int totalCells;

  /* Type of delay the display requires after sending it a command.
   * 0 -> no delay, 1 -> drain only, 2 -> drain + wait for SEND_DELAY.
   */
  unsigned char slowUpdate;

  /* Whether multiple packets can be sent for a single update. */
  unsigned char noMultipleUpdates;

   /* We periodicaly refresh the display even if nothing has changed, will clear
    * out any garble...
    */
  unsigned char fullFreshenEvery;
};

static unsigned char *prevdata; /* previous data sent */
static unsigned char *dispbuf;

static ssize_t
writeBytes (BrailleDisplay *brl, const void *data, size_t size) {
  brl->writeDelay += brl->data->slowUpdate * 24;
  return writeBraillePacket(brl, NULL, data, size);
}

static BraillePacketVerifierResult
verifyPacket1 (
  BrailleDisplay *brl,
  const unsigned char *bytes, size_t size,
  size_t *length, void *data
) {
  InputPacket *packet = data;
  const off_t index = size - 1;
  const unsigned char byte = bytes[index];

  if (size == 1) {
    switch (byte) {
      case IDENTITY_H1:
        packet->type = IPT_IDENTITY;
        *length = 2;
        break;

      default:
        if ((byte & KEYS_BYTE_SIGNATURE_MASK) == keysDescriptor_Navigator[0].signature) {
          packet->data.keys.descriptor = keysDescriptor_Navigator;
          packet->data.keys.count = ARRAY_COUNT(keysDescriptor_Navigator);
          goto isKeys;
        }

        if ((byte & KEYS_BYTE_SIGNATURE_MASK) == keysDescriptor_PowerBraille[0].signature) {
          packet->data.keys.descriptor = keysDescriptor_PowerBraille;
          packet->data.keys.count = ARRAY_COUNT(keysDescriptor_PowerBraille);
          goto isKeys;
        }

        return BRL_PVR_INVALID;

      isKeys:
        packet->type = IPT_KEYS;
        *length = packet->data.keys.count;
        break;
    }
  } else {
    switch (packet->type) {
      case IPT_IDENTITY:
        if (size == 2) {
          switch (byte) {
            case IDENTITY_H2:
              *length = sizeof(packet->fields.identity);
              break;

            case ROUTING_H2:
              packet->type = IPT_ROUTING;
              *length = 3;
              break;

            case BATTERY_H2:
              packet->type = IPT_BATTERY;
              break;

            default:
              return BRL_PVR_INVALID;
          }
        }
        break;

      case IPT_ROUTING:
        if (size == 3) {
          packet->data.routing.count = byte;
          *length += packet->data.routing.count;
        }
        break;

      case IPT_KEYS:
        if ((byte & KEYS_BYTE_SIGNATURE_MASK) != packet->data.keys.descriptor[index].signature) return BRL_PVR_INVALID;
        break;

      default:
        break;
    }
  }

  return BRL_PVR_INCLUDE;
}

static size_t
readPacket (BrailleDisplay *brl, InputPacket *packet) {
  return readBraillePacket(brl, NULL, &packet->fields, sizeof(packet->fields), verifyPacket1, packet);
}

static int
getIdentity (BrailleDisplay *brl, InputPacket *reply) {
  static const unsigned char request[] = {0xFF, 0xFF, 0x0A};

  if (writeBytes(brl, request, sizeof(request))) {
    if (gioAwaitInput(brl->gioEndpoint, 100)) {
      size_t count = readPacket(brl, reply);

      if (count > 0) {
        if (reply->type == IPT_IDENTITY) return 1;
        logUnexpectedPacket(reply->fields.bytes, count);
      }
    } else {
      logMessage(LOG_DEBUG, "no response");
    }
  }

  return 0;
}

static int
setAutorepeat (BrailleDisplay *brl, int on, int delay, int interval) {
  const unsigned char request[] = {
    0XFF, 0XFF, 0X0D,
    on? ((delay + 9) / 10): 0XFF,
    on? ((interval + 9) / 10): 0XFF
  };

  return writeBytes(brl, request, sizeof(request));
}

static int
setLocalBaud (BrailleDisplay *brl, int baud) {
  SerialParameters *parameters = &brl->data->serialParameters;

  logMessage(LOG_DEBUG, "trying at %d baud", baud);
  if (parameters->baud == baud) return 1;

  parameters->baud = baud;
  return gioReconfigureResource(brl->gioEndpoint, parameters);
}

static int
setRemoteBaud (BrailleDisplay *brl, int baud) {
  unsigned char request[] = {0xFF, 0xFF, 0x05, 0};
  unsigned char *byte = &request[sizeof(request) - 1];

  switch (baud) {
    case TS_BAUD_LOW:
      *byte = 2;
      break;

    case TS_BAUD_NORMAL:
      *byte = 3;
      break;

    case TS_BAUD_HIGH:
      *byte = 4;
      break;

    default:
      logMessage(LOG_WARNING, "display does not support %d baud", baud);
      return 0;
  }

  logMessage(LOG_WARNING, "changing display to %d baud", baud);
  return writeBraillePacket(brl, NULL, request, sizeof(request));
}

static int
connectResource (BrailleDisplay *brl, const char *identifier) {
  static const SerialParameters serialParameters = {
    SERIAL_DEFAULT_PARAMETERS
  };

  GioDescriptor descriptor;
  gioInitializeDescriptor(&descriptor);

  descriptor.serial.parameters = &serialParameters;

  if (connectBrailleResource(brl, identifier, &descriptor, NULL)) {
    brl->data->serialParameters = serialParameters;
    return 1;
  }

  return 0;
}

static void
disconnectResource (BrailleDisplay *brl) {
  disconnectBrailleResource(brl, NULL);
}

static int
brl_construct (BrailleDisplay *brl, char **parameters, const char *device) {
  InputPacket reply;
  unsigned int allowHighBaud = 1;

  {
    const char *parameter = parameters[PARM_HIGHBAUD];

    if (parameter && *parameter) {
      if (!validateYesNo(&allowHighBaud, parameter)) {
        logMessage(LOG_WARNING, "unsupported high baud setting: %s", parameter);
      }
    }
  }

  dispbuf = prevdata = NULL;

  if ((brl->data = malloc(sizeof(*brl->data)))) {
    memset(brl->data, 0, sizeof(*brl->data));

    if (connectResource(brl, device)) {
      if (!setLocalBaud(brl, TS_BAUD_NORMAL)) goto failure;

      if (!getIdentity(brl, &reply)) {
        /* Then send the query at 19200 baud, in case a PB was left ON
         * at that speed
         */

        if (!allowHighBaud) goto failure;
        if (!setLocalBaud(brl, TS_BAUD_HIGH)) goto failure;
        if (!getIdentity(brl, &reply)) goto failure;
      }

      memcpy(brl->data->hardwareVersion,
             &reply.fields.identity.version[1],
             sizeof(brl->data->hardwareVersion));

      brl->data->totalCells = reply.fields.identity.columns;
      brl->data->textCells = brl->data->totalCells;

      logMessage(LOG_INFO, "display replied: %d cells, version %.*s",
                 brl->data->totalCells,
                 (int)sizeof(brl->data->hardwareVersion),
                 brl->data->hardwareVersion);

      switch (brl->data->textCells) {
        case 20:
          brl->data->model = &modelNavigator20;
          break;

        case 40:
          brl->data->model = (brl->data->hardwareVersion[0] > '3')?
                               &modelPowerBraille40:
                               &modelNavigator40;
          break;

        case 80:
          brl->data->model = &modelNavigator80;
          break;

        case 65:
          brl->data->model = &modelPowerBraille65;
          break;

        case 81:
          brl->data->model = &modelPowerBraille80;
          break;

        default:
          logMessage(LOG_ERR, "unrecognized braille display size: %u", brl->data->textCells);
          goto failure;
      }

      logMessage(LOG_INFO, "detected %s", brl->data->model->modelName);

      brl->data->slowUpdate = brl->data->model->slowUpdate;
      brl->data->noMultipleUpdates = 0;

#ifdef FORCE_DRAIN_AFTER_SEND
      brl->data->slowUpdate = 1;
#endif /* FORCE_DRAIN_AFTER_SEND */

#ifdef FORCE_FULL_SEND_DELAY
      brl->data->slowUpdate = 2;
#endif /* FORCE_FULL_SEND_DELAY */

#ifdef NO_MULTIPLE_UPDATES
      brl->data->noMultipleUpdates = 1;
#endif /* NO_MULTIPLE_UPDATES */

      if (brl->data->slowUpdate == 2) brl->data->noMultipleUpdates = 1;
      brl->data->fullFreshenEvery = FULL_FRESHEN_EVERY;

      if ((brl->data->serialParameters.baud < TS_BAUD_HIGH) && allowHighBaud && brl->data->model->highBaudSupported) {
        /* if supported (PB) go to 19200 baud */
        if (!setRemoteBaud(brl, TS_BAUD_HIGH)) goto failure;
      //serialAwaitOutput(brl->gioEndpoint);
        asyncWait(BAUD_DELAY);
        if (!setLocalBaud(brl, TS_BAUD_HIGH)) goto failure;
        logMessage(LOG_DEBUG, "switched to %d baud - checking if display followed", TS_BAUD_HIGH);

        if (getIdentity(brl, &reply)) {
          logMessage(LOG_DEBUG, "display responded at %d baud", TS_BAUD_HIGH);
        } else {
          logMessage(LOG_INFO,
                     "display did not respond at %d baud"
                     " - falling back to %d baud", TS_BAUD_NORMAL,
                     TS_BAUD_HIGH);

          if (!setLocalBaud(brl, TS_BAUD_NORMAL)) goto failure;
        //serialAwaitOutput(brl->gioEndpoint);
          asyncWait(BAUD_DELAY); /* just to be safe */

          if (getIdentity(brl, &reply)) {
            logMessage(LOG_INFO,
                       "found display again at %d baud"
                       " - must be a TSI emulator",
                       TS_BAUD_NORMAL);

            brl->data->fullFreshenEvery = 1;
          } else {
            logMessage(LOG_ERR, "display lost after baud switch");
            goto failure;
          }
        }
      }

      {
        const KeyTableDefinition *ktd = brl->data->model->keyTableDefinition;

        brl->keyBindings = ktd->bindings;
        brl->keyNames = ktd->names;
      }

      brl->textColumns = brl->data->textCells;		/* initialise size of display */
      brl->setAutorepeat = setAutorepeat;

      makeOutputTable(dotsTable_ISO11548_1);
      memset(brl->data->routingKeys, 0, sizeof(brl->data->routingKeys));

      /* Allocate space for buffers */
      dispbuf = malloc(brl->data->totalCells);
      prevdata = malloc(brl->data->totalCells);
      /* 2* to insert 0s for attribute code when sending to the display */
      if (!dispbuf || !prevdata)
        goto failure;

      /* Force rewrite of display on first writebrl */
      memset(prevdata, 0xFF, brl->data->totalCells);

      return 1;

    failure:
      disconnectResource(brl);
    }

    free(brl->data);
  } else {
    logMallocError();
  }

  return 0;
}

static void 
brl_destruct (BrailleDisplay *brl) {
  disconnectResource(brl);

  if (brl->data) {
    free(brl->data);
    brl->data = NULL;
  }

  if (dispbuf) {
    free(dispbuf);
    dispbuf = NULL;
  }

  if (prevdata) {
    free(prevdata);
    prevdata = NULL;
  }
}

static int
writeCells (
  BrailleDisplay *brl, const unsigned char *cells,
  unsigned int from, unsigned int to
) {
  static const unsigned char header[] = {
    0XFF, 0XFF, 0X04, 0X00, 0X99, 0X00
  };

  unsigned int length = to - from;
  unsigned char packet[sizeof(header) + 2 + (length * 2)];
  unsigned char *byte = packet;
  unsigned int i;

  byte = mempcpy(byte, header, sizeof(header));
  *byte++ = 2 * length;
  *byte++ = from;

  for (i=0; i<length; i+=1) {
    *byte++ = 0;
    *byte++ = translateOutputCell(cells[from + i]);
  }

  /* Some displays apparently don't like rapid updating. Most or all apprently
   * don't do flow control. If we update the display too often and too fast,
   * then the packets queue up in the send queue, the info displayed is not up
   * to date, and the info displayed continues to change after we stop
   * updating while the queue empties (like when you release the arrow key and
   * the display continues changing for a second or two). We also risk
   * overflows which put garbage on the display, or often what happens is that
   * some cells from previously displayed lines will remain and not be cleared
   * or replaced; also the pinging fails and the display gets
   * reinitialized... To expose the problem skim/scroll through a long file
   * (with long lines) holding down the up/down arrow key on the PC keyboard.
   *
   * pb40 has no problems: it apparently can take whatever we throw at
   * it. Nav40 is good but we drain just to be safe.
   *
   * pb80 (twice larger but twice as fast as nav40) cannot take a continuous
   * full speed flow. There is no flow control: apparently not supported
   * properly on at least pb80. My pb80 is recent yet the hardware version is
   * v1.0a, so this may be a hardware problem that was fixed on pb40.  There's
   * some UART handshake mode that might be relevant but only seems to break
   * everything (on both pb40 and pb80)...
   *
   * Nav80 is untested but as it receives at 9600, we probably need to
   * compensate there too.
   *
   * Finally, some TSI emulators (at least the mdv mb408s) may have timing
   * limitations.
   *
   * I no longer have access to a Nav40 and PB80 for testing: I only have a
   * PB40.
   */

  return writeBytes(brl, packet, (byte - packet));
}

static void 
writeAllCells (BrailleDisplay *brl, const unsigned char *cells) {
  writeCells(brl, cells, 0, brl->data->totalCells);
}

static int 
brl_writeWindow (BrailleDisplay *brl, const wchar_t *text) {
  static int count = 0;

  memcpy(dispbuf, brl->buffer, brl->data->textCells);

  if (--count<=0) {
    /* Force an update of the whole display every now and then to clear any
       garble. */
    count = brl->data->fullFreshenEvery;
    memcpy(prevdata, dispbuf, brl->data->totalCells);
    writeAllCells(brl, dispbuf);
  } else if (brl->data->noMultipleUpdates) {
    unsigned int from, to;
    
    if (cellsHaveChanged(prevdata, dispbuf, brl->data->totalCells, &from, &to, NULL)) {
      writeCells(brl, dispbuf, from, to);
    }
  }else{
    int base = 0, i = 0, collecting = 0, simil = 0;
    
    while (i < brl->data->totalCells)
      if (dispbuf[i] == prevdata[i])
	{
	  simil++;
	  if (collecting && 2 * simil > DIM_BRL_SEND)
	    {
	      writeCells(brl, dispbuf, base, i - simil + 1);
	      base = i;
	      collecting = 0;
	      simil = 0;
	    }
	  if (!collecting)
	    base++;
	  i++;
	}
      else
	{
	  prevdata[i] = dispbuf[i];
	  collecting = 1;
	  simil = 0;
	  i++;
	}
    
    if (collecting)
      writeCells(brl, dispbuf, base, i - simil );
  }
return 1;
}

static int
handleInputPacket (BrailleDisplay *brl, const InputPacket *packet) {
  switch (packet->type) {
    case IPT_KEYS: {
      KeyNumberSet keys = 0;
      unsigned int i;

      for (i=0; i<packet->data.keys.count; i+=1) {
        const KeysByteDescriptor *kbd = &packet->data.keys.descriptor[i];

        keys |= (packet->fields.keys[i] & kbd->mask) << kbd->shift;
      }

      enqueueKeys(brl, keys, TS_GRP_NavigationKeys, 0);
      break;
    }

    case IPT_ROUTING: {
      if (packet->data.routing.count != brl->data->model->routingBytes) return 0;
      enqueueUpdatedKeyGroup(brl, brl->data->model->routingKeyCount,
                             packet->fields.routing.horizontal,
                             brl->data->routingKeys,
                             TS_GRP_RoutingKeys);
      break;
    }

    case IPT_BATTERY:
      message(NULL, gettext("battery low"), MSG_WAITKEY);
      return 1;

    default:
      return 0;
  }

  return 1;
}

static int 
brl_readCommand (BrailleDisplay *brl, KeyTableCommandContext context) {
  /* Key press codes come in pairs of bytes for nav and pb40, in 6bytes
   * for pb65/80. Each byte has bits representing individual keys + a special
   * mask/signature in the most significant 3bits.
   *
   * The low battery warning from the display is a specific 2bytes code.
   *
   * Finally, the routing keys have a special 2bytes header followed by 9, 14
   * or 15 bytes of info (1bit for each routing key). The first 4bytes describe
   * vertical routing keys and are ignored in this driver.
   *
   * We might get a query reply, since we send queries when we don't get
   * any keys in a certain time. That a 2byte header + 10 more bytes ignored.
   */

  InputPacket packet;
  size_t size;

  while ((size = readPacket(brl, &packet))) {
    if (!handleInputPacket(brl, &packet)) {
      logUnexpectedPacket(packet.fields.bytes, size);
    }
  }

  return (errno == EAGAIN)? EOF: BRL_CMD_RESTARTBRL;
}
