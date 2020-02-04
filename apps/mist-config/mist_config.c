/* C lib and other "standard" includes */
#include <stdint.h>
#include <string.h>

/* ESP8266 SDK includes */
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "osapi.h"
#include "espmissingincludes.h"


/* Wish & Mist includes */
#include "wish_app.h" 
#include "mist_app.h"
#include "mist_model.h"
#include "mist_model.h"
#include "mist_handler.h"
#include "wish_debug.h"
#include "bson.h"

#include "wish_port_config.h"

/* Mist app includes */
#include "mist_config.h"
#include "mist_api.h"

#include "bson.h"
#include "bson_visit.h"

/* App includes which are ESP8266 specific */
#include "user_wifi.h"
#include "user_tcp.h"
#include "user_support.h"
#include "port_printf.h"


static mist_app_t* mist_app;
static os_timer_t reboot_timer;

static void reboot_timer_cb(void) {
    user_reboot();
}

/** Endpoint name for MistCommissioning device (type) */
#define MIST_TYPE_EP "mistType"
/** Endpoint name for MistCommissioning device (version) */
#define MIST_VERSION_EP "mistVersion"
/** Endpoint name for MistCommissioning device (Endpoint for listing
 * wifi) */
#define MIST_WIFI_LIST_AVAILABLE_EP "mistWifiListAvailable"
/** Endpoint name for MistCommissioning device (Configuration endpoint) */
#define MIST_WIFI_COMMISSIONING_EP "mistWifiCommissioning"

#define MIST_UPTIME_EP "uptime"

#define MIST_APP_NAME "MistConfig"

static enum mist_error wifi_read(mist_ep* ep, wish_protocol_peer_t* peer, int request_id) {
    size_t result_max_len = 256;
    uint8_t result[result_max_len];

    char full_epid[MIST_EPID_LEN];
    mist_ep_full_epid(ep, full_epid);

    bson bs;
    bson_init_buffer(&bs, result, result_max_len);

    WISHDEBUG(LOG_CRITICAL, "in wifi_read");
    /* FIXME: should use full_epid in name comparisons */
    if (strcmp(ep->id, MIST_TYPE_EP) == 0) {
        bson_append_string(&bs, "data", "WifiCommissioning");
    }
    else if (strcmp(ep->id, MIST_VERSION_EP) == 0) {
        bson_append_string(&bs, "data", "1.0.1");
    }
    else if (strcmp(ep->id, "portVersion") == 0) {
        bson_append_string(&bs, "data", PORT_VERSION);
    }
    else if (strcmp(ep->id, MIST_UPTIME_EP) == 0) {
        int32_t reltime = user_get_uptime_minutes();
        bson_append_int(&bs, "data", reltime);
    }
    else if (strcmp(ep->id, "mist") == 0) {
        bson_append_string(&bs, "data", "");
    }
    else if (strcmp(ep->id, "name") == 0) {
        bson_append_string(&bs, "data", MIST_APP_NAME);
    }
    bson_finish(&bs);
  
    mist_read_response(ep->model->mist_app, full_epid, request_id, &bs);
    
    WISHDEBUG(LOG_CRITICAL, "returning from wifi_read");
    return MIST_NO_ERROR;
    
}

/* See here what the Invoke should return
 * https://gist.github.com/akaustel/1f7efeb791d156ea98099fe7b6e63ae7
 *
 * To connect the ESP8266 to a wifi network, invoke endpoint 
 * mistWifiCommissioning with this argument:
 *
 * { "wifi_Credentials":"password", "ssid":"ssid-name" }
 *
 * For example for our office network:
 * { "wifi_Credentials":"19025995", "ssid":"Buffalo-G-12DA" }
 * In Mist UI, remember to check "JSON" checkbox! 
 *
 */
static enum mist_error wifi_invoke(mist_ep* ep, wish_protocol_peer_t* peer, int request_id, bson* args) {
    /* The stuff that comes in to this function in args.base has following structure: 
     
        epid: 'mistWifiCommissioning'           <---- added by handle_control_model on local side
        id: 8                                   <---- idem  
        args: {                                 <---- this is arguments from remote side
            ssid: 'Buffalo-G-12DA'
            wifi_Credentials: '19025995'
        }

     */
   
    char *ep_id = ep->id;
    int32_t result_max_len = WISH_PORT_RPC_BUFFER_SZ;

