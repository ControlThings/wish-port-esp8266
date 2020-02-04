/*
 * A Service IPC layer implementation for ESP8266.
 * 
 * In the past, this layer used to be just a simple "pass-through" API for connecting the function calls together. The calls over the layer were synchronous.
 * This led to many problems, including pronounced stack usage, which became a problem when doing wish calls from app to core, for example when accepting a friend request. 
 * In such cases there there is a lot of core<->app calls back and forth over the IPC layer, with new entries on the stack each time.
 * 
 * Now, we make use of the esp8266's ETS event construct to make the core<->app calls asynchronous, stack spece use reduction as primary motivation.
 * One task to handle both app-to-core and core-to-app calls. 
 */

#include "osapi.h"
#include "os_type.h"
#include "user_task.h"
#include "user_interface.h"
#include "espmissingincludes.h"
#include "mem.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "wish_core.h"
#include "../../mist-c99/wish_app/wish_app.h"
#include "../../mist-c99/wish_app/app_service_ipc.h"
#include "core_service_ipc.h"
#include "bson_visit.h"
#include "wish_debug.h"
#include "wish_dispatcher.h"
#include "wish_port_config.h"
#include "utlist.h"
#include "user_tcp.h"
#include "port_printf.h"

static wish_core_t* core;

enum ipc_event_type { EVENT_UNKNOWN, EVENT_APP_TO_CORE, EVENT_CORE_TO_APP };

struct ipc_event {
    enum ipc_event_type type;
    wish_app_t *app;
    uint8_t *data;
    size_t len;
    struct ipc_event *next;
};

struct ipc_event *ipc_event_queue = NULL;
os_event_t single_ets_ev;

static void service_ipc_task(os_event_t *ets_ev) {
    
    /* Take first eleent in queue*/
    struct ipc_event *event = ipc_event_queue;
   
    if (event == NULL) {
        PORT_PRINTF("Unexpected: Event queue is empty!");
        return;
    }
    
    if (user_get_send_queue_len() > 0) {
        /* Defer processing of the queue! */
        system_os_post(SERVICE_IPC_TASK_ID, 0, 0);
        //user_hold();
        return;
    }
    else {
        //user_unhold();
    }
    
    switch (event->type) {
        case EVENT_APP_TO_CORE:
            /* Feed the message to core */
            receive_app_to_core(core, event->app->wsid, event->data, event->len);
            break;
        case EVENT_CORE_TO_APP: {
            
            receive_core_to_app(event->app, event->data, event->len);
           
            break;
        }
        default:
            PORT_PRINTF("Bad ipc event!");
    }
    
    LL_DELETE(ipc_event_queue, event);
    os_free(event->data);
    os_free(event);
    
    struct ipc_event *elt;
    int queue_len = 0;
    LL_COUNT(ipc_event_queue, elt, queue_len);
    
    if ( queue_len > 0) {
        /* Continue processing the event queue */
        system_os_post(SERVICE_IPC_TASK_ID, 0, 0);
    }
    else {
        PORT_PRINTF("IPC event queue is empty!\n  ");
    }
}

void core_service_ipc_init(wish_core_t* wish_core) {
    core = wish_core;    
    system_os_task(service_ipc_task, SERVICE_IPC_TASK_ID, &single_ets_ev, 1);
}

void send_app_to_core(uint8_t *wsid, const uint8_t *data, size_t len) {
    /* Handle the following situations:
     *      -login message 
     *      -normal situation */
    
    
    
    struct ipc_event *event = os_malloc(sizeof(struct ipc_event));
    if (event == NULL) {
        PORT_PRINTF("Could not allocate ipc event!\n");
        return;
    }
    else {
        event->data = os_malloc(len);
        if (event->data == NULL) {
            PORT_PRINTF("Could not allocate ipc event data buffer: type %d\n", EVENT_APP_TO_CORE);
            return;
        }
        memcpy(event->data, data, len);
        event->len = len;
        event->app = wish_app_find_by_wsid(wsid);
        event->type = EVENT_APP_TO_CORE;
        LL_APPEND(ipc_event_queue, event);
        system_os_post(SERVICE_IPC_TASK_ID, 0, 0);
    }
}


void receive_app_to_core(wish_core_t* core, const uint8_t wsid[WISH_ID_LEN], const uint8_t *data, size_t len) {
    wish_core_handle_app_to_core(core, wsid, data, len);
}

void receive_core_to_app(wish_app_t *app, const uint8_t *data, size_t len) {
    /* Parse the message:
     * -peer
     * -frame
     * -ready signal
     *
     * Then feed it to the proper wish_app_* callback depending on type
     */
    wish_app_determine_handler(app, (uint8_t*) data, len);
}


void send_core_to_app(wish_core_t* core, const uint8_t wsid[WISH_ID_LEN], const uint8_t *data, size_t len) {
    struct ipc_event *event = os_malloc(sizeof(struct ipc_event));
    if (event == NULL) {
        PORT_PRINTF("Could not allocate ipc event!\n");
        return;
    }
    else {
        event->data = os_malloc(len);
        if (event->data == NULL) {
            PORT_PRINTF("Could not allocate ipc event data buffer: type %d\n", EVENT_CORE_TO_APP);
            return;
        }
        memcpy(event->data, data, len);
        event->len = len;
        event->app = wish_app_find_by_wsid((uint8_t*) wsid);
        event->type = EVENT_CORE_TO_APP;
        LL_APPEND(ipc_event_queue, event);
        system_os_post(SERVICE_IPC_TASK_ID, 0, 0);
    }
}
