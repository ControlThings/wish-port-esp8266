#include <string.h>

#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "osapi.h"
#include "espmissingincludes.h"

#include "mist_app.h"
#include "mist_model.h"

#include "wish_debug.h"

#include "mist_mapping.h"
#include "mist_follow.h"
#include "wish_fs.h"

#include "user_task.h" 
#include "user_support.h"
#include "user_wifi.h"
#include "port_printf.h"

static char mist_app_name[30] = { 0 }; /* Need to have enough storage for: Sonoff S20 (ab:cd) but actually the name cannot be that long! */
#define RELAY_DEFAULT_STATE true
#define TOGGLE_RELAY_DEFAULT_STATE true

static enum mist_error hw_read(mist_ep* ep, wish_protocol_peer_t* peer, int id);
static enum mist_error mist_read(mist_ep* ep, wish_protocol_peer_t* peer, int id);
static enum mist_error hw_write(mist_ep* ep, wish_protocol_peer_t* peer, int id, bson* args);

static bool relay_state = RELAY_DEFAULT_STATE;
static mist_ep ep_relay = { .id = "relay", .label = "Relay", .type = MIST_TYPE_BOOL, .read = hw_read, .write = hw_write, .invoke = NULL};
static bool led_state = false;
static mist_ep ep_led =  { .id = "led", .label = "LED", .type = MIST_TYPE_BOOL, .read =  hw_read, .write = hw_write, .invoke = NULL };
volatile bool button_state = false; /* volatile, because it will be updated from ISR */
static mist_ep ep_button = { .id = "button", .label = "Button", .type = MIST_TYPE_BOOL, .read = hw_read, .write = NULL, .invoke = NULL };
static bool toggle_state = false;
static mist_ep ep_toggle = { .id = "toggle", .label = "Toggle", .type = MIST_TYPE_BOOL, .read = hw_read, .write = NULL, .invoke = NULL };
static bool toggle_relay = TOGGLE_RELAY_DEFAULT_STATE;
static mist_ep ep_toggle_relay = { .id = "toggle_relay", .label = "Toggle controls relay", .type = MIST_TYPE_BOOL, .read = hw_read, .write = hw_write, .invoke = NULL };

static mist_ep mist_super_ep = {.id = "mist", .label = "", .type = MIST_TYPE_STRING, .read = mist_read };

static mist_ep mist_name_ep = {.id = "name", .label = "Name", .type = MIST_TYPE_STRING, .read = mist_read };


mist_app_t *mist_app;

#define CONFIG_FILENAME "sonoff.bin"
#define CONFIG_DATA_LEN 1   /* uint8_t */

static void load_settings(void) {
    wish_file_t fd = wish_fs_open(CONFIG_FILENAME);
    if (fd <= 0) {
        PORT_PRINTF("Error opening file!\n");
        return;
    }
    wish_fs_lseek(fd, 0, WISH_FS_SEEK_SET);
    
    uint8_t config_data = 0;
    int read_ret = wish_fs_read(fd, (void*) &config_data, CONFIG_DATA_LEN);   
    if (read_ret == 0) {
        PORT_PRINTF("Empty file, apparently\n");
        
        relay_state = RELAY_DEFAULT_STATE;
        toggle_relay = TOGGLE_RELAY_DEFAULT_STATE;
    }
    else {
        relay_state = config_data & 1 ? true : false;
        toggle_relay = config_data & 2 ? true : false;
    }
    PORT_PRINTF("Load %x from file\n", config_data);
    wish_fs_close(fd);
}

static void write_settings() {
    wish_file_t fd = wish_fs_open(CONFIG_FILENAME);
    if (fd <= 0) {
        PORT_PRINTF("Error opening file!\n");
        return;
    }
    wish_fs_lseek(fd, 0, WISH_FS_SEEK_SET);
    
    uint8_t config_data = 0;
    config_data = relay_state ? 1 : 0;
    config_data |= ((toggle_relay) ? 1 : 0) << 1;
    wish_fs_write(fd, (void*) &config_data, CONFIG_DATA_LEN);
    PORT_PRINTF("Wrote %x to file\n", config_data);
    wish_fs_close(fd);
}

static void actuate_relay(bool new_state) {
    relay_state = new_state;
    if (relay_state) {
        gpio_output_set(BIT12, 0, BIT12, 0);
    }
    else {
        gpio_output_set(0, BIT12, BIT12, 0);
    }
}

static void toggle_led(bool new_state) {
    led_state = new_state;
    if (led_state) {
        gpio_output_set(0, BIT13, BIT13, 0);
    }
    else {
        gpio_output_set(BIT13, 0, BIT13, 0);
    }

}



/* This variable is incremented every time the button is pressed */
static volatile int btn_event_time = 0;
volatile bool btn_handled = false;
static volatile int curr_time = 0;

LOCAL os_timer_t button_timer;
const int button_timer_interval = 10; /* Milliseconds */

