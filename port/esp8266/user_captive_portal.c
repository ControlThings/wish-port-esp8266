/* Built in DNS and HTTPD server which acts as a "captive portal"; the
 * DNS server will resolve any hostname to the IP of the ESP8266, and
 * the HTTP server will  */


#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_task.h"
#include "espmissingincludes.h"
#include "user_wifi.h"

typedef struct espconn* ConnTypePtr;

#include "httpd.h"
#include "captdns.h"

static int ICACHE_FLASH_ATTR cgiGenerateCaptivePortalLoginPage(HttpdConnData *connData);

HttpdBuiltInUrl builtInUrls[] = {
    //{"/index.cgi", cgiGenerateCaptivePortalLoginPage, NULL},
    {"/*", cgiGenerateCaptivePortalLoginPage, NULL},
    {"*", cgiRedirectToHostname, "192.168.4.1"},
    {NULL, NULL, NULL}
};

void init_captive_portal(void) {
    captdnsInit();
    httpdInit(builtInUrls, 80);
}


int ICACHE_FLASH_ATTR cgiGenerateCaptivePortalLoginPage(HttpdConnData *connData) {
    int max_output_len = 256;
    char output[max_output_len];   //Temporary buffer for HTML output

    //If the browser unexpectedly closes the connection, the CGI will be called 
    //with connData->conn=NULL. We can use this to clean up any data. It's not really
    //used in this simple CGI function.
    if (connData->conn==NULL) {
        //Connection aborted. Clean up.
        os_printf("HTTP: connection abort\n");
        return HTTPD_CGI_DONE;
    }

    if (connData->requestType!=HTTPD_METHOD_GET) {
        //Sorry, we only accept GET requests.
        httpdStartResponse(connData, 406);  //http error code 'unacceptable'
        httpdEndHeaders(connData);
        os_printf("HTTP: illegal method\n");
        return HTTPD_CGI_DONE;
    }


    os_printf("HTTP: Generating OK response\n");
    //Generate the header
    //We want the header to start with HTTP code 200, which means the document is found.
    httpdStartResponse(connData, 200); 
    //We are going to send some HTML.
    httpdHeader(connData, "Content-Type", "text/html");
    //No more headers.
    httpdEndHeaders(connData);

    //We're going to send the HTML as two pieces: a head and a body. We could've also done
    //it in one go, but this demonstrates multiple ways of calling httpdSend.
    //Send the HTML head. Using -1 as the length will make httpdSend take the length
    //of the zero-terminated string it's passed as the amount of data to send.
    httpdSend(connData, "<html><head><title></title></head>", -1);
    //Generate the HTML body. 
    char ssid[SSID_NAME_MAX_LEN];
    user_wifi_format_ssid_str(ssid);
    httpdSend(connData, "<body><H1>Get started with your Mist device!</H1><p>\
After you have installed the Wish and Mist apps on your smart phone, you can "\
        , -1);
    /* For Mist commissioning, an URL of this format should be given as
     * link:
     * wish://commissioning?ssid=YourSSIDhere&security=OPEN
     */
    int len=ets_sprintf(output, "<a href=\"wish://commissioning?ssid=%s&security=%s\">Start commissioning</a> your device!</p></body></html>", ssid, "OPEN");
    //Send HTML body to webbrowser. We use the length as calculated by sprintf here.
    //Using -1 again would also have worked, but this is more efficient.
    httpdSend(connData, output, len);

    //All done.
    return HTTPD_CGI_DONE;
}
