#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_config.h"
#include "user_task.h"
//#include "driver/uart.h"

#include "c_types.h"                            
#include "ip_addr.h"
#include "espconn.h"
#include "mem.h"
#include "user_interface.h"
#include "espmissingincludes.h"

#include "wish_ip_addr.h"
#include "wish_connection.h"
#include "wish_dispatcher.h"
#include "wish_event.h"

#include "user_tcp.h"
#include "user_wifi.h"
#include "user_relay.h"
#include "user_tcp_client.h"
#include "user_tcp_server.h"

#include "wish_event.h"
#include "wish_identity.h"
#include "wish_connection_mgr.h"
#include "wish_local_discovery.h"
#include "wish_time.h"

#include "utlist.h"
#include "wish_connection.h"
#include "user_main.h"
#include "port_printf.h"


LOCAL os_timer_t test_timer;
ip_addr_t tcp_server_ip;

LOCAL void ICACHE_FLASH_ATTR user_tcp_recon_cb(void *arg, sint8 err);
static int ICACHE_FLASH_ATTR my_send_data(void *pespconn, unsigned char* data, int len);


/******************************************************************************
 * FunctionName : user_tcp_recv_cb
 * Description  : receive callback.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_tcp_recv_cb(void *arg, char *pusrdata, unsigned short length)
{
    struct espconn *espconn = arg;
    PORT_PRINTF("tcp_recv_cb IP: %d.%d.%d.%d\n\r", 
        espconn->proto.tcp->remote_ip[0],
        espconn->proto.tcp->remote_ip[1],
        espconn->proto.tcp->remote_ip[2],
        espconn->proto.tcp->remote_ip[3]);

    //received some data from tcp connection

    /* If we would have many concurrent connections, we would need to
     * first check from source ip, dst and src ports, to which wish_context
     * (which TCP connection) the data we just received pertains to. */
    wish_connection_t* connection 
        = wish_identify_context(user_get_core_instance(), espconn->proto.tcp->remote_ip,
            espconn->proto.tcp->remote_port, 
            espconn->proto.tcp->local_ip,
            espconn->proto.tcp->local_port);

    if (connection == NULL) {
        return;
    }
    
    
    int rb_free =  wish_core_get_rx_buffer_free(user_get_core_instance(), connection);
    if (length > rb_free) {
        /* Note: We must do this check explicitly here, even if wish_core_feed would do it anyway. In this case, as the we have more data than we can process,
         * wish_core_feed() would disconnect the connection, but that is against the espconn rules - you can't call espconn_disconnect while in an espconn callback.
         * We need to just flag the connection for closure later, else a memory leak will ensue.
         */
        PORT_PRINTF("Received packet that is too large to handle. %d > %d Disconnecting.\n", length, rb_free);
        struct wish_event ev = { .event_type = WISH_EVENT_REQUEST_CONNECTION_CLOSING, 
            .context = connection };
        wish_message_processor_notify(&ev);
    }
    else {
        wish_core_feed(user_get_core_instance(), connection, pusrdata, length);
        espconn_recv_hold(espconn);
        struct wish_event ev = { .event_type = WISH_EVENT_NEW_DATA, 
            .context = connection };
        wish_message_processor_notify(&ev);
    }
}


struct fifo_entry {
    char *data;
    int data_len;

    struct fifo_entry *next;
};

struct active_conn_entry {
    bool busy;
    struct espconn *espconn;
    struct fifo_entry *fifo_head;
    struct active_conn_entry *next;
};

struct active_conn_entry *active_conn_head = NULL;


int user_get_send_queue_len(void) {
    int total_len_of_queues = 0;
    struct active_conn_entry *elt;
    LL_FOREACH(active_conn_head, elt) {
        int this_queue_len = 0;
        struct fifo_entry *elt2;
        LL_COUNT(elt->fifo_head, elt2, this_queue_len);
        total_len_of_queues += this_queue_len;
    }
    return total_len_of_queues;
}

