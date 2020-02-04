/*
 * ESP8266 Wish port main program file. Entry point is user_init(). 
 */
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_task.h"
#include "c_types.h"
#include "ip_addr.h"
#include "espconn.h"
#include "mem.h"
#include "user_interface.h"
#include "espmissingincludes.h"

#include "driver/uart.h"

#include "wish_connection.h"
#include "wish_dispatcher.h"
#include "wish_fs.h"
#include "wish_identity.h"
#include "wish_time.h"
#include "wish_event.h"
#include "wish_platform.h"
#include "mbedtls/platform.h"
#include "user_captive_portal.h"

#include "user_wifi.h"
#include "user_tcp_client.h"
#include "user_relay.h"
#include "user_task.h"
#include "spiffs_integration.h"
#include "user_hw_config.h"
#include "user_support.h"
#include "user_main.h"
#include "port_printf.h"

#ifdef OLIMEX_HARDWARE
#include "mist_esp8266_evb_app.h"
#else
#ifdef SONOFF_HARDWARE
#include "mist_esp8266_sonoff_app.h"
#else
#error no hardware defined
#endif
#endif
#include "mist_config.h"

/** The Wish core instance running on this host */
static wish_core_t core;

wish_core_t* user_get_core_instance(void) {
    return &core;
}

bool user_is_claimed(void) {
    return !core.config_skip_connection_acl;
}



/* A wrapper to "calloc" function, which will be used by mbedtls. The
 * wrapper is needed because the platform-supplied os_calloc is a macro
 * expanding to a function call with incompatible signature */
static void* my_calloc(size_t n_members, size_t size) {
    void* ptr = (void*) os_malloc(n_members*size);
    os_memset(ptr, 0, n_members*size);
    return ptr;
}

static void* my_malloc(size_t size) {
    return (void*) os_malloc(size);
}

static void* my_realloc(void *ptr, size_t size) {
    return (void*) os_realloc(ptr, size);
}

/* A wrapper to "free" function, which will be used by mbedtls. */
static void  my_free(void* ptr) {
    os_free(ptr);
}

static os_timer_t systick_timer;

/* Call-back functoin to the meminfo timer to periodically print memory
 * statistics */
static void ICACHE_FLASH_ATTR user_print_meminfo(void) {
    os_printf("\t*** Free heap size %d\n\r", system_get_free_heap_size());
    os_printf("\t*** Current amount of untouched stack: %d \n\r", user_find_stack_canary());
}

void ICACHE_FLASH_ATTR systick_timer_cb(void) {
    /* Report to Wish that one second has passed */
    wish_time_report_periodic(&core);

    static wish_time_t timestamp;
    wish_time_t now = wish_time_get_relative(&core);
    if (now >= (timestamp + 60)) {
        timestamp = now;
        os_printf("\t***\n\r");
        os_printf("\t*** System uptime %d minutes\n\r", now/60);
        user_print_meminfo();
        os_printf("\t***\n\r");
        user_signal_update_changed();
    }
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
    user_paint_stack();
    uart_init(BIT_RATE_115200, BIT_RATE_115200);
    system_print_meminfo();
    os_printf("SDK version:%s\n", system_get_sdk_version());
    os_printf("Port version: " PORT_VERSION "\n");
    user_print_meminfo();
    
    /* Setup system tick timer which supplies timebase to Wish */
    os_timer_disarm(&systick_timer);
    os_timer_setfn(&systick_timer, (os_timer_func_t *) systick_timer_cb, NULL);
    os_timer_arm(&systick_timer, 1000, 1);

    mbedtls_platform_set_calloc_free(my_calloc, my_free);

    wish_platform_set_malloc(my_malloc);
    wish_platform_set_realloc(my_realloc);
    wish_platform_set_free(my_free);
    wish_platform_set_rng((long int (*)(void))os_random);

    wish_fs_set_open(my_fs_open);
    wish_fs_set_read(my_fs_read);
    wish_fs_set_write(my_fs_write);
    wish_fs_set_lseek(my_fs_lseek);
    wish_fs_set_close(my_fs_close);
    wish_fs_set_rename(my_fs_rename);
    wish_fs_set_remove(my_fs_remove);

    my_spiffs_mount();
    //test_spiffs();
    wish_uid_list_elem_t uid_list[4];
    memset(uid_list, 0, sizeof (uid_list));

    int num_ids = wish_load_uid_list(uid_list, 4);
    PORT_PRINTF("Number of identities in db: %d\n", num_ids);

    int i = 0;
    for (i = 0; i < num_ids; i++) {
        wish_identity_t recovered_id;
        memset(&recovered_id, 0, sizeof (wish_identity_t));
        wish_identity_load(uid_list[i].uid, &recovered_id);
        PORT_PRINTF("Loaded identity, alias: %s\n", recovered_id.alias);
    }

    if (num_ids <= 0) {
        PORT_PRINTF("Creating new identity.\n");
        /* Create new identity */
        wish_identity_t id;
        
        const int max_alias_len = 15;
#ifdef OLIMEX_HARDWARE      
        char *alias = "Olimex";
#else
#ifdef SONOFF_HARDWARE
        char *alias = "Sonoff";
#endif
#endif
        
        if (strnlen(alias, max_alias_len) == max_alias_len) {
            char *default_alias = "User";
            PORT_PRINTF("The alias %s is too long. Using '%s' instead\n", alias, default_alias);
            alias=default_alias;
        }
        /* Add some bytes of the mac addr in order to differentiate between ids */
        char alias_with_mac[max_alias_len+5];
        uint8_t ap_mac_addr[6] = { 0 };
        if (wifi_get_macaddr(SOFTAP_IF, ap_mac_addr) == false) {
            PORT_PRINTF("Failed to get mac addr");
        }
        
        wish_platform_sprintf(alias_with_mac, "%s %x:%x", alias, ap_mac_addr[4], ap_mac_addr[5]);
        wish_create_local_identity(&core, &id, alias_with_mac);
        wish_save_identity_entry(&id);
    }

    wish_core_init(&core);
    wish_core_update_identities(&core);

    mist_config_init();
#ifdef OLIMEX_HARDWARE
    mist_esp8266_evb_app_init();
#else
#ifdef SONOFF_HARDWARE
    mist_esp8266_sonoff_app_init();
#else
#error Hardware not defined
#endif
#endif

    wish_message_processor_init(&core);

    //init_captive_portal();

#if 1
    /* Setup wifi (depending on commissioning or normal mode) */
    user_setup_wifi();
#else
    /* Disable wifi alltogether */
    wifi_set_opmode(NULL_MODE); 
    wifi_station_set_auto_connect (0);
#endif
    /* Set the allowed number of TCP connections */
    const uint8_t max_num_tcp_connections = WISH_PORT_CONTEXT_POOL_SZ + 1;  /* +1 for relay control connection */
    if (espconn_tcp_set_max_con(max_num_tcp_connections) != 0) {
        PORT_PRINTF("Error setting up max tcp connections to %d", max_num_tcp_connections);
    }
}

uint32 user_rf_cal_sector_set(void) {
    /* RF_CAL_SEC_ADDR defined in Makefile as flash address, we need to
     * shift 3 bytes out of the address to get sector number */
    return RF_CAL_SEC_ADDR >> 24;
}

#if 0
/* Do-nothing ISR function for uart0 receive. 
 * This implementation is essential to have, becuase for some reason the 
 * Espressif-supplied uart driver does enable the interrupt, 
 * and defines this ISR routine to be called. */
void uart0_rx_intr_handler(void *para) {

}
#endif