    uint8_t* result = wish_platform_malloc(result_max_len);
    if (result == NULL) {
        WISHDEBUG(LOG_CRITICAL, "OOM in wifi_invoke");
        return MIST_ERROR;
    }

    WISHDEBUG(LOG_CRITICAL, "in wifi_invoke ep: %s %p", ep_id, args);
    
    bson_visit("args", bson_data(args));
    
    WISHDEBUG(LOG_DEBUG, "Control.invoke rpc %d", request_id);
    
    bson bs;
    bson_init_buffer(&bs, result, result_max_len);
    char ** ssid_str_ptrs = user_wifi_get_ssids();
    int8_t *ssid_rssis = user_wifi_get_rssis();
    if (strcmp(ep_id, MIST_WIFI_LIST_AVAILABLE_EP) == 0) {
        bson_append_start_object(&bs, "data");
        int i = 0;
        for (i = 0; i < MAX_NUM_SSIDS; i++) {
            if (ssid_str_ptrs[i] == NULL) {
                continue;
            }
            
            /* FIXME terrible array index hack */
            char arr_index[2];
            arr_index[0] = '0'+i;
            arr_index[1] = 0;
            WISHDEBUG(LOG_DEBUG, "encoding %s %d", ssid_str_ptrs[i], ssid_rssis[i]);
            bson_append_start_object(&bs, arr_index);
            bson_append_string(&bs, "ssid", ssid_str_ptrs[i]);
            bson_append_int(&bs, "rssi", ssid_rssis[i]);
            bson_append_finish_object(&bs);
            if (bs.err) {
                WISHDEBUG(LOG_CRITICAL, "BSON error while adding ssid/rssi");
                os_free(result);
                return MIST_ERROR;
            }
        }
        WISHDEBUG(LOG_DEBUG, "finished appending");
        bson_append_finish_object(&bs);
        bson_finish(&bs);
        if (bs.err) {
            WISHDEBUG(LOG_CRITICAL, "There was an BSON error");
            os_free(result);
            return MIST_ERROR;
        } else {
            /* Send away the control.invoke response */
            mist_invoke_response(mist_app, ep->id, request_id, &bs);
        }
    }
    else if (strcmp(ep_id, MIST_WIFI_COMMISSIONING_EP) == 0) {
        char* ssid = NULL;
        char* password = NULL;
        bson_iterator it;
        bson_find(&it, args, "args");
        bson_iterator sit;
        bson_iterator_subiterator(&it, &sit);
        bson_find_fieldpath_value("wifi_Credentials", &sit);
        if (bson_iterator_type(&sit) == BSON_STRING) {
            password = (char *) bson_iterator_string(&sit);
        } else {
            WISHDEBUG(LOG_CRITICAL, "element wifi_Credentials is missing or not a string");
            os_free(result);
            return MIST_ERROR;
        }
         /* Sub-iterator must be re-set in order to guarantee that the order in which we take out the elems do not depend on the order of elems in 'args' document! */
        bson_iterator_subiterator(&it, &sit);
        bson_find_fieldpath_value("ssid", &sit);
        if (bson_iterator_type(&sit) == BSON_STRING) {
            ssid = (char *) bson_iterator_string(&sit);
        } else {
            WISHDEBUG(LOG_CRITICAL, "element ssid is missing or not a string");
            os_free(result);
            return MIST_ERROR;
        }
        
        
        WISHDEBUG(LOG_CRITICAL, "Will switch to SSID: %s password %s", ssid, password);
        bson_append_bool(&bs, "data", true);
        bson_finish(&bs);
        if (bs.err) {
            WISHDEBUG(LOG_CRITICAL, "There was an BSON error");
            os_free(result);
            return MIST_ERROR;
        } else {
            /* Send away the control.invoke response */
            mist_invoke_response(mist_app, ep->id, request_id, &bs);
        }
        
        /* Stop broadcasting local discovery messages */
        user_close_active_connections();
        user_stop_server();
        user_close_active_connections();
        /* The Wish server and Wish local discovery should be started
         * automatically when we detect we have obtained IP */
        user_wifi_set_station_mode();
        user_set_station_config(ssid, password);
        /* FIXME Find a way to make connections work *without* needing a
         * reboot */

        /* Schedule a reboot via timer. This is because we would like to
         * give the TCP stack possibility to send the "ack" message of
         * this invoke function */
        os_timer_disarm(&reboot_timer);
        os_timer_setfn(&reboot_timer, (os_timer_func_t *) reboot_timer_cb, NULL);
        os_timer_arm(&reboot_timer, 1000, 0);
    }

