#include <inttypes.h>
#include <string.h>

#include "osapi.h"
#include "user_support.h"
#include "espmissingincludes.h"
#include "user_interface.h"

#include "wish_connection.h"
#include "wish_connection_mgr.h"
#include "wish_local_discovery.h"
#include "user_main.h"
#include "wish_time.h"
#include "spiffs.h"
#include "spiffs_integration.h"
#include "port_printf.h"

void user_paint_stack(void) {
    uint8_t *stack_end = (uint8_t *)USER_STACK_END;
    memset(stack_end, STACK_CANARY, USER_STACK_SIZE-32);    /* NB: substract 32 bytes
    for the invocation of this function (otherwise we would corrupt
        stack and this function would not return */
}

int32_t user_find_stack_canary(void) {
    uint8_t *p = (uint8_t *) USER_STACK_END;
    int32_t c = 0;
    while(*p == STACK_CANARY && p <= (uint8_t *) USER_STACK_START) {
        p++;
        c++;
    }
    return c;
}

/**
 * This function will restart the device. 
 *
 * XXX Please that restart will not happen if you have just flashed the
 * device. In that case a hard reboot is necessary before reboot will
 * work correctly. This is an SDK API issue...
 */
void user_reboot(void) {
    os_printf("\n\r\t*** REBOOT ***\n");
    system_restart();
    while(1);
}

void user_close_active_connections(void) {
    wish_connections_close_all(user_get_core_instance());
}

void user_stop_wld_bcasts(void) {
    wish_ldiscover_disable_bcast(user_get_core_instance());
}

void user_delete_id_db(void) {
    wish_identity_delete_db();
}

int32_t user_get_uptime_minutes(void) {
    wish_time_t now = wish_time_get_relative(user_get_core_instance());
    return now/60;
}

void user_mappings_file_delete(void) {
    int32_t ret = my_fs_remove("mappings.bin");
    if (ret != SPIFFS_OK) {
        PORT_PRINTF("Failed removing mappings.bin");
    }
}