/******************************************************************************
 * FunctionName : user_tcp_sent_cb
 * Description  : data sent callback.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_tcp_sent_cb(void *arg) 
{
    //data sent successfully

    struct espconn *espconn = arg;

    wish_connection_t *cb_ctx = (wish_connection_t *) espconn->reverse;

    /* Find the active connection related to this espconn */
    struct active_conn_entry *conn = NULL;
    LL_FOREACH(active_conn_head, conn) {
        if (conn->espconn->reverse == cb_ctx) {
            if (conn->busy == false) {
                PORT_PRINTF("Not busy, this is not possible\n");
                return;
            }

            struct fifo_entry *e = conn->fifo_head;   /* Cannot be NULL in this situation */
            if (e != NULL) {
                LL_DELETE(conn->fifo_head, e);
                os_free(e->data);
                os_free(e);
            }
            else {
                PORT_PRINTF("fifo_head is null, this is not possible!\n");
            }
            
            /* Send next */
            int fifo_len = 0;
            LL_COUNT(conn->fifo_head, e, fifo_len);
    
            if (fifo_len == 0) {
                /* No longer busy */
                conn->busy = false;
                PORT_PRINTF("Send FIFO is now empty!\n");
            }
            else {
                /* Continue with sending next buffer */
                sint8 ret = espconn_send(conn->espconn, conn->fifo_head->data, conn->fifo_head->data_len);
                if (ret == ESPCONN_OK) {
                    /* Sending was OK. */
                }
                else if (ret == ESPCONN_MAXNUM) {
                    /* Buffers are currently full, can send later */
                    PORT_PRINTF("user_tcp_send_cb: send buffers full, will retry\n");
                }
                else {
                    /* failed send, and we don't think we can recover it. 
                     * ESPCONN_MEM out of memory
                     * ESPCONN_ARG illegal espconn structure
                     * ?? */
                    PORT_PRINTF("Failed sending data, dropping the data, ret = %d\n", ret);
                    struct fifo_entry *elt = NULL;
                    LL_DELETE(conn->fifo_head, elt);
                    os_free(elt->data);
                    os_free(elt);
                    conn->busy = false;
                    
                    struct wish_event ev = { .event_type = WISH_EVENT_REQUEST_CONNECTION_CLOSING, 
                        .context = cb_ctx };
                    wish_message_processor_notify(&ev);
                }
            }
            
            break;
        }
    }
}

static void cleanup_active_conn(wish_connection_t* ctx) {
    PORT_PRINTF("cleanup_active_conn\n");
    /* Find the active connection related to this espconn */
    struct active_conn_entry *conn = NULL;
    struct active_conn_entry *tmp = NULL;
    LL_FOREACH_SAFE(active_conn_head, conn, tmp) {
        if (conn->espconn->reverse == (void*) ctx) {
            struct fifo_entry *e;
            struct fifo_entry *tmp_inner = NULL;
            LL_FOREACH_SAFE(active_conn_head->fifo_head, e, tmp_inner) {
                LL_DELETE(active_conn_head->fifo_head, e);
                os_free(e->data);
                os_free(e);
            }
            LL_DELETE(active_conn_head, conn);
            os_free(conn);
            break;
        }
    }
    //PORT_PRINTF("cleanup_active_conn (exit)\n");
}

