#include <pablo/service.h>
#include <string.h>

struct pablo_service *pablo_service_find(struct pablo_config *config,
                                         const char *name) {
    for (size_t i = 0; i < config->service_count; i++)
        if (strcmp(config->services[i].name, name) == 0) return &config->services[i];
    return 0;
}

static int service_index(struct pablo_config *config, const char *name) {
    for (size_t i = 0; i < config->service_count; i++)
        if (!strcmp(config->services[i].name, name)) return (int)i;
    return -1;
}

static int visit_dependency(struct pablo_config *config, size_t index,
                            unsigned char states[PABLO_SERVICE_MAX]) {
    if (states[index] == 1) return -1;
    if (states[index] == 2) return 0;
    states[index] = 1;
    struct pablo_service *service = &config->services[index];
    if (service->after[0]) {
        int dependency = service_index(config, service->after);
        if (dependency < 0 || visit_dependency(config, (size_t)dependency, states) != 0)
            return -1;
    }
    states[index] = 2;
    return 0;
}

int pablo_service_validate(struct pablo_config *config) {
    for (size_t i = 0; i < config->service_count; i++) {
        struct pablo_service *service = &config->services[i];
        if (!service->name[0] || service->path[0] != '/') return -1;
        for (size_t j = i + 1; j < config->service_count; j++)
            if (strcmp(service->name, config->services[j].name) == 0) return -1;
        if (service->after[0] &&
            (!strcmp(service->after, service->name) ||
             !pablo_service_find(config, service->after)))
            return -1;
    }
    unsigned char states[PABLO_SERVICE_MAX] = { 0 };
    for (size_t i = 0; i < config->service_count; i++)
        if (visit_dependency(config, i, states) != 0) return -1;
    return 0;
}
