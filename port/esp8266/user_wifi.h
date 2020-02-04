#ifndef USER_WIFI_H
#define USER_WIFI_H
#include "user_hw_config.h"

#define SSID_NAME_MAX_LEN 32

#define MAX_NUM_SSIDS 100

enum user_wifi_mode { USER_WIFI_MODE_SETUP, USER_WIFI_MODE_STATION  };
enum user_wifi_mode user_wifi_get_mode(void);

/* SSID: "mist" + "-" + customer_defined_str[20] + last_6_bytes_of_mac */
#define MIST_CONFIG_SOFTAP_SSID_PREFIX "mist-"
#define MIST_CONFIG_SOFTAP_CH 3

#define MIST_CONFIG_SSID_CUSTOMER_STRING_MAX_LEN 20

#ifdef SONOFF_HARDWARE
#define MIST_CONFIG_SSID_CUSTOMER_STRING "Sonoff S20"
#else
#ifdef OLIMEX_HARDWARE
#define MIST_CONFIG_SSID_CUSTOMER_STRING "ESP8266 EVB"
#endif
#endif

/** This function will setupt a MistConfig Mist device for Wifi
 * commissioning purposes */
void user_wifi_setup_commissioning(void);

/** Start a Wifi access point scan */
void user_wifi_setup_ap_scan(void);

/* Public entrypoint to wifi setup. */
void user_setup_wifi(void);

/* Get list of ssids from latest AP scan */
char ** user_wifi_get_ssids(void);

/* Get list of rssi values from latest scan, the list corresponds with
 * the AP entries from user_wifi_get_ssids() (same indices apply) */
int8_t *user_wifi_get_rssis(void);

/* Format SSID string according to Mist Commissioning specification
 * @param ssid_buffer the buffer to write to
 * @return the length
 */
int user_wifi_format_ssid_str(char *ssid_buffer);

/** 
 * Set wifi mode to station mode
 * @see user_set_station_config()
 */
void user_wifi_set_station_mode(void);


void user_set_station_config(char *ssid, char *password);

void user_wifi_schedule_reboot(void);

#endif // USER_WIFI_H
