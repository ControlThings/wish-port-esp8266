#ifndef USER_SUPPORT_H
#define USER_SUPPORT_H

#include "os_type.h"
#include "c_types.h"                            
#include "ip_addr.h"
#include "espconn.h"
#include "mem.h"
#include "user_interface.h"
#include "espmissingincludes.h"

#define STACK_CANARY 0x55

#define USER_STACK_END      0x3fffdfe0

#define USER_STACK_START    0x3ffff7a0 

#define USER_STACK_SIZE          (6*1024)

/* This function "paints" the user program stack with a known value. 
 * The other function can now be used every once in a while to examine
 * how "far" we have used the stack (because the "canary" value is no
 * longer there) */
void user_paint_stack(void);

/* Count how many bytes of stack have not been overwritten by procedure
 * calls */
int32_t user_find_stack_canary(void);

/**
 * This function will restart the device. 
 *
 * XXX Please that restart will not happen if you have just flashed the
 * device. In that case a hard reboot is necessary before reboot will
 * work correctly. This is an SDK API issue...
 */
void user_reboot(void);

void user_close_active_connections(void);

void user_stop_wld_bcasts(void);

void user_delete_id_db(void);

bool user_is_claimed(void);

int32_t user_get_uptime_minutes(void);

void user_signal_update_changed(void);

void user_mappings_file_delete(void);

#endif
