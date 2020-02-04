#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "c_types.h"
#include "ip_addr.h"
#include "espconn.h"
#include "mem.h"
#include "user_interface.h"
#include "espmissingincludes.h"

#include "wish_connection.h"
#include "wish_relay_client.h"

#include "user_config.h"
#include "user_task.h"
#include "user_relay.h"
#include "wish_identity.h"
#include "user_main.h"
#include "port_printf.h"


static struct espconn* espconn;


/*
 * The relay control connection's TCP receive CB
 */
static void user_relay_tcp_recv_cb(void *arg, char *pusrdata, unsigned short length) {
//    PORT_PRINTF("in relay tcp recv cb\n\r");
    struct espconn *espconn = arg;
    wish_relay_client_t *relay = espconn->reverse;
    wish_relay_client_feed(user_get_core_instance(), relay, (unsigned char*) pusrdata, length);
    wish_relay_client_periodic(user_get_core_instance(), relay);
}

/* The relay control connection's TCP sent callback
 */
static void user_relay_tcp_sent_cb(void *arg)
{
}


/* The relay control connection's TCP connection disconnect callback 
 */
static void user_relay_tcp_discon_cb(void *arg)
{
    PORT_PRINTF("Relay TCP disconnect cb\n\r");
    struct espconn *espconn = arg;
    wish_relay_client_t *relay = espconn->reverse;
    relay_ctrl_disconnect_cb(user_get_core_instance(), relay);
    
    os_free(espconn->proto.tcp);
    os_free(espconn);
}

/*
 * The relay control connection's TCP send data function
 */
static int user_relay_tcp_send(int relay_sockfd, unsigned char* data, int len) {
    /* FIXME relay_sockfd just plainly ignored ! */
    return espconn_send(espconn, data, len);
}

/* 
 * The relay control connections TCP connect error callback 
 */
static void user_relay_tcp_recon_cb(void *arg, sint8 err) {
    PORT_PRINTF("Error when establishing relay control connection: %d\r\n", err);
    
    
    struct espconn *espconn = arg;
    wish_relay_client_t *relay = espconn->reverse;
    relay_ctrl_connect_fail_cb(user_get_core_instance(), relay);
    
    os_free(espconn->proto.tcp);
    os_free(espconn);
}


/* 
 * The Relay control connection's TCP connect callback
 */
static void user_relay_tcp_connect_cb(void *arg) {
    struct espconn *pespconn = arg;
    wish_relay_client_t *relay = pespconn->reverse;

    PORT_PRINTF("Relay server control connection established \r\n");

    espconn_regist_recvcb(pespconn, user_relay_tcp_recv_cb);
    espconn_regist_sentcb(pespconn, user_relay_tcp_sent_cb);
    espconn_regist_disconcb(pespconn, user_relay_tcp_discon_cb);
    espconn_regist_reconcb(pespconn, user_relay_tcp_recon_cb);

    relay->send = user_relay_tcp_send;
    //relay->send_arg = arg;
    relay_ctrl_connected_cb(user_get_core_instance(), relay);
    wish_relay_client_periodic(user_get_core_instance(), relay);
}

//int8_t user_start_relay_control() {
void wish_relay_client_open(wish_core_t* core, wish_relay_client_t *relay, uint8_t relay_uid[WISH_ID_LEN]) {
    /* FIXME refactor these! They are port-agnostic! */
    relay->curr_state = WISH_RELAY_CLIENT_CONNECTING;
    relay->last_input_timestamp = wish_time_get_relative(core);
    
    ring_buffer_init(&(relay->rx_ringbuf), relay->rx_ringbuf_storage,
                    RELAY_CLIENT_RX_RB_LEN);
    memcpy(relay->uid, relay_uid, WISH_ID_LEN);

    /* ESP-specific stuff starts here */

    PORT_PRINTF("Open relay control connection\n\r");

    /* Allocate the espconn structure for client use. This will be
     * de-allocated in the client disconnect callback, or the
     * reconnect callback */
    espconn = (struct espconn*) os_malloc(sizeof(struct espconn));
    memset(espconn, 0, sizeof(struct espconn));
    esp_tcp* user_tcp = (esp_tcp*) os_malloc(sizeof(esp_tcp)); 
    memset(user_tcp, 0, sizeof(esp_tcp));
    espconn->proto.tcp = user_tcp;
    espconn->type = ESPCONN_TCP;
    espconn->state = ESPCONN_NONE;
    espconn->reverse = relay;

    /* Connect to a Wish system willing to relay */
    ip_addr_t relay_server_ip;
    IP4_ADDR(&relay_server_ip, relay->ip.addr[0], relay->ip.addr[1], relay->ip.addr[2], relay->ip.addr[3]);

 
    sint8 err = 0;

    if (espconn->proto.tcp == 0) {
        err = ESPCONN_ARG;
    }
    else if (espconn->type != ESPCONN_TCP) {
        err = ESPCONN_ARG;
    }
    else if (espconn->state != ESPCONN_NONE) {
        err = ESPCONN_ARG;
    }
    else {
        /* struct espconn seems acceptable */

        os_memcpy(espconn->proto.tcp->remote_ip, &relay_server_ip, 4);

        espconn->proto.tcp->remote_port = relay->port;      // remote port

        espconn->proto.tcp->local_port = espconn_port();   //local port of ESP8266

        espconn_regist_connectcb(espconn, user_relay_tcp_connect_cb);  // register connect callback
        espconn_regist_reconcb(espconn, user_relay_tcp_recon_cb);      // register reconnect callback as error handler
        espconn_connect(espconn);
    }

}


void wish_relay_client_close(wish_core_t *core, wish_relay_client_t *rctx) {
    if (espconn_disconnect(espconn) != 0) {
        PORT_PRINTF("Relay control diconnect fail\n\r");
        /* The relay control connection disconnect failed, this is probably because it had never connected. Free the espconn */
 
        relay_ctrl_disconnect_cb(user_get_core_instance(), rctx);

        os_free(espconn->proto.tcp);
        os_free(espconn);
    }
}