static int fine_grained_cnt = 0;
static uint32_t button_debounce_time = 0;
static bool button_debounce_state = false;


void button_timer_cb(void) {
    fine_grained_cnt++;

    if (system_get_time() - button_debounce_time > 100000) { /* unit: us */
        if (button_state != button_debounce_state) {
            button_state = button_debounce_state;
            btn_event_time = curr_time;
            mist_value_changed(mist_app, ep_button.id);
            
            if (button_state) {
                /* Toggle the state on positive edge */
                toggle_state = !toggle_state;
                mist_value_changed(mist_app, ep_toggle.id);
            }
            
            if (toggle_relay) {
                actuate_relay(toggle_state);
                mist_value_changed(mist_app, ep_relay.id);
                write_settings();
            }
            
        }
    }
    
    
    if (fine_grained_cnt < (1000/button_timer_interval)) {
        return;
    }
    /* We get here at 1 seconds intervals */
    fine_grained_cnt = 0;
    /* Detect a long press on the button */
    curr_time++;

    if (curr_time > btn_event_time + 10 && !btn_handled) {
        if (button_state == true) {
            PORT_PRINTF("Button was held down for 10 secs\n");
            btn_handled = true;
            user_delete_id_db();
            user_mappings_file_delete();
            system_restore();
        }
    }

    /* When we have detected a long press, start blinking the green led */
    static int cnt;
    if (btn_handled) {
        cnt++;
        if (curr_time % 2) {
            gpio_output_set(0, BIT13, BIT13, 0);
        }
        else {
            gpio_output_set(BIT13, 0, BIT13, 0);
        }
        if (cnt > 1) {
            system_restart();
        }
    }
}


/* ISR routine - notice how it it is explicitly declared to reside on
 * .text, which is mapped to iram by the ld script. All functions called
 * from an ISR routine must also reside on .text segment, else random
 * crashes will occur.
 */
void __attribute__((section(".text"))) gpio_isr(void *arg) {
    /* Temporarly disable GPIO interrupt and prepare for clearing the
     * interrupt */

    /* FIXME add debouncing */

    /* FIXME add GPIO status read */

    ETS_GPIO_INTR_DISABLE();
    uint32_t status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

    button_debounce_state = !GPIO_INPUT_GET(0);

    button_debounce_time = system_get_time();

    /* Clear interrupt and re-enable GPIO interrupt */
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, status);
    ETS_GPIO_INTR_ENABLE();
}

os_timer_t led_blink_timer;
const int led_blink_timer_interval = 100; /* milliseconds */

void led_blink_timer_cb(void) {
    switch (user_wifi_get_mode()) {
        case  USER_WIFI_MODE_STATION:
            if (wifi_station_get_connect_status() == STATION_GOT_IP) {
                /* Stop this timer */
                os_timer_disarm(&led_blink_timer);
                toggle_led(true);
                return;
            }
            else {
                /* Blink very rapidly */
                os_timer_arm(&led_blink_timer, led_blink_timer_interval, false);
            }
            break;
        case USER_WIFI_MODE_SETUP:
            /* Blink slowly */
            os_timer_arm(&led_blink_timer, 8*led_blink_timer_interval, true);
            break;
    }
    static bool led_state;
    toggle_led(led_state);
    led_state = !led_state;
}

/* Reader function for endpoint "mist" and "mist.name" */
static enum mist_error mist_read(struct mist_endpoint* ep, wish_protocol_peer_t* peer, int request_id) {
    PORT_PRINTF("mist_read: %s\n", ep->id);
    
    /* In this case we reply synchronously. Start building the response. */
    size_t result_max_len = 100;
    uint8_t result[result_max_len];
    bson bs;
    bson_init_buffer(&bs, result, result_max_len);
    
    char full_epid[MIST_EPID_LEN];
    mist_ep_full_epid(ep, full_epid);
    
    if (strncmp(ep->id, "mist", MIST_EPID_LEN) == 0) {
        bson_append_string(&bs, "data", "");
    }
    else if (strncmp(ep->id, "name", MIST_EPID_LEN) == 0) {
        bson_append_string(&bs, "data", mist_app_name);
    }
    
    bson_finish(&bs);
  
    mist_read_response(ep->model->mist_app, full_epid, request_id, &bs);
    PORT_PRINTF("Ret: mist_read\n");
  
    return MIST_NO_ERROR;
    
}


static enum mist_error hw_read(mist_ep* ep, wish_protocol_peer_t* peer, int request_id) {
    PORT_PRINTF("hw read: %s, type %d", ep->id, ep->type);
    
    size_t result_max_len = 100;
    uint8_t result[result_max_len];
    
    char full_epid[MIST_EPID_LEN];
    mist_ep_full_epid(ep, full_epid);
    
    bson bs;
    bson_init_buffer(&bs, result, result_max_len);
    
