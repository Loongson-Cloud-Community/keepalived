/*
 * Soft:        Keepalived is a failover program for the LVS project
 *              <www.linuxvirtualserver.org>. It monitor & manipulate
 *              a loadbalanced server pool using multi-layer checks.
 *
 * Part:        TCP checker.
 *
 * Version:     $Id: check_tcp.c,v 0.5.7 2002/05/02 22:18:07 acassen Exp $
 *
 * Author:      Alexandre Cassen, <acassen@linux-vs.org>
 *
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *              See the GNU General Public License for more details.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 */

#include "check_tcp.h"
#include "check_api.h"
#include "memory.h"
#include "ipwrapper.h"
#include "layer4.h"
#include "smtp.h"
#include "utils.h"
#include "parser.h"

int tcp_connect_thread(thread *);

/* Configuration stream handling */
void free_tcp_check(void *data)
{
  tcp_checker *tcp_chk = CHECKER_DATA(data);

  FREE(tcp_chk);
  FREE(data);
}
void dump_tcp_check(void *data)
{
  tcp_checker *tcp_chk = CHECKER_DATA(data);

  syslog(LOG_INFO, "   Keepalive method = TCP_CHECK");
  syslog(LOG_INFO, "   Connection timeout = %d"
                 , tcp_chk->connection_to);
}
void tcp_check_handler(vector strvec)
{
  tcp_checker *tcp_chk = (tcp_checker *)MALLOC(sizeof(tcp_checker));

  /* queue new checker */
  queue_checker(free_tcp_check, dump_tcp_check
                              , tcp_connect_thread
                              , tcp_chk);
}
void connect_timeout_handler(vector strvec)
{
  tcp_checker *tcp_chk = CHECKER_GET();
  tcp_chk->connection_to = CHECKER_VALUE_INT(strvec);
}
void install_tcp_check_keyword(void)
{
  install_keyword("TCP_CHECK",		&tcp_check_handler);
  install_sublevel();
    install_keyword("connect_timeout",	&connect_timeout_handler);
  install_sublevel_end();
}

int tcp_check_thread(thread *thread)
{
  checker *checker;
  int status;

  checker = THREAD_ARG(thread);

  status = tcp_socket_state(thread->u.fd, thread
                                        , CHECKER_RIP(checker)
                                        , CHECKER_RPORT(checker)
                                        , tcp_check_thread);

  /* If status = connect_success, TCP connection to remote host is established.
   * Otherwise we have a real connection error or connection timeout.
   */
  if (status == connect_success) {

#ifdef _DEBUG_
    syslog(LOG_DEBUG, "TCP connection to [%s:%d] success."
                    ,  ip_ntoa(CHECKER_RIP(checker))
                    ,  ntohs(CHECKER_RPORT(checker)));
#endif
    close(thread->u.fd);

    if (!ISALIVE(checker->rs)) {
      smtp_alert(thread->master, checker->rs
                               , NULL
                               , "UP"
                               , "=> TCP CHECK succeed on service <=\n\n");
      perform_svr_state(UP, checker->vs, checker->rs);
    }

  } else {
#ifdef _DEBUG_
    syslog(LOG_DEBUG, "TCP connection to [%s:%d] failed !!!"
                    ,  ip_ntoa(CHECKER_RIP(checker))
                    ,  ntohs(CHECKER_RPORT(checker)));
#endif

    if (ISALIVE(checker->rs)) {
      smtp_alert(thread->master, checker->rs
                               , NULL
                               , "DOWN"
                               , "=> TCP CHECK failed on service <=\n\n");
      perform_svr_state(DOWN, checker->vs, checker->rs);
    }

  }

  /* Register next timer checker */
  if (status != connect_in_progress)
    thread_add_timer(thread->master, tcp_connect_thread
                                   , checker
                                   , checker->vs->delay_loop);
  return 0;
}

int tcp_connect_thread(thread *thread)
{
  checker *checker;
  tcp_checker *tcp_check;
  int fd;
  int status;

  checker   = THREAD_ARG(thread);
  tcp_check = CHECKER_ARG(checker);

  if ( (fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1 ) {
#ifdef _DEBUG_
    syslog(LOG_DEBUG, "TCP connect fail to create socket.");
#endif
    return 0;
  }

  status = tcp_connect(fd, CHECKER_RIP(checker)
                         , CHECKER_RPORT(checker));

  /* handle tcp connection status & register check worker thread */
  tcp_connection_state(fd, status, thread
                                 , tcp_check_thread
                                 , tcp_check->connection_to);
  return 0;
}