/******************************************************************************
 * FunctionName : user_tcp_discon_cb
 * Description  : disconnect callback.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_tcp_discon_cb(void *arg)
{
    struct espconn *espconn = arg;

    os_free(espconn->proto.tcp);
    os_free(espconn);

    PORT_PRINTF("tcp_discon_cb IP: %d.%d.%d.%d\n\r", 
        espconn->proto.tcp->remote_ip[0],
        espconn->proto.tcp->remote_ip[1],
        espconn->proto.tcp->remote_ip[2],
        espconn->proto.tcp->remote_ip[3]);

    wish_connection_t *ctx = espconn->reverse;
    if (ctx == NULL) {
        PORT_PRINTF("Connection close: wish context not found!\n");
        return;
    }

    cleanup_active_conn(ctx);
    wish_core_signal_tcp_event(user_get_core_instance(), ctx, TCP_DISCONNECTED);
}

/******************************************************************************
 * FunctionName : user_tcp_server_discon_cb
 * Description  : disconnect callback, called when connection to the remote client has been disconnected.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_tcp_server_discon_cb(void *arg)
{
    //tcp disconnect successfully
    struct espconn *espconn = arg;
    PORT_PRINTF("tcp_server_discon_cb IP: %d.%d.%d.%d\n\r", 
        espconn->proto.tcp->remote_ip[0],
        espconn->proto.tcp->remote_ip[1],
        espconn->proto.tcp->remote_ip[2],
        espconn->proto.tcp->remote_ip[3]);

    wish_connection_t *ctx = espconn->reverse;
    if (ctx == NULL) {
        PORT_PRINTF("Connection close: wish context not found!\n");
        return;
    }
    
    cleanup_active_conn(ctx);
    wish_core_signal_tcp_event(user_get_core_instance(), ctx, TCP_CLIENT_DISCONNECTED);  
}

/******************************************************************************
 * FunctionName : user_tcp_server_connect_cb
 * Description  : A new incoming tcp connection has been connected.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_tcp_server_connect_cb(void *arg)
{
    struct espconn *pespconn = arg;
    uint8_t null_wuid[WISH_ID_LEN] = { 0 };
    wish_connection_t* connection = wish_connection_init(user_get_core_instance(), null_wuid, null_wuid);
    if (connection == NULL) {
        PORT_PRINTF("We cannot accept incoming connections right now\n");
        /* We cannot call espconn_disconnect() here. But we have no connection either! Then we put the pespconn pointer to event metadata, and handle it explicitly in message processor task implementation. */
        struct wish_event ev = { .event_type = WISH_EVENT_REQUEST_CONNECTION_CLOSING, 
            .context = NULL,
            .metadata = pespconn };
        wish_message_processor_notify(&ev);
        return;
    }
    /* Register the reverse so that we can later for example clean up
     * the context if/when the connection closes */
    pespconn->reverse = connection;

    wish_core_register_send(user_get_core_instance(), connection, my_send_data, arg);
    /* Populate Wish connection structure with IP info */
    memcpy(connection->local_ip_addr, pespconn->proto.tcp->local_ip, 4);
    memcpy(connection->remote_ip_addr, pespconn->proto.tcp->remote_ip, 4);
    connection->local_port = pespconn->proto.tcp->local_port;
    connection->remote_port = pespconn->proto.tcp->remote_port;
 

    PORT_PRINTF("Incoming connection from %d.%d.%d.%d\r\n", 
        connection->remote_ip_addr[0], connection->remote_ip_addr[1], connection->remote_ip_addr[2], connection->remote_ip_addr[3]);

    espconn_regist_recvcb(pespconn, user_tcp_recv_cb);
    espconn_regist_sentcb(pespconn, user_tcp_sent_cb);
    espconn_regist_disconcb(pespconn, user_tcp_server_discon_cb);
    
    /* Set a 60 second inactivity timeout */
    espconn_regist_time(pespconn, 60, 1);
    /* Disable Nagle algorithm */
    if (espconn_set_opt(pespconn, ESPCONN_NODELAY) != 0) {
        PORT_PRINTF("Error disabling Nagle algorithm (incoming connection)");
    }
    
    struct active_conn_entry *conn_entry = (struct active_conn_entry*) os_malloc(sizeof (struct active_conn_entry));
    if (conn_entry != NULL) {
        conn_entry->busy = false;
        conn_entry->espconn = connection->send_arg;
        conn_entry->fifo_head = NULL;
        conn_entry->next = NULL;
        LL_APPEND(active_conn_head, conn_entry);
    }
    
    wish_core_signal_tcp_event(user_get_core_instance(), connection, TCP_CLIENT_CONNECTED);
}

/* Use this function the post data to be sent using TCP.
 *
 * Returns 0 if the data was queued and possibly sent, or -1 for a failed send */
