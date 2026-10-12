#include "../internal/narc.h"
#include <sys/stat.h>

mode_t umask(mode_t mask) {
    return (mode_t)narc_umask(mask & 0777).value;
}
