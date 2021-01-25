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

#include "prologue.h"

#include <string.h>
#include <errno.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "log.h"
#include "messages.h"
#include "file.h"

// MinGW doesn't define LC_MESSAGES
#ifndef LC_MESSAGES
#define LC_MESSAGES LC_ALL
#endif /* LC_MESSAGES */

// Windows needs O_BINARY
#ifndef O_BINARY
#define O_BINARY 0
#endif /* O_BINARY */

static char *messagesLocale = NULL;
static char *messagesDomain = NULL;
static char *messagesDirectory = NULL;

const char *
getMessagesLocale (void) {
  return messagesLocale;
}

const char *
getMessagesDomain (void) {
  return messagesDomain;
}

const char *
getMessagesDirectory (void) {
  return messagesDirectory;
}

static const uint32_t magicNumber = UINT32_C(0X950412DE);
typedef uint32_t GetIntegerFunction (uint32_t value);

typedef struct {
  uint32_t magicNumber;
  uint32_t versionNumber;
  uint32_t stringCount;
  uint32_t originalStrings;
  uint32_t translatedStrings;
  uint32_t hashSize;
  uint32_t hashOffset;
} MessagesHeader;

typedef struct {
  union {
    void *area;
    const unsigned char *bytes;
    const MessagesHeader *header;
  } view;

  size_t areaSize;
  GetIntegerFunction *getInteger;
} MessagesData;

static MessagesData messagesData = {
  .view.area = NULL,
  .areaSize = 0,
  .getInteger = NULL,
};

static uint32_t
getNativeInteger (uint32_t value) {
  return value;
}

static uint32_t
getFlippedInteger (uint32_t value) {
  uint32_t result = 0;

  while (value) {
    result <<= 8;
    result |= value & UINT8_MAX;
    value >>= 8;
  }

  return result;
}

static int
checkMagicNumber (MessagesData *data) {
  const MessagesHeader *header = data->view.header;

  {
    static GetIntegerFunction *const functions[] = {
      getNativeInteger,
      getFlippedInteger,
      NULL
    };

    GetIntegerFunction *const *function = functions;

    while (*function) {
      if ((*function)(header->magicNumber) == magicNumber) {
        data->getInteger = *function;
        return 1;
      }

      function += 1;
    }
  }

  return 0;
}

static char *
makeLocalesPath (void) {
  if (messagesLocale && messagesDomain && messagesDirectory) {
    size_t length = strlen(messagesLocale);

    char dialect[length + 1];
    strcpy(dialect, messagesLocale);
    length = strcspn(dialect, ".@");
    dialect[length] = 0;

    char language[length + 1];
    strcpy(language, dialect);
    length = strcspn(language, "_");
    language[length] = 0;

    char *names[] = {dialect, language, NULL};
    char **name = names;

    while (*name) {
      char *path = makePath(messagesDirectory, *name);

      if (path) {
        if (testDirectoryPath(path)) return path;
        free(path);
      }

      name += 1;
    }
  }

  return NULL;
}

static char *
makeDataPath (void) {
  char *locales = makeLocalesPath();

  if (locales) {
    char *category = makePath(locales, "LC_MESSAGES");

    free(locales);
    locales = NULL;

    if (category) {
      char *file = makeFilePath(category, messagesDomain, ".mo");

      free(category);
      category = NULL;

      if (file) return file;
    }
  }

  return NULL;
}

int
loadMessagesData (void) {
  if (messagesData.view.area) return 1;

  int loaded = 0;
  char *path = makeDataPath();

  if (path) {
    int fd = open(path, (O_RDONLY | O_BINARY));

    if (fd != -1) {
      struct stat info;

      if (fstat(fd, &info) != -1) {
        size_t size = info.st_size;
        void *area = NULL;

        if (size) {
          if ((area = malloc(size))) {
            ssize_t count = read(fd, area, size);

            if (count == -1) {
              logMessage(LOG_WARNING, "messages data read error: %s: %s", path, strerror(errno));
            } else if (count < size) {
              logMessage(LOG_WARNING,
                "truncated messages data: %"PRIssize" < %"PRIsize": %s",
                count, size, path
              );
            } else {
              MessagesData data = {
                .view.area = area,
                .areaSize = size
              };

              if (checkMagicNumber(&data)) {
                messagesData = data;
                loaded = 1;
              }
            }

            if (!loaded) free(area);
          } else {
            logMallocError();
          }
        }
      } else {
        logMessage(LOG_WARNING, "messages file stat error: %s: %s", path, strerror(errno));
      }

      close(fd);
    } else {
      logMessage(LOG_WARNING, "messages file open error: %s: %s", path, strerror(errno));
    }

    free(path);
  }

  return loaded;
}

void
releaseMessagesData (void) {
  if (messagesData.view.area) {
    free(messagesData.view.area);
    messagesData.view.area = NULL;
  }

  messagesData.areaSize = 0;
  messagesData.getInteger = NULL;
}

static inline const MessagesHeader *
getHeader (void) {
  return messagesData.view.header;
}

static inline const void *
getItem (uint32_t offset) {
  return &messagesData.view.bytes[messagesData.getInteger(offset)];
}

uint32_t
getStringCount (void) {
  return messagesData.getInteger(getHeader()->stringCount);
}

