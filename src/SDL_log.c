/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "./SDL_internal.h"

#if defined(__WIN32__) || defined(__WINRT__) || defined(__GDK__)
#include "core/windows/SDL_windows.h"
#endif

/* Simple log messages in SDL */

#include "SDL_error.h"
#include "SDL_log.h"
#include "SDL_mutex.h"
#include "SDL_log_c.h"

#if HAVE_STDIO_H
#include <stdio.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(__WIIU__)
#include <coreinit/debug.h>
#endif

#include "stdlib/SDL_vacopy.h"

/* The size of the stack buffer to use for rendering log messages. */
#define SDL_MAX_LOG_MESSAGE_STACK 256

#define DEFAULT_PRIORITY             SDL_LOG_PRIORITY_CRITICAL
#define DEFAULT_ASSERT_PRIORITY      SDL_LOG_PRIORITY_WARN
#define DEFAULT_APPLICATION_PRIORITY SDL_LOG_PRIORITY_INFO
#define DEFAULT_TEST_PRIORITY        SDL_LOG_PRIORITY_VERBOSE

typedef struct SDL_LogLevel
{
    int category;
    SDL_LogPriority priority;
    struct SDL_LogLevel *next;
} SDL_LogLevel;

/* The default log output function */
static void SDLCALL SDL_LogOutput(void *userdata, int category, SDL_LogPriority priority, const char *message);

static SDL_LogLevel *SDL_loglevels;
static SDL_LogPriority SDL_default_priority = DEFAULT_PRIORITY;
static SDL_LogPriority SDL_assert_priority = DEFAULT_ASSERT_PRIORITY;
static SDL_LogPriority SDL_application_priority = DEFAULT_APPLICATION_PRIORITY;
static SDL_LogPriority SDL_test_priority = DEFAULT_TEST_PRIORITY;
static SDL_LogOutputFunction SDL_log_function = SDL_LogOutput;
static void *SDL_log_userdata = NULL;
static SDL_mutex *log_function_mutex = NULL;

static const char *SDL_priority_prefixes[SDL_NUM_LOG_PRIORITIES] = {
    NULL,
    "VERBOSE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "CRITICAL"
};

#ifdef __ANDROID__
static const char *SDL_category_prefixes[] = {
    "APP",
    "ERROR",
    "ASSERT",
    "SYSTEM",
    "AUDIO",
    "VIDEO",
    "RENDER",
    "INPUT",
    "TEST"
};

SDL_COMPILE_TIME_ASSERT(category_prefixes_enum, SDL_TABLESIZE(SDL_category_prefixes) == SDL_LOG_CATEGORY_RESERVED1);

static int SDL_android_priority[SDL_NUM_LOG_PRIORITIES] = {
    ANDROID_LOG_UNKNOWN,
    ANDROID_LOG_VERBOSE,
    ANDROID_LOG_DEBUG,
    ANDROID_LOG_INFO,
    ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR,
    ANDROID_LOG_FATAL
};
#endif /* __ANDROID__ */

void SDL_LogInit(void)
{
    if (log_function_mutex == NULL) {
        /* if this fails we'll try to continue without it. */
        log_function_mutex = SDL_CreateMutex();
    }
}

void SDL_LogQuit(void)
{
    SDL_LogResetPriorities();
    if (log_function_mutex) {
        SDL_DestroyMutex(log_function_mutex);
        log_function_mutex = NULL;
    }
}

void SDL_LogSetAllPriority(SDL_LogPriority priority)
{
    SDL_LogLevel *entry;

    for (entry = SDL_loglevels; entry; entry = entry->next) {
        entry->priority = priority;
    }
    SDL_default_priority = priority;
    SDL_assert_priority = priority;
    SDL_application_priority = priority;
}

