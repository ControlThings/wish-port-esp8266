/* 
 * Wish message processor task for ESP8266 runtime 
 */

#include "c_types.h"

#include "user_interface.h"
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "user_config.h"
#include "mem.h"
#include "espconn.h"
#include "espmissingincludes.h"

#include "wish_connection.h"
#include "wish_event.h"

#include "user_hw_config.h"
#include "user_task.h"
#include "user_main.h"
#include "wish_port_config.h"
#include "port_printf.h"


os_event_t *task_event_queue;

os_event_t *follow_event_queue;

static void my_message_processor_task(ETSEvent *e) {
    struct wish_event ev = { .event_type = e->sig, 
        .context = (wish_connection_t *)e->par };
    wish_core_t* core = user_get_core_instance();
    
    if (ev.event_type == WISH_EVENT_REQUEST_CONNECTION_CLOSING && ev.context == NULL && ev.metadata != NULL) {
        espconn_disconnect(ev.metadata);
    }
    
    if (ev.event_type == WISH_EVENT_REQUEST_CONNECTION_ABORT && ev.context == NULL && ev.metadata != NULL) {
        struct espconn *espconn = ev.metadata;
        sint8 err = espconn_abort(espconn);
        if (err != 0) {
            PORT_PRINTF("Abort fails: %d", err);
        }
        os_free(espconn->proto.tcp);
        os_free(espconn);
    }

    wish_message_processor_task(core, &ev);
    
    if (ev.event_type == WISH_EVENT_NEW_DATA) {
        /* If we are handling a "new data" event, consume all the new data immediately, but only as long as the connection is still connected    */
        int max_iter = 10;
        while (wish_core_get_rx_buffer_free(core, ev.context) < WISH_PORT_RX_RB_SZ && ev.context->context_state == WISH_CONTEXT_CONNECTED && max_iter--) {
            wish_message_processor_task(core, &ev);
            system_soft_wdt_feed(); 
        }
        
        espconn_recv_unhold(ev.context->send_arg);
    }
}

/* Initialise a event queue for handling messages. */
void wish_message_processor_init(wish_core_t *core) {
    task_event_queue 
        = (os_event_t*) os_malloc(sizeof(os_event_t)*TASK_EVENT_QUEUE_LEN);
    system_os_task(my_message_processor_task, MESSAGE_PROCESSOR_TASK_ID, 
        task_event_queue, TASK_EVENT_QUEUE_LEN);
}

void wish_message_processor_notify(struct wish_event *ev) {
    system_os_post(MESSAGE_PROCESSOR_TASK_ID, ev->event_type, (uint32_t) ev->context);
}


void user_display_malloc(void) {
#ifdef MEMLEAK_DEBUG
    system_show_malloc();
#else
    //PORT_PRINTF("no malloc info available\n\r");
#endif  //MEMLEAK_DEBUG
}