    wish_platform_free(result);

    WISHDEBUG(LOG_DEBUG, "Exiting");
    return MIST_NO_ERROR;
}

static mist_ep type_ep = {.id = MIST_TYPE_EP, .label = MIST_TYPE_EP, .type = MIST_TYPE_STRING, .read = wifi_read};
static mist_ep version_ep = {.id = MIST_VERSION_EP, .label = MIST_VERSION_EP, .type = MIST_TYPE_STRING, .read = wifi_read};
static mist_ep list_available_ep = { .id = MIST_WIFI_LIST_AVAILABLE_EP, .label = MIST_WIFI_LIST_AVAILABLE_EP, .type = MIST_TYPE_INVOKE, .read = NULL, .write = NULL, .invoke = wifi_invoke};
static mist_ep commissioning_ep = { .id = MIST_WIFI_COMMISSIONING_EP, .label = MIST_WIFI_COMMISSIONING_EP, .type = MIST_TYPE_INVOKE, .read = NULL, .write = NULL, .invoke = wifi_invoke};

static mist_ep mist_super_ep = {.id = "mist", .label = "", .type = MIST_TYPE_STRING, .read = wifi_read };
static mist_ep mist_name_ep = {.id = "name", .label = "Name", .type = MIST_TYPE_STRING, .read = wifi_read };
static mist_ep port_version_ep = {.id = "portVersion", .label = "Port layer version", .type = MIST_TYPE_STRING, .read = wifi_read };
static mist_ep uptime_ep = {.id = "uptime", .label = "Uptime" , .type = MIST_TYPE_INT, .read = wifi_read };

static wish_app_t *app;

static void friend_request_accept_cb(struct wish_rpc_entry* req, void* ctx, const uint8_t* data, size_t data_len) {
    PORT_PRINTF( "Friend request accept cb, data_len = %d", data_len);
    bson_visit("Friend request accept cb", data);
}

static void friend_request_list_cb(struct wish_rpc_entry* req, void* ctx, const uint8_t* data, size_t data_len) {
    PORT_PRINTF("friend_request_list_cb, data_len = %d\n", data_len);
    //bson_visit("friend_request_list_cb", data);
    
    uint8_t *luid = NULL; 
    uint8_t *ruid = NULL;
    
    bson_iterator it;
    BSON_ITERATOR_FROM_BUFFER(&it, data);
    /* Snatch the local uid who got this friend request */
    bson_find_fieldpath_value("data.0.luid", &it);
    if (bson_iterator_type(&it) == BSON_BINDATA) {
        luid = (uint8_t *) bson_iterator_bin_data(&it);
    }
    else {
        PORT_PRINTF("friend_request_list_cb, unexpected datatype");
    }
    
    BSON_ITERATOR_FROM_BUFFER(&it, data);
    /* Snatch the remote uid who sent this friend request*/
    bson_find_fieldpath_value("data.0.ruid", &it);
    if (bson_iterator_type(&it) == BSON_BINDATA) {
        ruid = (uint8_t *) bson_iterator_bin_data(&it);
    }
    else {
        PORT_PRINTF("friend_request_list_cb, unexpected datatype");
    }
    bson *bs = os_malloc(sizeof(bson));
    const size_t buf_sz = 256;
    uint8_t *buf = os_malloc(buf_sz);
    if (buf == NULL) {
        PORT_PRINTF("OOM alloc bson\n");
    }
    bson_init_buffer(bs, buf, buf_sz);
    /* Accept the friend request */
    bson_append_string(bs, "op",  "identity.friendRequestAccept");
    bson_append_start_array(bs, "args");
    bson_append_binary(bs, "0", luid, 32); //bson *b, const char *name, const char *str, int len
    bson_append_binary(bs, "1", ruid, 32); //bson *b, const char *name, const char *str, int len
    bson_append_finish_array(bs);
    bson_append_int(bs, "id", 0);
    bson_finish(bs);
    
    PORT_PRINTF("friend_request_list_cb, about to send\n");
    //bson_visit("accept req:", bson_data(&bs));
    wish_app_request(mist_app->app, bs, friend_request_accept_cb, NULL);
    os_free(bs);
    os_free(buf);
    PORT_PRINTF("friend_request_list_cb, sent!\n");
}