static int ICACHE_FLASH_ATTR
my_send_data(void *pespconn, unsigned char* data, int len)
{
    //PORT_PRINTF("Send data\n");
    struct espconn* espconn = (struct espconn*) pespconn;

    struct fifo_entry *e = (struct fifo_entry *) os_malloc(sizeof (struct fifo_entry));
    if (e == NULL) {
        PORT_PRINTF("Out of memory when sending data!\n");
        return -1;
    }
    memset(e, 0, sizeof (struct fifo_entry));
    e->data = (char *) os_malloc(len);
    if (e->data == NULL) {
        PORT_PRINTF("Out of memory when sending data!\n");
        return -1;
    }
    memcpy(e->data, data, len);
    e->data_len = len;

    struct active_conn_entry *conn;
    LL_FOREACH(active_conn_head, conn) {
        if (conn->espconn->reverse == espconn->reverse) {
            //PORT_PRINTF("Append\n");
            LL_APPEND(conn->fifo_head, e);

            if (conn->busy) {
                PORT_PRINTF("Send deferred!\n");
            }
            else {
                /* busy is false */
                PORT_PRINTF("Sending now!\n");
                sint8 ret = espconn_send(conn->espconn, conn->fifo_head->data, conn->fifo_head->data_len);
                
                if ( ret == ESPCONN_OK) {
                    /* Success */
                    conn->busy = true;
                }
                else if ( ret == ESPCONN_MAXNUM) {
                    PORT_PRINTF("my_send_data: Send buffers full - will try later\n");
                }
                else {
                    /* failed send, and we don't think we can recover it. 
                     * ESPCONN_MEM out of memory
                     * ESPCONN_ARG illegal espconn structure
                     * ?? */
                    PORT_PRINTF("Failed sending data, dropping the data, ret = %d\n", ret);
                    LL_DELETE(conn->fifo_head, e);
                    os_free(e->data);
                    os_free(e);
                    
                    struct wish_event ev = { .event_type = WISH_EVENT_REQUEST_CONNECTION_CLOSING, 
                        .context = espconn->reverse };
                    wish_message_processor_notify(&ev);
                    
                    return -1;
                }
            }
            break;
        }
    }
    //PORT_PRINTF("Send data exit\n");
    return 0;
}


/******************************************************************************
 * FunctionName : user_tcp_connect_cb
 * Description  : A new incoming tcp connection has been connected.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_tcp_connect_cb(void *arg)
{
    struct espconn *pespconn = arg;

    espconn_regist_recvcb(pespconn, user_tcp_recv_cb);
    espconn_regist_sentcb(pespconn, user_tcp_sent_cb);
    espconn_regist_disconcb(pespconn, user_tcp_discon_cb);

    wish_connection_t *connection = (wish_connection_t *) pespconn->reverse;

    wish_core_register_send(user_get_core_instance(), connection, my_send_data, arg);
    /* Populate Wish connection structure with IP info */
    memcpy(connection->local_ip_addr, pespconn->proto.tcp->local_ip, 4);
    memcpy(connection->remote_ip_addr, pespconn->proto.tcp->remote_ip, 4);
    connection->local_port = pespconn->proto.tcp->local_port;
    connection->remote_port = pespconn->proto.tcp->remote_port;
 
    PORT_PRINTF("Connected to %d.%d.%d.%d:%d\r\n", 
        connection->remote_ip_addr[0], connection->remote_ip_addr[1], connection->remote_ip_addr[2], connection->remote_ip_addr[3], connection->remote_port);

    /* Disable Nagle algorithm */
    if (espconn_set_opt(pespconn, ESPCONN_NODELAY) != 0) {
        PORT_PRINTF("Error disabling Nagle algorithm (outgoing connection)");
    }
    
    struct active_conn_entry *conn_entry = (struct active_conn_entry*) os_malloc(sizeof (struct active_conn_entry));
    if (conn_entry != NULL) {
        conn_entry->busy = false;
        conn_entry->espconn = connection->send_arg; 
        conn_entry->fifo_head = NULL;
        conn_entry->next = NULL;
        LL_APPEND(active_conn_head, conn_entry);
    }

    if (connection->via_relay) {
        /* For connections opened by relay client to accept an 
         * incoming connection */
        wish_core_signal_tcp_event(user_get_core_instance(), connection, TCP_RELAY_SESSION_CONNECTED);
    } else {
        /* For connections opened normally */
        wish_core_signal_tcp_event(user_get_core_instance(), connection, TCP_CONNECTED);
    }
}