void SDL_LogSetPriority(int category, SDL_LogPriority priority)
{
    SDL_LogLevel *entry;

    for (entry = SDL_loglevels; entry; entry = entry->next) {
        if (entry->category == category) {
            entry->priority = priority;
            return;
        }
    }

    /* Create a new entry */
    entry = (SDL_LogLevel *)SDL_malloc(sizeof(*entry));
    if (entry) {
        entry->category = category;
        entry->priority = priority;
        entry->next = SDL_loglevels;
        SDL_loglevels = entry;
    }
}

SDL_LogPriority SDL_LogGetPriority(int category)
{
    SDL_LogLevel *entry;

    for (entry = SDL_loglevels; entry; entry = entry->next) {
        if (entry->category == category) {
            return entry->priority;
        }
    }

    if (category == SDL_LOG_CATEGORY_TEST) {
        return SDL_test_priority;
    } else if (category == SDL_LOG_CATEGORY_APPLICATION) {
        return SDL_application_priority;
    } else if (category == SDL_LOG_CATEGORY_ASSERT) {
        return SDL_assert_priority;
    } else {
        return SDL_default_priority;
    }
}

void SDL_LogResetPriorities(void)
{
    SDL_LogLevel *entry;

    while (SDL_loglevels) {
        entry = SDL_loglevels;
        SDL_loglevels = entry->next;
        SDL_free(entry);
    }

    SDL_default_priority = DEFAULT_PRIORITY;
    SDL_assert_priority = DEFAULT_ASSERT_PRIORITY;
    SDL_application_priority = DEFAULT_APPLICATION_PRIORITY;
    SDL_test_priority = DEFAULT_TEST_PRIORITY;
}

