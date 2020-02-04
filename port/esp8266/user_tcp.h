#ifndef USER_TCP_H
#define USER_TCP_H

void user_start_server(void);
void user_stop_server(void);
void user_tcp_setup_dhcp_check(void);
int user_get_send_queue_len(void);

void user_hold(void);
void user_unhold(void);

#endif
