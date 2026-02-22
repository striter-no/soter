#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#ifndef SOTER_LOGGING
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4
} logger_type;

typedef struct {
    int ofd;
    logger_type min_level;
    int use_colors;
    int use_timestamp;
} slogger;

slogger logger_init(int output_fd);

void logger_set_level(slogger *l, logger_type level);
void logger_set_colors(slogger *l, int enable);
void logger_set_timestamp(slogger *l, int enable);
void slog(slogger *l, logger_type t, const char *format, ...);

#define LOG_DEBUG(l, ...)  slog(l, LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(l, ...)   slog(l, LOG_INFO, __VA_ARGS__)
#define LOG_WARNING(l, ...) slog(l, LOG_WARNING, __VA_ARGS__)
#define LOG_ERROR(l, ...)  slog(l, LOG_ERROR, __VA_ARGS__)
#define LOG_FATAL(l, ...)  slog(l, LOG_FATAL, __VA_ARGS__)

void logger_stop(slogger *logger);
static const char *level_names[] = {
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
    "FATAL"
};

static const char *level_colors[] = {
    "\033[36m",  // DEBUG - cyan
    "\033[32m",  // INFO - green
    "\033[33m",  // WARNING - yellow
    "\033[31m",  // ERROR - red
    "\033[35m"   // FATAL - magenta
};

static const char *color_reset = "\033[0m";

slogger logger_init(int output_fd) {
    slogger logger;
    logger.ofd = output_fd;
    logger.min_level = LOG_DEBUG;
    logger.use_colors = isatty(output_fd);
    logger.use_timestamp = 1;
    return logger;
}

void logger_set_level(slogger *l, logger_type level) {
    if (l) {
        l->min_level = level;
    }
}

void logger_set_colors(slogger *l, int enable) {
    if (l) {
        l->use_colors = enable;
    }
}

void logger_set_timestamp(slogger *l, int enable) {
    if (l) {
        l->use_timestamp = enable;
    }
}

static void write_timestamp(int fd) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    write(fd, buffer, strlen(buffer));
    write(fd, " ", 1);
}

void slog(slogger *l, logger_type t, const char *format, ...) {
    if (!l || t < l->min_level) {
        return;
    }

    char buffer[4096];
    int offset = 0;

    if (l->use_timestamp) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_buffer[32];
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "[%s] ", time_buffer);
    }

    if (l->use_colors) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                          "%s%-7s%s", level_colors[t], level_names[t], color_reset);
    } else {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                          "%-7s", level_names[t]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, ": ");

    va_list args;
    va_start(args, format);
    offset += vsnprintf(buffer + offset, sizeof(buffer) - offset, format, args);
    va_end(args);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");

    write(l->ofd, buffer, strlen(buffer));

    if (t == LOG_FATAL) {
        fflush(NULL);
        _exit(1);
    }
}

void logger_stop(slogger *logger) {
    if (logger && logger->ofd != STDOUT_FILENO && logger->ofd != STDERR_FILENO) {
        close(logger->ofd);
    }
}

slogger SOTER_LOGER;
#define SLOG_DEBUG(...)  slog(&SOTER_LOGER, LOG_DEBUG, __VA_ARGS__)
#define SLOG_INFO(...)   slog(&SOTER_LOGER, LOG_INFO, __VA_ARGS__)
#define SLOG_WARNING(...) slog(&SOTER_LOGER, LOG_WARNING, __VA_ARGS__)
#define SLOG_ERROR(...)  slog(&SOTER_LOGER, LOG_ERROR, __VA_ARGS__)
#define SLOG_FATAL(...)  slog(&SOTER_LOGER, LOG_FATAL, __VA_ARGS__)

#endif
#define SOTER_LOGGING