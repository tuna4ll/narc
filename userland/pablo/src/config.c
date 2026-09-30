#include <dirent.h>
#include <errno.h>
#include <pablo/config.h>
#include <pablo/io.h>
#include <pablo/limits.h>
#include <pablo/log.h>
#include <pablo/text.h>
#include <stdio.h>
#include <string.h>

static int split_setting(char *line, char **key, char **value) {
    char *separator = strchr(line, '=');
    if (!separator) return -1;
    *separator = 0;
    *key = pablo_trim(line);
    *value = pablo_trim(separator + 1);
    return **key && **value ? 0 : -1;
}

static int next_line(char **cursor, char **line) {
    if (!**cursor) return 0;
    *line = *cursor;
    char *end = strchr(*cursor, '\n');
    if (end) {
        *end = 0;
        *cursor = end + 1;
    } else {
        *cursor += strlen(*cursor);
    }
    return 1;
}

static int load_settings(struct pablo_config *config, const char *path) {
    char buffer[PABLO_FILE_MAX];
    size_t length;
    if (pablo_read_file(path, buffer, sizeof(buffer), &length) != 0) {
        if (errno == ENOENT) return 0;
        return -1;
    }
    (void)length;
    char *cursor = buffer;
    char *line;
    while (next_line(&cursor, &line)) {
        line = pablo_trim(line);
        if (!*line || *line == '#') continue;
        char *key, *value;
        unsigned int number;
        if (split_setting(line, &key, &value) != 0 ||
            pablo_parse_unsigned(value, &number) != 0)
            return -1;
        if (!strcmp(key, "restart_limit")) config->restart_limit = number;
        else if (!strcmp(key, "restart_delay")) config->restart_delay = number;
        else return -1;
    }
    return 0;
}

static int service_name(char *destination, const char *filename) {
    static const char suffix[] = ".service";
    if (!pablo_has_suffix(filename, suffix)) return -1;
    size_t length = strlen(filename) - (sizeof(suffix) - 1);
    if (!length || length >= PABLO_NAME_MAX) return -1;
    memcpy(destination, filename, length);
    destination[length] = 0;
    return 0;
}

static int parse_service_line(struct pablo_service *service, char *line) {
    char *key, *value;
    if (split_setting(line, &key, &value) != 0) return -1;
    if (!strcmp(key, "exec")) return pablo_copy(service->path, sizeof(service->path), value);
    if (!strcmp(key, "after")) return pablo_copy(service->after, sizeof(service->after), value);
    if (!strcmp(key, "policy")) {
        if (!strcmp(value, "once")) service->policy = PABLO_ONCE;
        else if (!strcmp(value, "respawn")) service->policy = PABLO_RESPAWN;
        else return -1;
        return 0;
    }
    if (!strcmp(key, "arg")) {
        if (service->argument_count >= PABLO_ARG_MAX) return -1;
        return pablo_copy(service->arguments[service->argument_count++],
                          PABLO_ARG_LENGTH, value);
    }
    return -1;
}

static int load_service(struct pablo_service *service, const char *directory,
                        const char *filename) {
    memset(service, 0, sizeof(*service));
    if (service_name(service->name, filename) != 0) return -1;
    char path[PABLO_PATH_MAX];
    size_t directory_length = strlen(directory);
    size_t filename_length = strlen(filename);
    if (directory_length + filename_length + 2 > sizeof(path)) return -1;
    memcpy(path, directory, directory_length);
    path[directory_length] = '/';
    memcpy(path + directory_length + 1, filename, filename_length + 1);

    char buffer[PABLO_FILE_MAX];
    size_t length;
    if (pablo_read_file(path, buffer, sizeof(buffer), &length) != 0) return -1;
    (void)length;
    char *cursor = buffer;
    char *line;
    while (next_line(&cursor, &line)) {
        line = pablo_trim(line);
        if (!*line || *line == '#') continue;
        if (parse_service_line(service, line) != 0) return -1;
    }
    return service->path[0] ? 0 : -1;
}

static void sort_names(char names[PABLO_SERVICE_MAX][PABLO_SERVICE_FILE_MAX], size_t count) {
    for (size_t i = 1; i < count; i++) {
        char current[PABLO_SERVICE_FILE_MAX];
        memcpy(current, names[i], sizeof(current));
        size_t position = i;
        while (position && strcmp(names[position - 1], current) > 0) {
            memcpy(names[position], names[position - 1], PABLO_SERVICE_FILE_MAX);
            position--;
        }
        memcpy(names[position], current, PABLO_SERVICE_FILE_MAX);
    }
}

static int load_services(struct pablo_config *config, const char *directory) {
    DIR *stream = opendir(directory);
    if (!stream) return errno == ENOENT ? 0 : -1;
    char names[PABLO_SERVICE_MAX][PABLO_SERVICE_FILE_MAX];
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(stream))) {
        if (entry->d_name[0] == '.' || !pablo_has_suffix(entry->d_name, ".service"))
            continue;
        if (count >= PABLO_SERVICE_MAX ||
            pablo_copy(names[count], sizeof(names[count]), entry->d_name) != 0) {
            closedir(stream);
            return -1;
        }
        count++;
    }
    if (closedir(stream) != 0) return -1;
    sort_names(names, count);
    for (size_t i = 0; i < count; i++) {
        if (load_service(&config->services[config->service_count], directory,
                         names[i]) != 0)
            return -1;
        config->service_count++;
    }
    return 0;
}

void pablo_config_defaults(struct pablo_config *config) {
    memset(config, 0, sizeof(*config));
    config->restart_limit = 5;
    config->restart_delay = 64;
}

int pablo_config_load(struct pablo_config *config, const char *settings_path,
                      const char *services_path) {
    pablo_config_defaults(config);
    if (load_settings(config, settings_path) != 0) {
        pablo_log_service(PABLO_LOG_ERROR, settings_path, "invalid config");
        return -1;
    }
    if (load_services(config, services_path) != 0) {
        pablo_log_service(PABLO_LOG_ERROR, services_path, "invalid service");
        return -1;
    }
    return pablo_service_validate(config);
}