struct MessagesStringStruct {
  uint32_t length;
  uint32_t offset;
};

uint32_t
getStringLength (const MessagesString *string) {
  return messagesData.getInteger(string->length);
}

const char *
getStringText (const MessagesString *string) {
  return getItem(string->offset);
}

static inline const MessagesString *
getOriginalStrings (void) {
  return getItem(getHeader()->originalStrings);
}

const MessagesString *
getOriginalString (unsigned int index) {
  return &getOriginalStrings()[index];
}

static inline const MessagesString *
getTranslatedStrings (void) {
  return getItem(getHeader()->translatedStrings);
}

const MessagesString *
getTranslatedString (unsigned int index) {
  return &getTranslatedStrings()[index];
}

int
findOriginalString (const char *text, size_t textLength, unsigned int *index) {
  const MessagesString *strings = getOriginalStrings();
  int from = 0;
  int to = getStringCount();

  while (from < to) {
    int current = (from + to) / 2;
    const MessagesString *string = &strings[current];

    uint32_t stringLength = getStringLength(string);
    int relation = memcmp(text, getStringText(string), MIN(textLength, stringLength));

    if (relation == 0) {
      if (textLength == stringLength) {
        *index = current;
        return 1;
      }

      relation = (textLength < stringLength)? -1: 1;
    }

    if (relation < 0) {
      to = current;
    } else {
      from = current + 1;
    }
  }

  return 0;
}

const MessagesString *
findBasicTranslation (const char *text, size_t length) {
  if (!text) return NULL;
  if (!length) return NULL;

  if (loadMessagesData()) {
    unsigned int index;

    if (findOriginalString(text, length, &index)) {
      return getTranslatedString(index);
    }
  }

  return NULL;
}

const char *
getBasicTranslation (const char *text) {
  const MessagesString *translation = findBasicTranslation(text, strlen(text));
  if (translation) return getStringText(translation);
  return text;
}

const MessagesString *
findPluralTranslation (const char *const *strings) {
  unsigned int count = 0;
  while (strings[count]) count += 1;
  if (!count) return NULL;

  size_t size = 0;
  size_t lengths[count];

  for (unsigned int index=0; index<count; index+=1) {
    size_t length = strlen(strings[index]);
    lengths[index] = length;
    size += length + 1;
  }

  char text[size];
  char *byte = text;

  for (unsigned int index=0; index<count; index+=1) {
    byte = mempcpy(byte, strings[index], (lengths[index] + 1));
  }

  byte -= 1; // the length mustn't include the final NUL
  return findBasicTranslation(text, (byte - text));
}

const char *
getPluralTranslation (const char *singular, const char *plural, unsigned long int count) {
  int useSingular = count == 1;

  const char *const strings[] = {singular, plural, NULL};
  const MessagesString *string = findPluralTranslation(strings);
  if (!string) return useSingular? singular: plural;

  const char *translation = getStringText(string);
  if (!useSingular) translation += strlen(translation) + 1;
  return translation;
}

#ifdef ENABLE_I18N_SUPPORT
static int
setDomain (const char *domain) {
  if (!textdomain(domain)) {
    logSystemError("textdomain");
    return 0;
  }

  if (!bind_textdomain_codeset(domain, "UTF-8")) {
    logSystemError("bind_textdomain_codeset");
  }

  return 1;
}

static int
setDirectory (const char *directory) {
  if (bindtextdomain(messagesDomain, directory)) return 1;
  logSystemError("bindtextdomain");
  return 0;
}
#else /* ENABLE_I18N_SUPPORT */
static int
setDomain (const char *domain) {
  return 1;
}

static int
setDirectory (const char *directory) {
  return 1;
}

char *
gettext (const char *text) {
  return (char *)getBasicTranslation(text);
}

char *
ngettext (const char *singular, const char *plural, unsigned long int count) {
  return (char *)getPluralTranslation(singular, plural, count);
}
#endif /* ENABLE_I18N_SUPPORT */

static int
updateProperty (
  char **property, const char *value, const char *defaultValue,
  int (*setter) (const char *value)
) {
  if (!(value && *value)) value = defaultValue;
  char *copy = strdup(value);

  if (copy) {
    if (!setter || setter(value)) {
      if (*property) free(*property);
      *property = copy;
      return 1;
    }

    free(copy);
  } else {
    logMallocError();
  }

  return 0;
}

int
setMessagesLocale (const char *locale) {
  releaseMessagesData();
  return updateProperty(&messagesLocale, locale, "C.UTF-8", NULL);
}

int
setMessagesDomain (const char *domain) {
  releaseMessagesData();
  return updateProperty(&messagesDomain, domain, PACKAGE_TARNAME, setDomain);
}

int
setMessagesDirectory (const char *directory) {
  releaseMessagesData();
  return updateProperty(&messagesDirectory, directory, LOCALE_DIRECTORY, setDirectory);
}

void
ensureAllMessagesProperties (void) {
  if (!messagesLocale) {
    setMessagesLocale(setlocale(LC_MESSAGES, ""));
  }

  if (!messagesDomain) setMessagesDomain(NULL);
  if (!messagesDirectory) setMessagesDirectory(NULL);
}