/******************************************************************************
 * FunctionName : user_tcp_recon_cb
 * Description  : reconnect callback, error occured in TCP connection.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_tcp_recon_cb(void *arg, sint8 err)
{
    //PORT_PRINTF("reconnect callback, error code %d !!! \r\n", err);
    //error occured , tcp connection broke. user can try to reconnect here.
    if (arg == NULL) {
        PORT_PRINTF("user_tcp_recon_cb arg is NULL");
        return;
    }
    struct espconn *espconn = arg;
    PORT_PRINTF("error occurred (outgoing connection), IP: %d.%d.%d.%d err=%d\n\r", 
        espconn->proto.tcp->remote_ip[0],
        espconn->proto.tcp->remote_ip[1],
        espconn->proto.tcp->remote_ip[2],
        espconn->proto.tcp->remote_ip[3],
        err);
    wish_connection_t* connection 
        = wish_identify_context(user_get_core_instance(), espconn->proto.tcp->remote_ip,
            espconn->proto.tcp->remote_port, 
            espconn->proto.tcp->local_ip,
            espconn->proto.tcp->local_port);
    
    /* FIXME: Determine if this should always be called, or only when we actually had a connection?
     In practice we have seen ESPCONN_RST (-9) */
    struct wish_event ev = { .event_type = WISH_EVENT_REQUEST_CONNECTION_ABORT, 
        .context = NULL,
        .metadata = espconn };
    wish_message_processor_notify(&ev);
    
    if (connection == NULL) {
        //os_free(espconn->proto.tcp);
        //os_free(espconn);
        return;
    }

    
    cleanup_active_conn(connection);
    wish_core_signal_tcp_event(user_get_core_instance(), connection, TCP_DISCONNECTED);
    
    //os_free(espconn->proto.tcp);
    //os_free(espconn);
}

/******************************************************************************
 * FunctionName : user_tcp_server_recon_cb
 * Description  : reconnect callback, error occured in TCP connection.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_tcp_server_recon_cb(void *arg, sint8 err)
{
    //error occured , tcp connection broke. user can try to reconnect here. 
    struct espconn *espconn = arg;
    PORT_PRINTF("error occurred (incoming connection) IP: %d.%d.%d.%d err=%d\n\r", 
        espconn->proto.tcp->remote_ip[0],
        espconn->proto.tcp->remote_ip[1],
        espconn->proto.tcp->remote_ip[2],
        espconn->proto.tcp->remote_ip[3],
        err);
    
    wish_connection_t* connection 
        = wish_identify_context(user_get_core_instance(), espconn->proto.tcp->remote_ip,
            espconn->proto.tcp->remote_port, 
            espconn->proto.tcp->local_ip,
            espconn->proto.tcp->local_port);
    
        
    /* FIXME: Determine if this should always be called, or only when we actually had a connection?
     In practice we have seen ESPCONN_RST (-9) */
    struct wish_event ev = { .event_type = WISH_EVENT_REQUEST_CONNECTION_ABORT, 
        .context = NULL,
        .metadata = espconn };
    wish_message_processor_notify(&ev);
    
    
    if (connection == NULL) {
        return;
    }
    
    cleanup_active_conn(connection);
    wish_core_signal_tcp_event(user_get_core_instance(), connection, TCP_DISCONNECTED);  
}



/*
 * Setup callbacks and initiate TCP connection to specified IP address.
 * The a copy is made of the ip address parameter.
 *
 * Retuns the value one of the values espconn_connect(). Note that in
 * case of badly initialised struct espconn, ESPCONN_ARG is returned 
 * before any other setup is performed.
 */
LOCAL sint8 user_connect_tcp(struct espconn* espconn, ip_addr_t* ipaddr, uint16_t port) {
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
    else if (ipaddr == 0) {
        err = ESPCONN_ARG;
    }
    else {
        /* struct espconn seems acceptable */

        os_memcpy(espconn->proto.tcp->remote_ip, ipaddr, 4);

        espconn->proto.tcp->remote_port = port;       // remote port

        espconn->proto.tcp->local_port = espconn_port();   //local port of ESP8266

        espconn_regist_connectcb(espconn, user_tcp_connect_cb);  // register connect callback
        espconn_regist_reconcb(espconn, user_tcp_recon_cb);      // register reconnect callback as error handler
        espconn_connect(espconn);
    }

    return err;
}