    if (ep == &ep_relay) {
        bson_append_bool(&bs, "data", relay_state);
    }
    else if (ep == &ep_led) {
        bson_append_bool(&bs, "data", led_state);
    }
    else if (ep == &ep_button) {
        bson_append_bool(&bs, "data", button_state);
    }
    else if (ep == &ep_toggle) {
        bson_append_bool(&bs, "data", toggle_state);
    }
    else if (ep == &ep_toggle_relay) {
        bson_append_bool(&bs, "data", toggle_relay);
    }
    bson_finish(&bs);
    mist_read_response(ep->model->mist_app, full_epid, request_id, &bs);
    
    return MIST_NO_ERROR;
}

static enum mist_error hw_write(mist_ep* ep,  wish_protocol_peer_t* peer, int request_id, bson* args) {
    PORT_PRINTF("hw write: %s, type %d\n", ep->id, ep->type);
    
    bson_iterator new_value_it;
    if ( BSON_EOO == bson_find(&new_value_it, args, "args") ) { 
        mist_write_error(ep->model->mist_app, ep->id, request_id, 7, "Bad BSON structure, no element 'data'");
        PORT_PRINTF("hw write reg: no element data\n");
        return MIST_ERROR; 
    }
    
    if (strncmp(ep->id, ep_relay.id, MIST_EPID_LEN) == 0) {
        bool value = bson_iterator_bool(&new_value_it);
        actuate_relay(value);
        write_settings();
        
    }
    else if (strncmp(ep->id, ep_led.id, MIST_EPID_LEN) == 0) {
        bool value = bson_iterator_bool(&new_value_it);
        toggle_led(value);
    }
    else if (ep == &ep_toggle_relay) {
        bool value = bson_iterator_bool(&new_value_it);
        toggle_relay = value;
        write_settings();
    }
    
 
    mist_write_response(ep->model->mist_app, ep->id, request_id);
    mist_value_changed(ep->model->mist_app, ep->id);

    return MIST_NO_ERROR;
}

static void init_hardware(void) {
    /* Configure the relay GPIO */
    // Initialize the GPIO subsystem.
    gpio_init();
    //Set GPIO12 to output mode (the relay)
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
    //Set GPIO12 hi
    actuate_relay(RELAY_DEFAULT_STATE);

    /* On-board green LED, GPIO13 */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
    /* Lit the LED (output low status).  */
    toggle_led(true);

    /* GPIO0 as button (input) */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
    PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO0_U);
    gpio_output_set(0, 0, 0, BIT0);
    GPIO_DIS_OUTPUT(BIT0);
    ETS_GPIO_INTR_ATTACH(gpio_isr, NULL);
    gpio_pin_intr_state_set(GPIO_ID_PIN(0), GPIO_PIN_INTR_ANYEDGE);
    ETS_GPIO_INTR_ENABLE();
}

void mist_esp8266_sonoff_app_init(void) {
    init_hardware();
    
    uint8_t ap_mac_addr[6] = { 0 };
    if (wifi_get_macaddr(SOFTAP_IF, ap_mac_addr) == false) {
        PORT_PRINTF("Failed to get mac addr");
    }
   
    wish_platform_sprintf(mist_app_name, "Sonoff S20 %x", ap_mac_addr[5]);
    wish_app_t *app;
    mist_app = start_mist_app();

    
    mist_model *model = &(mist_app->model);
    
    WISHDEBUG(LOG_CRITICAL, "Adding EPs");
    mist_ep_add(&(mist_app->model), NULL, &mist_super_ep);
    mist_ep_add(&(mist_app->model), mist_super_ep.id, &mist_name_ep);
    mist_ep_add(model, NULL, &ep_relay);
    mist_ep_add(model, NULL, &ep_led);
    mist_ep_add(model, NULL, &ep_button);
    mist_ep_add(model, NULL, &ep_toggle);
    mist_ep_add(model, NULL, &ep_toggle_relay); 
    WISHDEBUG(LOG_CRITICAL, "Finished with EPs");
      
    app = wish_app_create(mist_app_name);
    if (app == NULL) {
        WISHDEBUG(LOG_CRITICAL, "Failed creating wish app");
        return;
    }

    wish_app_add_protocol(app, &(mist_app->protocol));
    mist_app->app = app;
    wish_app_login(app);
    
    os_timer_disarm(&button_timer);
    os_timer_setfn(&button_timer, (os_timer_func_t *) button_timer_cb, NULL);
    os_timer_arm(&button_timer, button_timer_interval, true);
    
    os_timer_disarm(&led_blink_timer);
    os_timer_setfn(&led_blink_timer, (os_timer_func_t *) led_blink_timer_cb, NULL);
    os_timer_arm(&led_blink_timer, led_blink_timer_interval, true);

    /* Read in initial value for relay */
    load_settings();
    actuate_relay(relay_state);
    toggle_state = relay_state; // initialisation
}