void SDL_Log(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

void SDL_LogVerbose(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    SDL_LogMessageV(category, SDL_LOG_PRIORITY_VERBOSE, fmt, ap);
    va_end(ap);
}

void SDL_LogDebug(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    SDL_LogMessageV(category, SDL_LOG_PRIORITY_DEBUG, fmt, ap);
    va_end(ap);
}

void SDL_LogInfo(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    SDL_LogMessageV(category, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

void SDL_LogWarn(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    SDL_LogMessageV(category, SDL_LOG_PRIORITY_WARN, fmt, ap);
    va_end(ap);
}

void SDL_LogError(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    SDL_LogMessageV(category, SDL_LOG_PRIORITY_ERROR, fmt, ap);
    va_end(ap);
}

void SDL_LogCritical(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    SDL_LogMessageV(category, SDL_LOG_PRIORITY_CRITICAL, fmt, ap);
    va_end(ap);
}

void SDL_LogMessage(int category, SDL_LogPriority priority, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    SDL_LogMessageV(category, priority, fmt, ap);
    va_end(ap);
}

#ifdef __ANDROID__
static const char *GetCategoryPrefix(int category)
{
    if (category < SDL_LOG_CATEGORY_RESERVED1) {
        return SDL_category_prefixes[category];
    }
    if (category < SDL_LOG_CATEGORY_CUSTOM) {
        return "RESERVED";
    }
    return "CUSTOM";
}
#endif /* __ANDROID__ */

void SDL_LogMessageV(int category, SDL_LogPriority priority, const char *fmt, va_list ap)
{
    char *message = NULL;
    char stack_buf[SDL_MAX_LOG_MESSAGE_STACK];
    size_t len_plus_term;
    int len;
    va_list aq;

    /* Nothing to do if we don't have an output function */
    if (!SDL_log_function) {
        return;
    }

    /* Make sure we don't exceed array bounds */
    if ((int)priority < 0 || priority >= SDL_NUM_LOG_PRIORITIES) {
        return;
    }

    /* See if we want to do anything with this message */
    if (priority < SDL_LogGetPriority(category)) {
        return;
    }

    if (log_function_mutex == NULL) {
        /* this mutex creation can race if you log from two threads at startup. You should have called SDL_Init first! */
        log_function_mutex = SDL_CreateMutex();
    }

    /* Render into stack buffer */
    va_copy(aq, ap);
    len = SDL_vsnprintf(stack_buf, sizeof(stack_buf), fmt, aq);
    va_end(aq);

    if (len < 0) {
        return;
    }

    /* If message truncated, allocate and re-render */
    if (len >= sizeof(stack_buf) && SDL_size_add_overflow(len, 1, &len_plus_term) == 0) {
        /* Allocate exactly what we need, including the zero-terminator */
        message = (char *)SDL_malloc(len_plus_term);
        if (message == NULL) {
            return;
        }
        va_copy(aq, ap);
        len = SDL_vsnprintf(message, len_plus_term, fmt, aq);
        va_end(aq);
    } else {
        message = stack_buf;
    }

    /* Chop off final endline. */
    if ((len > 0) && (message[len - 1] == '\n')) {
        message[--len] = '\0';
        if ((len > 0) && (message[len - 1] == '\r')) { /* catch "\r\n", too. */
            message[--len] = '\0';
        }
    }

    SDL_LockMutex(log_function_mutex);
    SDL_log_function(SDL_log_userdata, category, priority, message);
    SDL_UnlockMutex(log_function_mutex);

    /* Free only if dynamically allocated */
    if (message != stack_buf) {
        SDL_free(message);
    }
}

#if defined(__WIN32__) && !defined(HAVE_STDIO_H) && !defined(__WINRT__) && !defined(__GDK__)
/* Flag tracking the attachment of the console: 0=unattached, 1=attached to a console, 2=attached to a file, -1=error */
static int consoleAttached = 0;

/* Handle to stderr output of console. */
static HANDLE stderrHandle = NULL;
#endif

static void SDLCALL SDL_LogOutput(void *userdata, int category, SDL_LogPriority priority,
                                  const char *message)
{
#if defined(__WIN32__) || defined(__WINRT__) || defined(__GDK__)
    /* Way too many allocations here, urgh */
    /* Note: One can't call SDL_SetError here, since that function itself logs. */
    {
        char *output;
        size_t length;
        LPTSTR tstr;
        SDL_bool isstack;

#if !defined(HAVE_STDIO_H) && !defined(__WINRT__) && !defined(__GDK__)
        BOOL attachResult;
        DWORD attachError;
        DWORD charsWritten;
        DWORD consoleMode;

        /* Maybe attach console and get stderr handle */
        if (consoleAttached == 0) {
            attachResult = AttachConsole(ATTACH_PARENT_PROCESS);
            if (!attachResult) {
                attachError = GetLastError();
                if (attachError == ERROR_INVALID_HANDLE) {
                    /* This is expected when running from Visual Studio */
                    /*OutputDebugString(TEXT("Parent process has no console\r\n"));*/
                    consoleAttached = -1;
                } else if (attachError == ERROR_GEN_FAILURE) {
                    OutputDebugString(TEXT("Could not attach to console of parent process\r\n"));
                    consoleAttached = -1;
                } else if (attachError == ERROR_ACCESS_DENIED) {
                    /* Already attached */
                    consoleAttached = 1;
                } else {
                    OutputDebugString(TEXT("Error attaching console\r\n"));
                    consoleAttached = -1;
                }
            } else {
                /* Newly attached */
                consoleAttached = 1;
            }

            if (consoleAttached == 1) {
                stderrHandle = GetStdHandle(STD_ERROR_HANDLE);

                if (GetConsoleMode(stderrHandle, &consoleMode) == 0) {
                    /* WriteConsole fails if the output is redirected to a file. Must use WriteFile instead. */
                    consoleAttached = 2;
                }
            }
        }
#endif /* !defined(HAVE_STDIO_H) && !defined(__WINRT__) && !defined(__GDK__) */

        length = SDL_strlen(SDL_priority_prefixes[priority]) + 2 + SDL_strlen(message) + 1 + 1 + 1;
        output = SDL_small_alloc(char, length, &isstack);
        (void)SDL_snprintf(output, length, "%s: %s\r\n", SDL_priority_prefixes[priority], message);
        tstr = WIN_UTF8ToString(output);

        /* Output to debugger */
        OutputDebugString(tstr);

#if !defined(HAVE_STDIO_H) && !defined(__WINRT__) && !defined(__GDK__)
        /* Screen output to stderr, if console was attached. */
        if (consoleAttached == 1) {
            if (!WriteConsole(stderrHandle, tstr, (DWORD)SDL_tcslen(tstr), &charsWritten, NULL)) {
                OutputDebugString(TEXT("Error calling WriteConsole\r\n"));
                if (GetLastError() == ERROR_NOT_ENOUGH_MEMORY) {
                    OutputDebugString(TEXT("Insufficient heap memory to write message\r\n"));
                }
            }

        } else if (consoleAttached == 2) {
            if (!WriteFile(stderrHandle, output, (DWORD)SDL_strlen(output), &charsWritten, NULL)) {
                OutputDebugString(TEXT("Error calling WriteFile\r\n"));
            }
        }
#endif /* !defined(HAVE_STDIO_H) && !defined(__WINRT__) && !defined(__GDK__) */

        SDL_free(tstr);
        SDL_small_free(output, isstack);
    }
#elif defined(__ANDROID__)
    {
        char tag[32];

        SDL_snprintf(tag, SDL_arraysize(tag), "SDL/%s", GetCategoryPrefix(category));
        __android_log_write(SDL_android_priority[priority], tag, message);
    }
#elif defined(__APPLE__) && (defined(SDL_VIDEO_DRIVER_COCOA) || defined(SDL_VIDEO_DRIVER_UIKIT))
    /* Technically we don't need Cocoa/UIKit, but that's where this function is defined for now.
     */
    extern void SDL_NSLog(const char *prefix, const char *text);
    {
        SDL_NSLog(SDL_priority_prefixes[priority], message);
        return;
    }
#elif defined(__WIIU__)
    {
        OSReport("SDL: %s: %s\n", SDL_priority_prefixes[priority], message);
    }
#elif defined(__PSP__) || defined(__PS2__)
    {
        FILE *pFile;
        pFile = fopen("SDL_Log.txt", "a");
        if (pFile != NULL) {
            (void)fprintf(pFile, "%s: %s\n", SDL_priority_prefixes[priority], message);
            (void)fclose(pFile);
        }
    }
#elif defined(__VITA__)
    {
        FILE *pFile;
        pFile = fopen("ux0:/data/SDL_Log.txt", "a");
        if (pFile != NULL) {
            (void)fprintf(pFile, "%s: %s\n", SDL_priority_prefixes[priority], message);
            (void)fclose(pFile);
        }
    }
#elif defined(__3DS__)
    {
        FILE *pFile;
        pFile = fopen("sdmc:/3ds/SDL_Log.txt", "a");
        if (pFile != NULL) {
            (void)fprintf(pFile, "%s: %s\n", SDL_priority_prefixes[priority], message);
            (void)fclose(pFile);
        }
    }
#endif
#if HAVE_STDIO_H && \
    !(defined(__APPLE__) && (defined(SDL_VIDEO_DRIVER_COCOA) || defined(SDL_VIDEO_DRIVER_UIKIT)))
    fprintf(stderr, "%s: %s\n", SDL_priority_prefixes[priority], message);
#if __NACL__
    fflush(stderr);
#endif
#endif
}

void SDL_LogGetOutputFunction(SDL_LogOutputFunction *callback, void **userdata)
{
    if (callback) {
        *callback = SDL_log_function;
    }
    if (userdata) {
        *userdata = SDL_log_userdata;
    }
}

void SDL_LogSetOutputFunction(SDL_LogOutputFunction callback, void *userdata)
{
    SDL_log_function = callback;
    SDL_log_userdata = userdata;
}

/* vi: set ts=4 sw=4 expandtab: */
