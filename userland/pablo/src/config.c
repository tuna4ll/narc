#include <dirent.h>
#include <errno.h>
#include <pablo/config.h>
#include <pablo/io.h>
#include <pablo/log.h>
#include <pablo/text.h>
#include <stdio.h>
#include <stdlib.h>
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
    char *buffer;
    size_t length;
    if (pablo_read_file(path, &buffer, &length) != 0) {
        if (errno == ENOENT) return 0;
        return -1;
    }
    (void)length;
    char *cursor = buffer;
    char *line;
    int status = 0;
    while (!status && next_line(&cursor, &line)) {
        line = pablo_trim(line);
        if (!*line || *line == '#') continue;
        char *key, *value;
        unsigned int number;
        if (split_setting(line, &key, &value) != 0 ||
            pablo_parse_unsigned(value, &number) != 0)
            status = -1;
        else if (!strcmp(key, "restart_limit")) config->restart_limit = number;
        else if (!strcmp(key, "restart_delay")) config->restart_delay = number;
        else status = -1;
    }
    free(buffer);
    return status;
}

static char *service_name(const char *filename) {
    static const char suffix[] = ".service";
    if (!pablo_has_suffix(filename, suffix)) return 0;
    size_t length = strlen(filename) - (sizeof(suffix) - 1);
    return length ? pablo_duplicate(filename, length) : 0;
}

static int replace(char **field, const char *value) {
    char *copy = pablo_duplicate(value, strlen(value));
    if (!copy) return -1;
    free(*field);
    *field = copy;
    return 0;
}

static int append_argument(struct pablo_service *service, const char *value) {
    char **argv = realloc(service->argv, (service->argument_count + 3) * sizeof(*argv));
    if (!argv) return -1;
    service->argv = argv;
    argv[service->argument_count + 1] = pablo_duplicate(value, strlen(value));
    if (!argv[service->argument_count + 1]) return -1;
    service->argument_count++;
    return 0;
}

static int parse_service_line(struct pablo_service *service, char *line) {
    char *key, *value;
    if (split_setting(line, &key, &value) != 0) return -1;
    if (!strcmp(key, "exec")) return replace(&service->path, value);
    if (!strcmp(key, "after")) return replace(&service->after, value);
    if (!strcmp(key, "policy")) {
        if (!strcmp(value, "once")) service->policy = PABLO_ONCE;
        else if (!strcmp(value, "respawn")) service->policy = PABLO_RESPAWN;
        else return -1;
        return 0;
    }
    if (!strcmp(key, "arg")) return append_argument(service, value);
    return -1;
}

static char *join_path(const char *directory, const char *filename) {
    size_t directory_length = strlen(directory);
    size_t filename_length = strlen(filename);
    char *path = malloc(directory_length + filename_length + 2);
    if (!path) return 0;
    memcpy(path, directory, directory_length);
    path[directory_length] = '/';
    memcpy(path + directory_length + 1, filename, filename_length + 1);
    return path;
}

static int load_service(struct pablo_service *service, const char *directory,
                        const char *filename) {
    memset(service, 0, sizeof(*service));
    service->name = service_name(filename);
    char *path = join_path(directory, filename);
    char *buffer = 0;
    size_t length;
    int status = service->name && path && pablo_read_file(path, &buffer, &length) == 0 ? 0 : -1;
    free(path);
    char *cursor = buffer;
    char *line;
    while (!status && next_line(&cursor, &line)) {
        line = pablo_trim(line);
        if (!*line || *line == '#') continue;
        status = parse_service_line(service, line);
    }
    free(buffer);
    if (status || !service->path) return -1;
    if (!service->argv && !(service->argv = malloc(2 * sizeof(*service->argv)))) return -1;
    service->argv[0] = service->path;
    service->argv[service->argument_count + 1] = 0;
    return 0;
}

static void sort_names(char **names, size_t count) {
    for (size_t i = 1; i < count; i++) {
        char *current = names[i];
        size_t position = i;
        while (position && strcmp(names[position - 1], current) > 0) {
            names[position] = names[position - 1];
            position--;
        }
        names[position] = current;
    }
}

static void free_names(char **names, size_t count) {
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
}

static int list_services(const char *directory, char ***result, size_t *count) {
    *result = 0;
    *count = 0;
    DIR *stream = opendir(directory);
    if (!stream) return errno == ENOENT ? 0 : -1;
    char **names = 0;
    size_t used = 0, capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(stream))) {
        if (entry->d_name[0] == '.' || !pablo_has_suffix(entry->d_name, ".service"))
            continue;
        if (used == capacity) {
            size_t next = capacity ? capacity * 2 : 8;
            char **grown = realloc(names, next * sizeof(*names));
            if (!grown) break;
            names = grown;
            capacity = next;
        }
        if (!(names[used] = pablo_duplicate(entry->d_name, strlen(entry->d_name)))) break;
        used++;
    }
    int failed = entry != 0;
    if (closedir(stream) != 0 || failed) {
        free_names(names, used);
        return -1;
    }
    sort_names(names, used);
    *result = names;
    *count = used;
    return 0;
}

static int load_services(struct pablo_config *config, const char *directory) {
    char **names;
    size_t count;
    if (list_services(directory, &names, &count) != 0) return -1;
    config->services = calloc(count ? count : 1, sizeof(*config->services));
    int status = config->services ? 0 : -1;
    for (size_t i = 0; i < count && !status; i++) {
        status = load_service(&config->services[i], directory, names[i]);
        if (!status) config->service_count++;
    }
    free_names(names, count);
    return status;
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
