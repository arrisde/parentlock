#include <stdlib.h>
#include "common.h"
#include "options.h"
#include "parentlock.h"
#include "device.h"
#include "mini/mini.h"

void load_parentlock(struct mux_parentlock *parlock, struct mux_device *device, const char* parlock_file) {

    mini_t *muos_pass = mini_try_load(parlock_file);
    if (!muos_pass) {
        fprintf(stderr, "Error: Could not load parentlock file: %s\n", parlock_file);
        exit(1);
    }
    fprintf(stdout, "Loading parentlock file from: %s\n", parlock_file);

    strncpy(parlock->CODE.UNLOCK, get_ini_string(muos_pass, "code", "unlock", "0000"),
            MAX_BUFFER_SIZE - 1);
    parlock->CODE.UNLOCK[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->MESSAGE.UNLOCK, get_ini_string(muos_pass, "message", "unlock", ""),
            MAX_BUFFER_SIZE - 1);
    parlock->MESSAGE.UNLOCK[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->MESSAGE.SETTING, get_ini_string(muos_pass, "message", "setting", ""),
            MAX_BUFFER_SIZE - 1);
    parlock->MESSAGE.SETTING[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->TIMES.MONDAY, get_ini_string(muos_pass, "times", "monday", "0"),
            MAX_BUFFER_SIZE - 1);
    parlock->TIMES.MONDAY[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->TIMES.TUESDAY, get_ini_string(muos_pass, "times", "tuesday", "0"),
            MAX_BUFFER_SIZE - 1);
    parlock->TIMES.TUESDAY[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->TIMES.WEDNESDAY, get_ini_string(muos_pass, "times", "wednesday", "0"),
            MAX_BUFFER_SIZE - 1);
    parlock->TIMES.WEDNESDAY[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->TIMES.THURSDAY, get_ini_string(muos_pass, "times", "thursday", "0"),
            MAX_BUFFER_SIZE - 1);
    parlock->TIMES.THURSDAY[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->TIMES.FRIDAY, get_ini_string(muos_pass, "times", "friday", "0"),
            MAX_BUFFER_SIZE - 1);
    parlock->TIMES.FRIDAY[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->TIMES.SATURDAY, get_ini_string(muos_pass, "times", "saturday", "0"),
            MAX_BUFFER_SIZE - 1);
    parlock->TIMES.SATURDAY[MAX_BUFFER_SIZE - 1] = '\0';

    strncpy(parlock->TIMES.SUNDAY, get_ini_string(muos_pass, "times", "sunday", "0"),
            MAX_BUFFER_SIZE - 1);
    parlock->TIMES.SUNDAY[MAX_BUFFER_SIZE - 1] = '\0';

    mini_free(muos_pass);
}