#ifdef DNS_ENABLE
/******************************************************************************
 * FunctionName : user_dns_found
 * Description  : dns found callback
 * Parameters   : name -- pointer to the name that was looked up.
 *                ipaddr -- pointer to an ip_addr_t containing the IP address of
 *                the hostname, or NULL if the name could not be found (or on any
 *                other error).
 *                callback_arg -- a user-specified callback argument passed to
 *                dns_gethostbyname
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_dns_found(const char *name, ip_addr_t * ipaddr, void *arg)
{
    struct espconn *pespconn = (struct espconn *) arg;

    if (ipaddr == NULL) {
        PORT_PRINTF("user_dns_found NULL \r\n");
        return;
    }

    //dns got ip
    PORT_PRINTF("user_dns_found %d.%d.%d.%d \r\n",
              *((uint8 *) & ipaddr->addr), *((uint8 *) & ipaddr->addr + 1),
              *((uint8 *) & ipaddr->addr + 2),
              *((uint8 *) & ipaddr->addr + 3));

    if (tcp_server_ip.addr == 0 && ipaddr->addr != 0) {
        // dns succeed, create tcp connection
        os_timer_disarm(&test_timer);
        user_connect_tcp(pespconn, ipaddr);

    }
}

/******************************************************************************
 * FunctionName : user_esp_platform_dns_check_cb
 * Description  : 1s time callback to check dns found
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_dns_check_cb(void *arg)
{
    struct espconn *pespconn = arg;

    espconn_gethostbyname(pespconn, TEST_HOSTNAME, &tcp_server_ip, user_dns_found);     // recall DNS function

    os_timer_arm(&test_timer, 1000, 0);
}


#endif

/******************************************************************************
 *
 * FunctionName : user_check_ip
 * Description  : check whether get ip addr or not
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_check_ip(void)
{
    struct ip_info ipconfig;

    //disarm timer first
    os_timer_disarm(&test_timer);

    //get ip info of ESP8266 station
    wifi_get_ip_info(STATION_IF, &ipconfig);
    //PORT_PRINTF("wifi connect status = %d\n\r", wifi_station_get_connect_status());
    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0) {
        PORT_PRINTF("user_check_ip: Got IP address.\r\n");

        static bool server_started = false;
        if (!server_started) {
            /* Only start the server once */
            user_start_server();
            server_started = true;

            /* Start broadcasting 'advertizements' for our identity */
            wish_ldiscover_enable_bcast(user_get_core_instance());

            /* Setup autodiscvoery UDP listening */
            wish_ldiscover_enable_recv(user_get_core_instance());
            
       }

    }
    else {

        if ((wifi_station_get_connect_status() == STATION_WRONG_PASSWORD ||
             wifi_station_get_connect_status() == STATION_NO_AP_FOUND ||
             wifi_station_get_connect_status() == STATION_CONNECT_FAIL)) {
            PORT_PRINTF("user_check_ip: Connect fail status=%d\r\n", wifi_station_get_connect_status());
        }
        else {
            //re-arm timer to check ip
            os_timer_setfn(&test_timer,
                           (os_timer_func_t *) user_check_ip, NULL);
            os_timer_arm(&test_timer, 100, 0);
        }
    }
}


void ICACHE_FLASH_ATTR user_tcp_setup_dhcp_check(void) {
    //set a timer to check whether got ip from router succeed or not.
    os_timer_disarm(&test_timer);
    os_timer_setfn(&test_timer, (os_timer_func_t *) user_check_ip, NULL);
    os_timer_arm(&test_timer, 100, 0);
}

/* TCP connection structures for the server */
struct espconn server_espconn;
esp_tcp server_esp_tcp;

/*
 * See http://bbs.espressif.com/viewtopic.php?f=31&t=763
 * for an extended server example */