static void make_friend_request_list(void) {
    bson *bs = os_malloc(sizeof(bson));
    const size_t buf_sz = 100;
    uint8_t *buf = os_malloc(buf_sz);
    bson_init_buffer(bs, buf, buf_sz);

    bson_append_string(bs, "op", "identity.friendRequestList");
    bson_append_start_array(bs, "args");
    bson_append_finish_array(bs);
    bson_append_int(bs, "id", 0);
    bson_finish(bs);

    wish_app_request(mist_app->app, bs, friend_request_list_cb, NULL);
    os_free(buf);
    os_free(bs);
}

#if 0
static void core_list_identities_cb(struct wish_rpc_entry* req, void* ctx, const uint8_t* data, size_t data_len) {
    bson_visit("core_list_identities_cb", data);
}
#endif

static void signals_cb(struct wish_rpc_entry* req, void* ctx, const uint8_t* data, size_t data_len) {
    bson_visit("signals_cb: Got core signals:", data);
    
    bson_iterator it;
    BSON_ITERATOR_FROM_BUFFER(&it, data);
    
    bson_find_fieldpath_value("data.0", &it);
    if (bson_iterator_type(&it) == BSON_STRING) {
        if (strncmp(bson_iterator_string(&it), "friendRequest", 100) == 0) {
            PORT_PRINTF( "signals_cb: We got friend request\n");
            
            if (user_is_claimed() == false) {
                PORT_PRINTF("core->config_skip_connection_acl is true, accepting friend request now!\n");
                make_friend_request_list();
            }
        }
        else {
           //PORT_PRINTF("Got core signals: %p", data);
        }
    }
    else {
        PORT_PRINTF( "signals_cb: Unexpected datatype\n");
    }
}


static void init_app(wish_app_t* app, bool ready) {
    PORT_PRINTF("We are here in init_app!");
    
    if (ready) {
        PORT_PRINTF("API ready!");
        //make_some_calls(mist_api);
        
        bson bs;
        const size_t buf_sz = 100;
        uint8_t buf[buf_sz];
        bson_init_buffer(&bs, buf, buf_sz);
        bson_append_string(&bs, "op", "signals");
        bson_append_start_array(&bs, "args");
        bson_append_finish_array(&bs);
        bson_append_int(&bs, "id", 0);
        bson_finish(&bs);
        
        //wish_app_core_with_cb_context(mist_app->app, "identity.list", (uint8_t*) bson_data(&bs), bson_size(&bs), core_list_identities_cb, NULL);

        /* Recycle the args buffer for signals */
        wish_app_request(mist_app->app,  &bs, signals_cb, NULL);
    
    } else {
        PORT_PRINTF("API not ready!");
    }
}




void mist_config_init(void) {
    WISHDEBUG(LOG_CRITICAL, "entering config init");
    char *name = "MistConfig";
    mist_app = start_mist_app();
    if (mist_app == NULL) {
        WISHDEBUG(LOG_CRITICAL, "Failed creating wish app");
        return;
    }

    app = wish_app_create(name);

    if (app == NULL) {
        WISHDEBUG(LOG_CRITICAL, "Failed creating wish app");
        return; 
    }
    WISHDEBUG(LOG_CRITICAL, "here3");
    wish_app_add_protocol(app, &(mist_app->protocol));
    mist_app->app = app;

    WISHDEBUG(LOG_CRITICAL, "Adding EPs");
    mist_ep_add(&(mist_app->model), NULL, &mist_super_ep);
    mist_ep_add(&(mist_app->model), mist_super_ep.id, &mist_name_ep);
    mist_ep_add(&(mist_app->model), NULL, &type_ep);
    mist_ep_add(&(mist_app->model), NULL, &version_ep);
    mist_ep_add(&(mist_app->model), NULL, &list_available_ep);
    mist_ep_add(&(mist_app->model), NULL, &commissioning_ep);
    mist_ep_add(&(mist_app->model), NULL, &port_version_ep);
    mist_ep_add(&(mist_app->model), NULL, &uptime_ep);
        
    app->ready = init_app;
     
    PORT_PRINTF("Commencing mist api init3\n");
    wish_app_connected(app, true);
    PORT_PRINTF("exiting config init"); 

    
    
    //WISHDEBUG(LOG_CRITICAL, "exiting config init"); 
}

void user_signal_update_changed(void) {
    mist_value_changed(mist_app, MIST_UPTIME_EP);
}