#pragma once

#include <pablo/service.h>

void pablo_config_defaults(struct pablo_config *config);
int pablo_config_load(struct pablo_config *config, const char *settings_path,
                      const char *services_path);