void user_start_server(void) {

    server_espconn.proto.tcp = &server_esp_tcp;
    server_espconn.type = ESPCONN_TCP;
    server_espconn.state = ESPCONN_NONE;

    server_espconn.proto.tcp->local_port = wish_get_host_port(user_get_core_instance());

    espconn_regist_connectcb(&server_espconn, user_tcp_server_connect_cb);  // register connect callback
    espconn_regist_reconcb(&server_espconn, user_tcp_server_recon_cb);      // register reconnect callback as error handler
    espconn_accept(&server_espconn); 
    espconn_tcp_set_max_con_allow(&server_espconn, USER_MAX_SERVER_TCP_CONNECTIONS);
}

void user_stop_server(void) {
    if (espconn_delete(&server_espconn)) {
        PORT_PRINTF("Could not stop server correctly\n");
    }
}


int wish_open_connection(wish_core_t *core, wish_connection_t *ctx, wish_ip_addr_t *ip, uint16_t port, bool via_relay) {
    //PORT_PRINTF("Open connection\n\r");

    /* Allocate the espconn structure for client use. This will be
     * de-allocated in the client disconnect callback, or the
     * reconnect callback */
    struct espconn* espconn = (struct espconn*) os_malloc(sizeof(struct espconn));
    memset(espconn, 0, sizeof(struct espconn));
    esp_tcp* user_tcp = (esp_tcp*) os_malloc(sizeof(esp_tcp)); 
    memset(user_tcp, 0, sizeof(esp_tcp));
    espconn->proto.tcp = user_tcp;
    espconn->type = ESPCONN_TCP;
    espconn->state = ESPCONN_NONE;

    /* Save the Wish context in the 'reverse' pointer of the espconn
     * struct. This is especially reserved for user-specified stuff */
    espconn->reverse = (void *) ctx;

    ctx->via_relay = via_relay;

    /* Connect to a Wish system */
    ip_addr_t esp_tcp_server_ip;
    IP4_ADDR(&esp_tcp_server_ip, ip->addr[0], ip->addr[1], ip->addr[2], ip->addr[3]);

    user_connect_tcp(espconn, &esp_tcp_server_ip, port);


    return 0;
}


void wish_close_connection(wish_core_t *core, wish_connection_t *ctx) {
    if (ctx->context_state != WISH_CONTEXT_CLOSING) {
        ctx->context_state = WISH_CONTEXT_CLOSING;
        ctx->close_timestamp = wish_time_get_relative(core);
    }
   
    if (wish_time_get_relative(core) > (ctx->close_timestamp + 5)) {
        /* We have postponed closing for too long. This could happen if for some reason the disconnect cb is not invoked by the stack.
         * Now discard any queued data and clear the connection */
        cleanup_active_conn(ctx);
        wish_core_signal_tcp_event(core, ctx, TCP_DISCONNECTED);
    }
    else {
        if (ctx->send_arg != NULL) {
            PORT_PRINTF("Explicit connection disconnect\n\r");
            int8_t ret = espconn_disconnect(ctx->send_arg);
            if (ret != 0) {
                PORT_PRINTF("Disconnect fail\n\r");
            }
        }
        else {
            /* espconn is null. Perhaps this occurs because the connection was never properly
             * started? */
            PORT_PRINTF("Skipping espconn disconnect, espconn struct is null.");
            /* The connection is not really opened anyway, so it should be safe to just discard the connection. */
            cleanup_active_conn(ctx);
            wish_core_signal_tcp_event(core, ctx, TCP_DISCONNECTED);
        }
    }
}

int wish_get_host_ip_str(wish_core_t *core, char* addr_str, size_t addr_str_len) {
    if (addr_str_len < 4*3+3+1) {
        PORT_PRINTF("IP addr buffer too small\n\r");
    }
    struct ip_info info;

    if (user_wifi_get_mode() == USER_WIFI_MODE_SETUP) {
        wifi_get_ip_info(0x01, &info);  /* Get for softAP interface */
    }
    else {
        wifi_get_ip_info(0x00, &info);  /* Get for station interface */
    }
    os_sprintf(addr_str, IPSTR, IP2STR(&info.ip));
    return 0;


}


int wish_get_host_port(wish_core_t *core) {
    return 37008;
}
