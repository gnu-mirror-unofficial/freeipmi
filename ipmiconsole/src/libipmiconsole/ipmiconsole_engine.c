/*****************************************************************************\
 *  $Id: ipmiconsole_engine.c,v 1.37 2007-08-20 20:05:54 chu11 Exp $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Albert Chu <chu11@llnl.gov>
 *  UCRL-CODE-221226
 *  
 *  This file is part of Ipmiconsole, a set of IPMI 2.0 SOL libraries
 *  and utilities.  For details, see http://www.llnl.gov/linux/.
 *  
 *  Ipmiconsole is free software; you can redistribute it and/or modify 
 *  it under the terms of the GNU General Public License as published by the 
 *  Free Software Foundation; either version 2 of the License, or (at your 
 *  option) any later version.
 *  
 *  Ipmiconsole is distributed in the hope that it will be useful, but 
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
 *  for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with Ipmiconsole; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#if STDC_HEADERS
#include <string.h>
#endif /* STDC_HEADERS */
#if HAVE_PTHREAD_H
#include <pthread.h>
#endif /* HAVE_PTHREAD_H */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif /* HAVE_FCNTL_H */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/poll.h>
#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else  /* !TIME_WITH_SYS_TIME */
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else /* !HAVE_SYS_TIME_H */
#include <time.h>
#endif  /* !HAVE_SYS_TIME_H */
#endif /* !TIME_WITH_SYS_TIME */
#include <assert.h>
#include <errno.h>
#include <freeipmi/freeipmi.h>

#include "ipmiconsole.h"
#include "ipmiconsole_defs.h"

#include "cbuf.h"
#include "list.h"
#include "timeval.h"
#include "ipmiconsole_engine.h"
#include "ipmiconsole_debug.h"
#include "ipmiconsole_fiid_wrappers.h"
#include "ipmiconsole_processing.h"

#include "freeipmi-portability.h"

/* 
 * Locking notes:
 *
 * when is_count mutex is locked - thread_count_mutex can be locked, not vice versa
 * when is_count mutex is locked - teardown_mutex can be locked, not vice versa
 * when thread_count mutex is locked - ctxs_mutex can be locked, not vice versa
 */
static int console_engine_is_setup = 0;
static pthread_mutex_t console_engine_is_setup_mutex = PTHREAD_MUTEX_INITIALIZER;

static int console_engine_thread_count = 0;
static pthread_mutex_t console_engine_thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;

static int console_engine_teardown = 0;
static int console_engine_teardown_immediate = 0;
static pthread_mutex_t console_engine_teardown_mutex = PTHREAD_MUTEX_INITIALIZER;

static List console_engine_ctxs[IPMICONSOLE_THREAD_COUNT_MAX];
static unsigned int console_engine_ctxs_count[IPMICONSOLE_THREAD_COUNT_MAX];
static pthread_mutex_t console_engine_ctxs_mutex[IPMICONSOLE_THREAD_COUNT_MAX];

/* In the core engine code, the poll() may sit for a large number of
 * seconds, waiting for the next event to happen.  In the meantime, a
 * user may have submitted a new context or wants to close the engine.
 * The poll() doesn't know this and will sit until it times out,
 * letting the user sit and wait for the engine loop to "come around
 * again" and start processing.  This pipe can be used to "interrupt"
 * the poll() when the user wants to get things moving a little
 * faster.
 */
static int console_engine_ctxs_notifier[IPMICONSOLE_THREAD_COUNT_MAX][2];
static int console_engine_ctxs_notifier_num = 0;

struct _ipmiconsole_poll_data {
  struct pollfd *pfds;
  ipmiconsole_ctx_t *pfds_ctxs;
  unsigned int ctxs_len;
  unsigned int pfds_index;
};

#define GETHOSTBYNAME_AUX_BUFLEN 1024

#define IPMICONSOLE_SPIN_WAIT_TIME 250000

#define IPMICONSOLE_PIPE_BUFLEN 1024

void
_ipmiconsole_init_ctx_managed_session_data(ipmiconsole_ctx_t c)
{
  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);

  /* init to -1 b/c -1 isn't a legit fd */
  c->user_fd = -1;
  c->asynccomm[0] = -1;
  c->asynccomm[1] = -1;
}

void
_ipmiconsole_cleanup_ctx_managed_session_data(ipmiconsole_ctx_t c)
{
  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);
  
  /* Closing handled elsewhere */
  if (!(c->engine_flags & IPMICONSOLE_ENGINE_CLOSE_FD))
    {
      if (!c->user_fd_retrieved)
        close(c->user_fd);
    }
  c->user_fd_retrieved = 0;
  close(c->asynccomm[0]);
  close(c->asynccomm[1]);
}

void
_ipmiconsole_cleanup_ctx_session(ipmiconsole_ctx_t c)
{
  struct ipmiconsole_ctx_session *s;
  int secure_malloc_flag;
  int perr;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);
  
  s = &(c->session);
  
  secure_malloc_flag = (c->security_flags & IPMICONSOLE_SECURITY_LOCK_MEMORY) ? 1 : 0;

  /* We have to cleanup, so continue on even if locking fails */

  if ((perr = pthread_mutex_lock(&(c->blocking_mutex))) != 0)
    IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));

  if (c->blocking_submit_requested
      && !c->sol_session_established)
    {
      uint8_t val;

      if (c->security_flags & IPMICONSOLE_SECURITY_DEACTIVATE_ONLY
          && s->deactivate_only_succeeded_flag)
        val = IPMICONSOLE_BLOCKING_NOTIFICATION_SOL_SESSION_DEACTIVATED;
      else
        val = IPMICONSOLE_BLOCKING_NOTIFICATION_SOL_SESSION_ERROR;

      if (write(c->blocking_notification[1], &val, 1) < 0)
        {
          IPMICONSOLE_CTX_DEBUG(c, ("write: %s", strerror(errno)));
          c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
        }
    }

  if ((perr = pthread_mutex_unlock(&(c->blocking_mutex))) != 0)
    IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));

  /* Under default circumstances, close only the ipmiconsole_fd so
   * that an error will be detected by the user via a EOF on a read()
   * or EPIPE on a write() when reading/writing on their file
   * descriptor.  The user is then required to close that fd.
   *
   * On error situations (i.e. ipmiconsole_engine_submit() doesn't
   * return to the user w/ success), it is the responsibility of other
   * code to call _ipmiconsole_cleanup_ctx_managed_session_data().
   *
   * The exception to this is when the user specifies the
   * IPMICONSOLE_ENGINE_CLOSE_FD flag.  Under that case, all bets are
   * off.
   */
  if (s->ipmiconsole_fd)
    close(s->ipmiconsole_fd);
  if (c->engine_flags & IPMICONSOLE_ENGINE_CLOSE_FD)
    {
      if (s->user_fd)
        close(s->user_fd);
    }
  if (s->console_remote_console_to_bmc)
    cbuf_destroy(s->console_remote_console_to_bmc, secure_malloc_flag);
  if (s->console_bmc_to_remote_console)
    cbuf_destroy(s->console_bmc_to_remote_console, secure_malloc_flag);
  if (s->ipmi_fd)
    close(s->ipmi_fd);
  if (s->ipmi_from_bmc)
    cbuf_destroy(s->ipmi_from_bmc, secure_malloc_flag);
  if (s->ipmi_to_bmc)
    cbuf_destroy(s->ipmi_to_bmc, secure_malloc_flag);
  /* Similarly to the user_fd above, it is the responsibility of other
   * code to close asynccomm[0] and asynccomm[1], which is replicated
   * in the context.
   */
  if (s->obj_rmcp_hdr_rq)
    Fiid_obj_destroy(c, s->obj_rmcp_hdr_rq);
  if (s->obj_rmcp_hdr_rs)
    Fiid_obj_destroy(c, s->obj_rmcp_hdr_rs);
  if (s->obj_lan_session_hdr_rq)
    Fiid_obj_destroy(c, s->obj_lan_session_hdr_rq);
  if (s->obj_lan_session_hdr_rs)
    Fiid_obj_destroy(c, s->obj_lan_session_hdr_rs);
  if (s->obj_lan_msg_hdr_rq)
    Fiid_obj_destroy(c, s->obj_lan_msg_hdr_rq);
  if (s->obj_lan_msg_hdr_rs)
    Fiid_obj_destroy(c, s->obj_lan_msg_hdr_rs);
  if (s->obj_lan_msg_trlr_rs)
    Fiid_obj_destroy(c, s->obj_lan_msg_trlr_rs);
  if (s->obj_rmcpplus_session_hdr_rq)
    Fiid_obj_destroy(c, s->obj_rmcpplus_session_hdr_rq);
  if (s->obj_rmcpplus_session_hdr_rs)
    Fiid_obj_destroy(c, s->obj_rmcpplus_session_hdr_rs);
  if (s->obj_rmcpplus_payload_rs)
    Fiid_obj_destroy(c, s->obj_rmcpplus_payload_rs);
  if (s->obj_rmcpplus_session_trlr_rq)
    Fiid_obj_destroy(c, s->obj_rmcpplus_session_trlr_rq);
  if (s->obj_rmcpplus_session_trlr_rs)
    Fiid_obj_destroy(c, s->obj_rmcpplus_session_trlr_rs);
  if (s->obj_authentication_capabilities_v20_rq)
    Fiid_obj_destroy(c, s->obj_authentication_capabilities_v20_rq);
  if (s->obj_authentication_capabilities_v20_rs)
    Fiid_obj_destroy(c, s->obj_authentication_capabilities_v20_rs);
  if (s->obj_open_session_request)
    Fiid_obj_destroy(c, s->obj_open_session_request);
  if (s->obj_open_session_response)
    Fiid_obj_destroy(c, s->obj_open_session_response);
  if (s->obj_rakp_message_1)
    Fiid_obj_destroy(c, s->obj_rakp_message_1);
  if (s->obj_rakp_message_2)
    Fiid_obj_destroy(c, s->obj_rakp_message_2);
  if (s->obj_rakp_message_3)
    Fiid_obj_destroy(c, s->obj_rakp_message_3);
  if (s->obj_rakp_message_4)
    Fiid_obj_destroy(c, s->obj_rakp_message_4);
  if (s->obj_set_session_privilege_level_rq)
    Fiid_obj_destroy(c, s->obj_set_session_privilege_level_rq);
  if (s->obj_set_session_privilege_level_rs)
    Fiid_obj_destroy(c, s->obj_set_session_privilege_level_rs);
  if (s->obj_get_channel_payload_support_rq)
    Fiid_obj_destroy(c, s->obj_get_channel_payload_support_rq);
  if (s->obj_get_channel_payload_support_rs)
    Fiid_obj_destroy(c, s->obj_get_channel_payload_support_rs);
  if (s->obj_get_payload_activation_status_rq)
    Fiid_obj_destroy(c, s->obj_get_payload_activation_status_rq);
  if (s->obj_get_payload_activation_status_rs)
    Fiid_obj_destroy(c, s->obj_get_payload_activation_status_rs);
  if (s->obj_activate_payload_rq)
    Fiid_obj_destroy(c, s->obj_activate_payload_rq);
  if (s->obj_activate_payload_rs)
    Fiid_obj_destroy(c, s->obj_activate_payload_rs);
  if (s->obj_sol_payload_data_rq)
    Fiid_obj_destroy(c, s->obj_sol_payload_data_rq);
  if (s->obj_sol_payload_data_rs)
    Fiid_obj_destroy(c, s->obj_sol_payload_data_rs);
  if (s->obj_get_channel_payload_version_rq)
    Fiid_obj_destroy(c, s->obj_get_channel_payload_version_rq);
  if (s->obj_get_channel_payload_version_rs)
    Fiid_obj_destroy(c, s->obj_get_channel_payload_version_rs);
  if (s->obj_deactivate_payload_rq)
    Fiid_obj_destroy(c, s->obj_deactivate_payload_rq);
  if (s->obj_deactivate_payload_rs)
    Fiid_obj_destroy(c, s->obj_deactivate_payload_rs);
  if (s->obj_close_session_rq)
    Fiid_obj_destroy(c, s->obj_close_session_rq);
  if (s->obj_close_session_rs)
    Fiid_obj_destroy(c, s->obj_close_session_rs);
  
  memset(s, '\0', sizeof(struct ipmiconsole_ctx_session));

  if ((perr = pthread_mutex_lock(&(c->exitted_mutex))) != 0)
    IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));

  c->exitted++;

  if ((perr = pthread_mutex_unlock(&(c->exitted_mutex))) != 0)
    IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
}

int
_ipmiconsole_init_ctx_session_data(ipmiconsole_ctx_t c)
{
  struct ipmiconsole_ctx_session *s;
#ifdef HAVE_FUNC_GETHOSTBYNAME_R_6
  struct hostent hent;
  int h_errnop;
  char buf[GETHOSTBYNAME_AUX_BUFLEN];
#endif /* HAVE_FUNC_GETHOSTBYNAME_R_6 */
  struct hostent *hptr;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);

  s = &(c->session);

  memset(&(s->addr), '\0', sizeof(struct sockaddr_in));
  s->addr.sin_family = AF_INET;
  s->addr.sin_port = htons(s->console_port);

#ifdef HAVE_FUNC_GETHOSTBYNAME_R_6
  memset(&hent, '\0', sizeof(struct hostent));
  if (gethostbyname_r(c->hostname,
                      &hent,
                      buf,
                      GETHOSTBYNAME_AUX_BUFLEN,
                      &hptr,
                      &h_errnop) != 0)
    {
      if (h_errnop == HOST_NOT_FOUND
          || h_errnop == NO_ADDRESS
          || h_errnop == NO_DATA)
        {
          c->errnum = IPMICONSOLE_ERR_HOSTNAME_INVALID;
          return -1;
        }
      IPMICONSOLE_DEBUG(("gethostbyname_r: %s", hstrerror(h_errnop)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      return -1;
    }

  if (!hptr)
    {
      c->errnum = IPMICONSOLE_ERR_HOSTNAME_INVALID;
      return -1;
    }
#else /* !HAVE_FUNC_GETHOSTBYNAME_R */
#error Additional threadsafe gethostbyname support needed
#endif /* !HAVE_FUNC_GETHOSTBYNAME_R */

  s->addr.sin_addr = *((struct in_addr *)hptr->h_addr);

  s->protocol_state = IPMICONSOLE_PROTOCOL_STATE_START;
  s->close_session_flag = 0;
  s->try_new_port_flag = 0;
  s->deactivate_payload_instances_and_try_again_flag = 0;
  s->close_timeout_flag = 0;
  s->deactivate_only_succeeded_flag = 0;

  s->retransmission_count = 0;
  s->errors_count = 0;
  s->session_sequence_number_errors_count = 0;
  s->deactivate_active_payloads_count = 0;
  s->highest_received_sequence_number = 0; /* so first packet received will be > 0 */
  s->previously_received_list = IPMI_SESSION_SEQUENCE_NUMBER_PREVIOUSLY_RECEIVED_LIST_INIT;

  if (ipmi_get_random(&(s->message_tag),
                      sizeof(s->message_tag)) < 0)
    {
      IPMICONSOLE_DEBUG(("ipmi_get_random: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }
  if (ipmi_get_random(&(s->requester_sequence_number),
                      sizeof(s->requester_sequence_number)) < 0)
    {
      IPMICONSOLE_DEBUG(("ipmi_get_random: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }
  s->requester_sequence_number %= (IPMI_LAN_REQUESTER_SEQUENCE_NUMBER_MAX + 1);

  s->session_sequence_number = 0; /* 0, so initial increment puts it at 1 */
  s->name_only_lookup = IPMI_NAME_ONLY_LOOKUP;

  /* In IPMI 2.0, session_ids of 0 are special */
  do
    {
      if (ipmi_get_random((uint8_t *)&(s->remote_console_session_id),
                          sizeof(s->remote_console_session_id)) < 0)
        {
          IPMICONSOLE_DEBUG(("ipmi_get_random: %s", strerror(errno)));
          c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
          return -1;
        }
    } while (!s->remote_console_session_id);

  if (ipmi_get_random(s->remote_console_random_number,
                      IPMI_REMOTE_CONSOLE_RANDOM_NUMBER_LENGTH) < 0)
    {
      IPMICONSOLE_DEBUG(("ipmi_get_random: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  /* Keys and ptrs will be calculated during session setup.  We just
   * memet/clear here.
   */
  memset(s->sik_key, '\0', IPMI_MAX_SIK_KEY_LENGTH);
  s->sik_key_ptr = s->sik_key;
  s->sik_key_len = IPMI_MAX_SIK_KEY_LENGTH;
  memset(s->integrity_key, '\0', IPMI_MAX_INTEGRITY_KEY_LENGTH);
  s->integrity_key_ptr = s->integrity_key;
  s->integrity_key_len = IPMI_MAX_INTEGRITY_KEY_LENGTH;
  memset(s->confidentiality_key, '\0', IPMI_MAX_CONFIDENTIALITY_KEY_LENGTH);
  s->confidentiality_key_ptr = s->confidentiality_key;
  s->confidentiality_key_len = IPMI_MAX_CONFIDENTIALITY_KEY_LENGTH;

  s->sol_payload_instance = IPMI_PAYLOAD_INSTANCE_DEFAULT;
  
  /* Following 3 will be calculated during session setup.  We only
   * memset/clear it here
   */
  s->sol_instance_capacity = 0;
  memset(s->sol_instances_activated, '\0', IPMI_INSTANCES_ACTIVATED_LENGTH);
  s->sol_instances_activated_count = 0;
  /* this is used just to index the number of instances deactivated */
  s->sol_instances_deactivated_count = 0;

  /* Calculated during the session setup. */
  s->max_sol_character_send_size = 0;
  
  /* SOL Session Maintenance */

  timeval_clear(&(s->last_keepalive_packet_sent));

  /* Serial Break Maintenance */
  s->break_requested = 0;
  s->console_remote_console_to_bmc_bytes_before_break = 0;

  /* SOL Input (remote console to BMC) */
  s->sol_input_waiting_for_ack = 0;
  s->sol_input_waiting_for_break_ack = 0;
  timeval_clear(&(s->last_sol_input_packet_sent));
  s->sol_input_packet_sequence_number = 0; /* 0, so initial increment puts it at 1 */
  memset(s->sol_input_character_data, '\0', IPMICONSOLE_MAX_CHARACTER_DATA+1);
  s->sol_input_character_data_len = 0;

  /* SOL Output (BMC to remote console) */
  s->last_sol_output_packet_sequence_number = 0;
  s->last_sol_output_accepted_character_count = 0;

  return 0;
}

int 
_ipmiconsole_init_ctx_session(ipmiconsole_ctx_t c)
{
  struct ipmiconsole_ctx_session *s;
  struct sockaddr_in srcaddr;
  int sv[2];
  int secure_malloc_flag;
  int closeonexec;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);
  assert(!(c->session_submitted));

  s = &(c->session);

  memset(s, '\0', sizeof(struct ipmiconsole_ctx_session));
  
  /* File Descriptor User Interface */

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    {
      IPMICONSOLE_DEBUG(("socketpair: %s", strerror(errno)));
      if (errno == EMFILE)
        c->errnum = IPMICONSOLE_ERR_TOO_MANY_OPEN_FILES;
      else
        c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }
  s->user_fd = sv[0];
  s->ipmiconsole_fd = sv[1];

  if ((closeonexec = fcntl(s->user_fd, F_GETFD, 0)) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }
  closeonexec |= FD_CLOEXEC;
  if (fcntl(s->user_fd, F_SETFD, closeonexec) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }

  if ((closeonexec = fcntl(s->ipmiconsole_fd, F_GETFD, 0)) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }
  closeonexec |= FD_CLOEXEC;
  if (fcntl(s->ipmiconsole_fd, F_SETFD, closeonexec) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }

  /* Copy for API level */
  c->user_fd = s->user_fd;

  secure_malloc_flag = (c->security_flags & IPMICONSOLE_SECURITY_LOCK_MEMORY) ? 1 : 0;

  if (!(s->console_remote_console_to_bmc = cbuf_create(CONSOLE_REMOTE_CONSOLE_TO_BMC_BUF_MIN, CONSOLE_REMOTE_CONSOLE_TO_BMC_BUF_MAX, secure_malloc_flag)))
    {
      IPMICONSOLE_DEBUG(("cbuf_create: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_OUT_OF_MEMORY;
      goto cleanup;
    }

  if (!(s->console_bmc_to_remote_console = cbuf_create(CONSOLE_BMC_TO_REMOTE_CONSOLE_BUF_MIN, CONSOLE_BMC_TO_REMOTE_CONSOLE_BUF_MAX, secure_malloc_flag)))
    {
      IPMICONSOLE_DEBUG(("cbuf_create: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_OUT_OF_MEMORY;
      goto cleanup;
    }

  /* Connection Data */

  if ((s->ipmi_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
      IPMICONSOLE_DEBUG(("socket: %s", strerror(errno)));
      if (errno == EMFILE)
        c->errnum = IPMICONSOLE_ERR_TOO_MANY_OPEN_FILES;
      else
        c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }

  if ((closeonexec = fcntl(s->ipmi_fd, F_GETFD, 0)) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }
  closeonexec |= FD_CLOEXEC;
  if (fcntl(s->ipmi_fd, F_SETFD, closeonexec) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }

  memset(&srcaddr, '\0', sizeof(struct sockaddr_in));
  srcaddr.sin_family = AF_INET;
  srcaddr.sin_port = htons(0);
  srcaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(s->ipmi_fd, (struct sockaddr *)&srcaddr, sizeof(struct sockaddr_in)) < 0)
    {
      IPMICONSOLE_DEBUG(("bind: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }
  
  /* Note: May be modified later based on results from Activate
   * Payload packet received
   */
  s->console_port = RMCP_PRIMARY_RMCP_PORT;

  if (!(s->ipmi_from_bmc = cbuf_create(IPMI_FROM_BMC_BUF_MIN, IPMI_FROM_BMC_BUF_MAX, secure_malloc_flag)))
    {
      IPMICONSOLE_DEBUG(("cbuf_create: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_OUT_OF_MEMORY;
      goto cleanup;
    }

  if (!(s->ipmi_to_bmc = cbuf_create(IPMI_TO_BMC_BUF_MIN, IPMI_TO_BMC_BUF_MAX, secure_malloc_flag)))
    {
      IPMICONSOLE_DEBUG(("cbuf_create: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_OUT_OF_MEMORY;
      goto cleanup;
    }

  timeval_clear(&(s->last_ipmi_packet_sent));
  /* Note:
   * Initial last_ipmi_packet_received to current time, so session
   * timeout can be calculated in the beginning if necessary.
   */
  if (gettimeofday(&(s->last_ipmi_packet_received), NULL) < 0)
    {
      IPMICONSOLE_DEBUG(("gettimeofday: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }  

  /* Pipe for non-fd communication */
  if (pipe(s->asynccomm) < 0)
    {
      IPMICONSOLE_DEBUG(("pipe: %s", strerror(errno)));
      if (errno == EMFILE)
        c->errnum = IPMICONSOLE_ERR_TOO_MANY_OPEN_FILES;
      else
        c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }

  if ((closeonexec = fcntl(s->asynccomm[0], F_GETFD, 0)) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }
  closeonexec |= FD_CLOEXEC;
  if (fcntl(s->asynccomm[0], F_SETFD, closeonexec) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }

  if ((closeonexec = fcntl(s->asynccomm[1], F_GETFD, 0)) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }
  closeonexec |= FD_CLOEXEC;
  if (fcntl(s->asynccomm[1], F_SETFD, closeonexec) < 0)
    {
      IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      goto cleanup;
    }

  /* Copy for API level */
  c->asynccomm[0] = s->asynccomm[0];
  c->asynccomm[1] = s->asynccomm[1];

  /* Data based on Configuration Parameters */

  if (ipmi_cipher_suite_id_to_algorithms(c->cipher_suite_id,
                                         &(s->authentication_algorithm),
                                         &(s->integrity_algorithm),
                                         &(s->confidentiality_algorithm)) < 0)
    {
      IPMICONSOLE_DEBUG(("ipmi_cipher_suite_id_to_algorithms: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      goto cleanup;
    }

  /* Fiid Objects */

  if (!(s->obj_rmcp_hdr_rq = Fiid_obj_create(c, tmpl_rmcp_hdr)))
    goto cleanup;
  if (!(s->obj_rmcp_hdr_rs = Fiid_obj_create(c, tmpl_rmcp_hdr)))
    goto cleanup;
  if (!(s->obj_lan_session_hdr_rq = Fiid_obj_create(c, tmpl_lan_session_hdr)))
    goto cleanup;
  if (!(s->obj_lan_session_hdr_rs = Fiid_obj_create(c, tmpl_lan_session_hdr)))
    goto cleanup;
  if (!(s->obj_lan_msg_hdr_rq = Fiid_obj_create(c, tmpl_lan_msg_hdr_rq)))
    goto cleanup;
  if (!(s->obj_lan_msg_hdr_rs = Fiid_obj_create(c, tmpl_lan_msg_hdr_rs)))
    goto cleanup;
  if (!(s->obj_lan_msg_trlr_rs = Fiid_obj_create(c, tmpl_lan_msg_trlr)))
    goto cleanup;
  if (!(s->obj_rmcpplus_session_hdr_rq = Fiid_obj_create(c, tmpl_rmcpplus_session_hdr)))
    goto cleanup;
  if (!(s->obj_rmcpplus_session_hdr_rs = Fiid_obj_create(c, tmpl_rmcpplus_session_hdr)))
    goto cleanup;
  if (!(s->obj_rmcpplus_payload_rs = Fiid_obj_create(c, tmpl_rmcpplus_payload)))
    goto cleanup;
  if (!(s->obj_rmcpplus_session_trlr_rq = Fiid_obj_create(c, tmpl_rmcpplus_session_trlr)))
    goto cleanup;
  if (!(s->obj_rmcpplus_session_trlr_rs = Fiid_obj_create(c, tmpl_rmcpplus_session_trlr)))
    goto cleanup;
  if (!(s->obj_authentication_capabilities_v20_rq = Fiid_obj_create(c, tmpl_cmd_get_channel_authentication_capabilities_v20_rq)))
    goto cleanup;
  if (!(s->obj_authentication_capabilities_v20_rs = Fiid_obj_create(c, tmpl_cmd_get_channel_authentication_capabilities_v20_rs)))
    goto cleanup;
  if (!(s->obj_open_session_request = Fiid_obj_create(c, tmpl_rmcpplus_open_session_request)))
    goto cleanup;
  if (!(s->obj_open_session_response = Fiid_obj_create(c, tmpl_rmcpplus_open_session_response)))
    goto cleanup;
  if (!(s->obj_rakp_message_1 = Fiid_obj_create(c, tmpl_rmcpplus_rakp_message_1)))
    goto cleanup;
  if (!(s->obj_rakp_message_2 = Fiid_obj_create(c, tmpl_rmcpplus_rakp_message_2)))
    goto cleanup;
  if (!(s->obj_rakp_message_3 = Fiid_obj_create(c, tmpl_rmcpplus_rakp_message_3)))
    goto cleanup;
  if (!(s->obj_rakp_message_4 = Fiid_obj_create(c, tmpl_rmcpplus_rakp_message_4)))
    goto cleanup;
  if (!(s->obj_set_session_privilege_level_rq = Fiid_obj_create(c, tmpl_cmd_set_session_privilege_level_rq)))
    goto cleanup;
  if (!(s->obj_set_session_privilege_level_rs = Fiid_obj_create(c, tmpl_cmd_set_session_privilege_level_rs)))
    goto cleanup;
  if (!(s->obj_get_channel_payload_support_rq = Fiid_obj_create(c, tmpl_cmd_get_channel_payload_support_rq)))
    goto cleanup;
  if (!(s->obj_get_channel_payload_support_rs = Fiid_obj_create(c, tmpl_cmd_get_channel_payload_support_rs)))
    goto cleanup;
  if (!(s->obj_get_payload_activation_status_rq = Fiid_obj_create(c, tmpl_cmd_get_payload_activation_status_rq)))
    goto cleanup;
  if (!(s->obj_get_payload_activation_status_rs = Fiid_obj_create(c, tmpl_cmd_get_payload_activation_status_rs)))
    goto cleanup;
  if (!(s->obj_activate_payload_rq = Fiid_obj_create(c, tmpl_cmd_activate_payload_sol_rq)))
    goto cleanup;
  if (!(s->obj_activate_payload_rs = Fiid_obj_create(c, tmpl_cmd_activate_payload_sol_rs)))
    goto cleanup;
  if (!(s->obj_sol_payload_data_rq = Fiid_obj_create(c, tmpl_sol_payload_data_remote_console_to_bmc)))
    goto cleanup;
  if (!(s->obj_sol_payload_data_rs = Fiid_obj_create(c, tmpl_sol_payload_data_bmc_to_remote_console)))
    goto cleanup;
  if (!(s->obj_get_channel_payload_version_rq = Fiid_obj_create(c, tmpl_cmd_get_channel_payload_version_rq)))
    goto cleanup;
  if (!(s->obj_get_channel_payload_version_rs = Fiid_obj_create(c, tmpl_cmd_get_channel_payload_version_rs)))
    goto cleanup;
  if (!(s->obj_deactivate_payload_rq = Fiid_obj_create(c, tmpl_cmd_deactivate_payload_rq)))
    goto cleanup;
  if (!(s->obj_deactivate_payload_rs = Fiid_obj_create(c, tmpl_cmd_deactivate_payload_rs)))
    goto cleanup;
  if (!(s->obj_close_session_rq = Fiid_obj_create(c, tmpl_cmd_close_session_rq)))
    goto cleanup;
  if (!(s->obj_close_session_rs = Fiid_obj_create(c, tmpl_cmd_close_session_rs)))
    goto cleanup;

  if (_ipmiconsole_init_ctx_session_data(c) < 0)
    goto cleanup;

  return 0;

 cleanup:
  _ipmiconsole_cleanup_ctx_session(c);
  _ipmiconsole_cleanup_ctx_managed_session_data(c);
  _ipmiconsole_init_ctx_managed_session_data(c);
  return -1;
}

int
ipmiconsole_engine_setup(unsigned int thread_count)
{
  int i, perr;

  assert(!console_engine_thread_count);
  assert(thread_count && thread_count <= IPMICONSOLE_THREAD_COUNT_MAX);

  if ((perr = pthread_mutex_lock(&console_engine_is_setup_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      return -1;
    }

  memset(console_engine_ctxs, '\0', IPMICONSOLE_THREAD_COUNT_MAX * sizeof(List));
  memset(console_engine_ctxs_count, '\0', IPMICONSOLE_THREAD_COUNT_MAX * sizeof(unsigned int));
  memset(console_engine_ctxs_mutex, '\0', IPMICONSOLE_THREAD_COUNT_MAX * sizeof(pthread_mutex_t));
  for (i = 0; i < IPMICONSOLE_THREAD_COUNT_MAX; i++)
    {
      console_engine_ctxs_notifier[i][0] = -1;
      console_engine_ctxs_notifier[i][1] = -1;
    }

  if (ipmi_rmcpplus_init() < 0)
    {
      IPMICONSOLE_DEBUG(("ipmi_crypt_init: %s", strerror(errno)));
      goto cleanup;
    }

  for (i = 0; i < IPMICONSOLE_THREAD_COUNT_MAX; i++)
    {
      if (!(console_engine_ctxs[i] = list_create((ListDelF)_ipmiconsole_cleanup_ctx_session)))
        {
          IPMICONSOLE_DEBUG(("list_create: %s", strerror(errno)));
          goto cleanup;
        }
      console_engine_ctxs_count[i] = 0;
      if ((perr = pthread_mutex_init(&console_engine_ctxs_mutex[i], NULL)) != 0)
        {
          IPMICONSOLE_DEBUG(("pthread_mutex_init: %s", strerror(perr)));
          goto cleanup;
        }
    }

  /* Don't create fds for all ctxs_notifier to limit fd creation */
  console_engine_ctxs_notifier_num = thread_count;
  for (i = 0; i < console_engine_ctxs_notifier_num; i++)
    {
      int closeonexec;

      if (pipe(console_engine_ctxs_notifier[i]) < 0)
        {
          IPMICONSOLE_DEBUG(("pipe: %s", strerror(errno)));
          goto cleanup;
        }
      if ((closeonexec = fcntl(console_engine_ctxs_notifier[i][0], F_GETFD, 0)) < 0)
        {
          IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
          goto cleanup;
        }
      closeonexec |= FD_CLOEXEC;
      if (fcntl(console_engine_ctxs_notifier[i][0], F_SETFD, closeonexec) < 0)
        {
          IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
          goto cleanup;
        }
      if ((closeonexec = fcntl(console_engine_ctxs_notifier[i][1], F_GETFD, 0)) < 0)
        {
          IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
          goto cleanup;
        }
      closeonexec |= FD_CLOEXEC;
      if (fcntl(console_engine_ctxs_notifier[i][1], F_SETFD, closeonexec) < 0)
        {
          IPMICONSOLE_DEBUG(("fcntl: %s", strerror(errno)));
          goto cleanup;
        }
    }

  console_engine_is_setup++;
  console_engine_teardown = 0;
  console_engine_teardown_immediate = 0;

  if ((perr = pthread_mutex_unlock(&console_engine_is_setup_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      goto cleanup;
    }

  return 0;

 cleanup:
  for (i = 0; i < IPMICONSOLE_THREAD_COUNT_MAX; i++)
    {
      if (console_engine_ctxs[i])
        {
          list_destroy(console_engine_ctxs[i]);
          pthread_mutex_destroy(&console_engine_ctxs_mutex[i]);
        }
      console_engine_ctxs[i] = NULL;
      close(console_engine_ctxs_notifier[i][0]);
      close(console_engine_ctxs_notifier[i][1]);
    }
  if ((perr = pthread_mutex_unlock(&console_engine_is_setup_mutex)))
    IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
  
  return -1;
}

int 
ipmiconsole_engine_is_setup(void)
{
  int is_setup, perr;

  if ((perr = pthread_mutex_lock(&console_engine_is_setup_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      return -1;
    }

  is_setup = console_engine_is_setup;

  if ((perr = pthread_mutex_unlock(&console_engine_is_setup_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      return -1;
    }

  return is_setup;
}

int 
ipmiconsole_engine_thread_count(void)
{
  int thread_count, perr;

  if ((perr = pthread_mutex_lock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      return -1;
    }

  thread_count = console_engine_thread_count;

  if ((perr = pthread_mutex_unlock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      return -1;
    }

  return thread_count;
}

static int
_teardown_initiate(void *x, void *arg)
{
  ipmiconsole_ctx_t c;
  struct ipmiconsole_ctx_session *s;

  assert(x);

  c = (ipmiconsole_ctx_t)x;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);

  s = &(c->session);

  if (!s->close_session_flag)
    s->close_session_flag++;

  return 0;
}

static int
_poll_setup(void *x, void *arg)
{
  ipmiconsole_ctx_t c;
  struct ipmiconsole_ctx_session *s;
  struct _ipmiconsole_poll_data *poll_data;

  assert(x);
  assert(arg);

  c = (ipmiconsole_ctx_t)x;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);

  s = &(c->session);
  poll_data = (struct _ipmiconsole_poll_data *)arg;

  poll_data->pfds[poll_data->pfds_index*3].fd = s->ipmi_fd;
  poll_data->pfds[poll_data->pfds_index*3].events = 0;
  poll_data->pfds[poll_data->pfds_index*3].revents = 0;
  poll_data->pfds[poll_data->pfds_index*3].events |= POLLIN;
  if (!cbuf_is_empty(s->ipmi_to_bmc))
    poll_data->pfds[poll_data->pfds_index*3].events |= POLLOUT;

  poll_data->pfds[poll_data->pfds_index*3 + 1].fd = s->asynccomm[0];
  poll_data->pfds[poll_data->pfds_index*3 + 1].events = 0;
  poll_data->pfds[poll_data->pfds_index*3 + 1].revents = 0;
  poll_data->pfds[poll_data->pfds_index*3 + 1].events |= POLLIN;
 
  /* If the session is being torn down, don't bother settings flags on
   * this fd.  However, to avoid spinning due to an invalid fd or a
   * closed fd (i.e. get a POLLINVAL or POLLHUP), re-use
   * s->asynccomm[0] as a dummy fd.
   */
  if (!s->close_session_flag)
    {
      poll_data->pfds[poll_data->pfds_index*3 + 2].fd = s->ipmiconsole_fd;
      poll_data->pfds[poll_data->pfds_index*3 + 2].events = 0;
      poll_data->pfds[poll_data->pfds_index*3 + 2].revents = 0;
      poll_data->pfds[poll_data->pfds_index*3 + 2].events |= POLLIN;
      if (!cbuf_is_empty(s->console_bmc_to_remote_console))
	poll_data->pfds[poll_data->pfds_index*3 + 2].events |= POLLOUT;
    }
  else
    {
      poll_data->pfds[poll_data->pfds_index*3 + 2].fd = s->asynccomm[0];
      poll_data->pfds[poll_data->pfds_index*3 + 2].events = 0;
      poll_data->pfds[poll_data->pfds_index*3 + 2].revents = 0;
    }

  poll_data->pfds_ctxs[poll_data->pfds_index] = c;

  poll_data->pfds_index++;
  return 0;
}

/*
 * Return 0 on success
 * Return -1 on fatal error
 */
static int
_ipmi_recvfrom(ipmiconsole_ctx_t c)
{
  struct ipmiconsole_ctx_session *s;
  char buffer[IPMICONSOLE_PACKET_BUFLEN];
  struct sockaddr_in from;
  unsigned int fromlen = sizeof(struct sockaddr_in);
  ssize_t len;
  int n, dropped = 0;
  int secure_malloc_flag;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);

  s = &(c->session);
  
  secure_malloc_flag = (c->security_flags & IPMICONSOLE_SECURITY_LOCK_MEMORY) ? 1 : 0;

  if ((len = ipmi_lan_recvfrom(s->ipmi_fd, 
                               buffer, 
                               IPMICONSOLE_PACKET_BUFLEN, 
                               0,
                               (struct sockaddr *)&from, 
                               &fromlen)) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("ipmi_lan_recvfrom: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  if (!len)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("ipmi_lan_recvfrom: no data", strerror(errno)));
      /* Note: Not a fatal error, just return*/
      return 0;
    }

  /* Sanity Check */
  if (from.sin_family != AF_INET
      || from.sin_addr.s_addr != s->addr.sin_addr.s_addr)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("received from invalid address"));
      /* Note: Not a fatal error, just return */
      return 0;
    }

  /* Empty the cbuf if it's not empty */
  if (!cbuf_is_empty(s->ipmi_from_bmc))
    {
      IPMICONSOLE_CTX_DEBUG(c, ("ipmi_from_bmc not empty, draining"));
      do {
        char tempbuf[IPMICONSOLE_PACKET_BUFLEN];
        if (cbuf_read(s->ipmi_from_bmc, tempbuf, IPMICONSOLE_PACKET_BUFLEN) < 0)
          {
            IPMICONSOLE_CTX_DEBUG(c, ("cbuf_read: %s", strerror(errno)));
            break;
          }
      } while(!cbuf_is_empty(s->ipmi_from_bmc));
    }
  
  if ((n = cbuf_write(s->ipmi_from_bmc, buffer, len, &dropped, secure_malloc_flag)) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_write: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  if (n != len)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_write: invalid bytes written; n=%d; len=%d", n, len));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  if (dropped)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_write: dropped data: dropped=%d", dropped));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  return 0;
}

/*
 * Return 0 on success
 * Return -1 on fatal error
 */
static int
_ipmi_sendto(ipmiconsole_ctx_t c)
{
  struct ipmiconsole_ctx_session *s;
  char buffer[IPMICONSOLE_PACKET_BUFLEN];
  ssize_t len;
  int n;
  
  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);

  s = &(c->session);

  if ((n = cbuf_read(s->ipmi_to_bmc, buffer, IPMICONSOLE_PACKET_BUFLEN)) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_read: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  if ((len = ipmi_lan_sendto(s->ipmi_fd,
                             buffer,
                             n, 
                             0, 
                             (struct sockaddr *)&(s->addr),
                             sizeof(struct sockaddr_in))) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("ipmi_lan_sendto: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  if (len != n)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("ipmi_lan_sendto: invalid bytes written; n=%d; len=%d", n, len));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  /* cbuf should be empty now */
  if (!cbuf_is_empty(s->ipmi_to_bmc))
    {
      IPMICONSOLE_CTX_DEBUG(c, ("ipmi_to_bmc not empty"));
      /* Note: Not a fatal error, just return*/
      return 0;
    }

  return 0;
}

/* 
 * Return 0 on success
 * Return -1 on fatal error
 */
static int
_asynccomm(ipmiconsole_ctx_t c)
{
  struct ipmiconsole_ctx_session *s;
  uint8_t val;
  ssize_t len;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);

  s = &(c->session);

  if ((len = read(s->asynccomm[0], (void *)&val, 1)) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("read: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      return -1;
    }

  if (!len)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("asynccomm closed"));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  /* User may have requested several break conditions in a
   * row quickly.  We assume it means just one
   */
  if (val == IPMICONSOLE_PIPE_GENERATE_BREAK_CODE)
    {
      if (!(s->break_requested))
	{
	  s->break_requested++;

	  if ((s->console_remote_console_to_bmc_bytes_before_break = cbuf_used(s->console_remote_console_to_bmc)) < 0)
	    {
	      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_used: %s", strerror(errno)));
	      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
	      return -1;
	    }
	}
    }

  return 0;
}

/*
 * Return 0 on success
 * Return -1 on fatal error
 */
static int
_console_read(ipmiconsole_ctx_t c)
{
  struct ipmiconsole_ctx_session *s;
  char buffer[IPMICONSOLE_PACKET_BUFLEN];
  ssize_t len;
  int n, dropped = 0;
  int secure_malloc_flag;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);
  assert(!c->session.close_session_flag);	

  s = &(c->session);
  
  secure_malloc_flag = (c->security_flags & IPMICONSOLE_SECURITY_LOCK_MEMORY) ? 1 : 0;

  if ((len = read(s->ipmiconsole_fd,
                  buffer,
                  IPMICONSOLE_PACKET_BUFLEN)) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("read: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      return -1;
    }

  if (!len)
    {
      /* Returning -1 closes the session, but really this error is ok
       * since the user is allowed to close the session
       */
      c->errnum = IPMICONSOLE_ERR_SUCCESS;
      return -1;
    }

  if ((n = cbuf_write(s->console_remote_console_to_bmc, buffer, len, &dropped, secure_malloc_flag)) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_write: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  if (n != len)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_write: invalid bytes written; n=%d; len=%d", n, len));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }
  
  if (dropped)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_write: dropped data: dropped=%d", dropped));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }
  
  return 0;
};

/*
 * Return 0 on success
 * Return -1 on fatal error
 */
static int
_console_write(ipmiconsole_ctx_t c)
{
  struct ipmiconsole_ctx_session *s;
  char buffer[IPMICONSOLE_PACKET_BUFLEN];
  ssize_t len;
  int n;
  
  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);
  assert(!c->session.close_session_flag);	

  s = &(c->session);

  if ((n = cbuf_read(s->console_bmc_to_remote_console, buffer, IPMICONSOLE_PACKET_BUFLEN)) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("cbuf_read: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  if ((len = write(s->ipmiconsole_fd,
                   buffer,
                   n)) < 0)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("write: %s", strerror(errno)));

      if (errno == EPIPE)
        {
          /* This error is ok since the user is allowed to close the
           * session
           */
          c->errnum = IPMICONSOLE_ERR_SUCCESS;
        }
      else
        c->errnum = IPMICONSOLE_ERR_SYSTEM_ERROR;
      return -1;
    }

  if (len != n)
    {
      IPMICONSOLE_CTX_DEBUG(c, ("write: invalid bytes written; n=%d; len=%d", n, len));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      return -1;
    }

  /* cbuf should be empty now */
  if (!cbuf_is_empty(s->console_bmc_to_remote_console))
    {
      IPMICONSOLE_CTX_DEBUG(c, ("console_bmc_to_remote_console not empty"));
      /* Note: Not a fatal error, just return*/
      return 0;
    }

  return 0;
};

static void *
_ipmiconsole_engine(void *arg)
{
  int perr, ctxs_count = 0;
  unsigned int index;
  unsigned int teardown_flag = 0;
  unsigned int teardown_initiated = 0;

  assert(arg);

  index = *((unsigned int *)arg);

  assert(index < IPMICONSOLE_THREAD_COUNT_MAX);

  free(arg);

  while (!teardown_flag || ctxs_count)
    {
      struct _ipmiconsole_poll_data poll_data;
      int i, count;
      unsigned int timeout_len;
      int unlock_console_engine_ctxs_mutex_flag = 0;
      int spin_wait_flag = 0;
      char buf[IPMICONSOLE_PIPE_BUFLEN];
      
      if ((perr = pthread_mutex_lock(&console_engine_teardown_mutex)))
        {
          /* This is one of the only truly "fatal" conditions */
          IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
          teardown_flag = 1;
        }
      
      if (console_engine_teardown_immediate)
        {
          if ((perr = pthread_mutex_unlock(&console_engine_teardown_mutex)))
            IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
          break;
        }

      if (console_engine_teardown)
        teardown_flag = 1;
      
      if ((perr = pthread_mutex_unlock(&console_engine_teardown_mutex)))
        {
          /* This is one of the only truly "fatal" conditions */
          IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
          teardown_flag = 1;
        }

      memset(&poll_data, '\0', sizeof(struct _ipmiconsole_poll_data));

      /* Notes:
       *
       * We must lock the list from here till all context data and pointers
       * are retrieved. 
       */

      if ((perr = pthread_mutex_lock(&console_engine_ctxs_mutex[index])))
        {
          /* This is one of the only truly "fatal" conditions */
          IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
          teardown_flag = 1;
        }

      /* Note: Set close_session_flag in the contexts before
       * ipmiconsole_process_ctxs(), so the initiation of the closing
       * down will begin now rather than the next iteration of the
       * loop.
       */
      if (teardown_flag && !teardown_initiated)
        {
          /* XXX: Umm, if this fails, we may not be able to teardown
           * cleanly.  Break out of the loop I guess. 
           */
          if (list_for_each(console_engine_ctxs[index], _teardown_initiate, NULL) < 0)
            {
              IPMICONSOLE_DEBUG(("list_for_each: %s", strerror(errno)));
              break;
            }
          teardown_initiated++;
        }
      
      if ((ctxs_count = ipmiconsole_process_ctxs(console_engine_ctxs[index], &timeout_len)) < 0)
        goto continue_loop;
      
      if (!ctxs_count && teardown_flag)
        continue;

      if (!ctxs_count)
        {
          spin_wait_flag++;
          goto continue_loop;
        }
      poll_data.ctxs_len = ctxs_count;

      /* 
       * There are 3 pfds per ctx.  One for 'ipmi_fd', 'asynccomm[0]', and 'ipmiconsole_fd'.
       *
       * There is + 1 pfds for the "console_engine_ctxs_notifier".
       * This will be set up manually here, and not in _poll_setup().
       */
      if (!(poll_data.pfds = (struct pollfd *)malloc(((poll_data.ctxs_len * 3) + 1) * sizeof(struct pollfd))))
        {
          IPMICONSOLE_DEBUG(("malloc: %s", strerror(errno)));
          goto continue_loop;
        }

      if (!(poll_data.pfds_ctxs = (ipmiconsole_ctx_t *)malloc(poll_data.ctxs_len * sizeof(ipmiconsole_ctx_t))))
        {
          IPMICONSOLE_DEBUG(("malloc: %s", strerror(errno)));
          goto continue_loop;
        }

      if ((count = list_for_each(console_engine_ctxs[index], _poll_setup, &poll_data)) < 0)
        {
          IPMICONSOLE_DEBUG(("list_for_each: %s", strerror(errno)));
          goto continue_loop;
        }
      
      if ((perr = pthread_mutex_unlock(&console_engine_ctxs_mutex[index])))
        IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      unlock_console_engine_ctxs_mutex_flag++;

      /* Setup notifier pipe as last remaining poll data */
      poll_data.pfds[(poll_data.ctxs_len * 3)].fd = console_engine_ctxs_notifier[index][0];
      poll_data.pfds[(poll_data.ctxs_len * 3)].events = POLLIN;
      poll_data.pfds[(poll_data.ctxs_len * 3)].revents = 0;
      
      if (count != ctxs_count)
        {
          IPMICONSOLE_DEBUG(("list_for_each: invalid length returned: %d", count));
          goto continue_loop;
        }

      if (poll_data.pfds_index != ctxs_count)
        {
          IPMICONSOLE_DEBUG(("invalid index set on returned: %d", poll_data.pfds_index));
          goto continue_loop;
        }

      if (poll(poll_data.pfds, (poll_data.ctxs_len * 3) + 1, timeout_len) < 0)
        {
          IPMICONSOLE_DEBUG(("poll: %s", strerror(errno)));
          goto continue_loop;
        }

      for (i = 0; i < poll_data.ctxs_len; i++)
        {
          if (poll_data.pfds[i*3].revents & POLLERR)
            {
              IPMICONSOLE_CTX_DEBUG(poll_data.pfds_ctxs[i], ("POLLERR"));
	      poll_data.pfds_ctxs[i]->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
	      poll_data.pfds_ctxs[i]->session.close_session_flag++;
              continue;
            }
	  if (poll_data.pfds[i*3+1].revents & POLLERR)
	    {
	      IPMICONSOLE_CTX_DEBUG(poll_data.pfds_ctxs[i], ("POLLERR"));
	      poll_data.pfds_ctxs[i]->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
	      poll_data.pfds_ctxs[i]->session.close_session_flag++;
	      continue;
	    }
	  if (!poll_data.pfds_ctxs[i]->session.close_session_flag)
	    {
	      if (poll_data.pfds[i*3+2].revents & POLLERR)
		{
		  IPMICONSOLE_CTX_DEBUG(poll_data.pfds_ctxs[i], ("POLLERR"));
		  poll_data.pfds_ctxs[i]->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
		  poll_data.pfds_ctxs[i]->session.close_session_flag++;
		  continue;
		}
	    }
          if (poll_data.pfds[i*3].revents & POLLIN)
            {
              if (_ipmi_recvfrom(poll_data.pfds_ctxs[i]) < 0)
                {
		  poll_data.pfds_ctxs[i]->session.close_session_flag++;
                  continue;
                }
            }
          if (poll_data.pfds[i*3].revents & POLLOUT)
            {
              if (_ipmi_sendto(poll_data.pfds_ctxs[i]) < 0)
                {
                  poll_data.pfds_ctxs[i]->session.close_session_flag++;
                  continue;
                }
            }
          if (poll_data.pfds[i*3 + 1].revents & POLLIN)
            {
              if (_asynccomm(poll_data.pfds_ctxs[i]) < 0)
                {
                  poll_data.pfds_ctxs[i]->session.close_session_flag++;
                  continue;
                }
            }
	  if (!poll_data.pfds_ctxs[i]->session.close_session_flag)
	    {
	      if (poll_data.pfds[i*3+2].revents & POLLIN)
		{
		  if (_console_read(poll_data.pfds_ctxs[i]) < 0)
		    {
		      poll_data.pfds_ctxs[i]->session.close_session_flag++;
		      continue;
		    }
		}
	      if (poll_data.pfds[i*3+2].revents & POLLOUT)
		{
		  if (_console_write(poll_data.pfds_ctxs[i]) < 0)
		    {
		      poll_data.pfds_ctxs[i]->session.close_session_flag++;
		      continue;
		    }
		}
	    }
        }

      /* We don't care what's read, just get it off the fd */
      if (poll_data.pfds[(poll_data.ctxs_len * 3)].revents & POLLIN)
        {
          if (read(console_engine_ctxs_notifier[index][0], buf, IPMICONSOLE_PIPE_BUFLEN) < 0)
            IPMICONSOLE_DEBUG(("read: %s", strerror(errno)));
        }

    continue_loop:
      if (!unlock_console_engine_ctxs_mutex_flag)
        {
          if ((perr = pthread_mutex_unlock(&console_engine_ctxs_mutex[index])))
            {
              /* This is one of the only truly "fatal" conditions */
              IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
              teardown_flag = 1;
            }
        }
      if (spin_wait_flag)
        {
          /* No contexts stored, either because they all died or none
           * have been submitted yet.  Sleep a little bit to kill some
           * time and avoid spinning.
           */
          /* XXX: Is this portable? */
          usleep(IPMICONSOLE_SPIN_WAIT_TIME);
        }
      if (poll_data.pfds)
        {
          free(poll_data.pfds);
          poll_data.pfds = NULL;
        }
      if (poll_data.pfds_ctxs)
        {
          free(poll_data.pfds_ctxs);
          poll_data.pfds_ctxs = NULL;
        }
      poll_data.ctxs_len = 0;
      poll_data.pfds_index = 0;
    }

  /* No way to return error, so just continue on even if there is a failure */
  if ((perr = pthread_mutex_lock(&console_engine_thread_count_mutex)))
    IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));

  console_engine_thread_count--;

  if ((perr = pthread_mutex_unlock(&console_engine_thread_count_mutex)))
    IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));

  return NULL;
}

int
ipmiconsole_engine_thread_create(void)
{
  pthread_t thread;
  pthread_attr_t attr;
  unsigned int *index = NULL;
  int perr, rv = -1;

  assert(console_engine_is_setup);
  
  if ((perr = pthread_mutex_lock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      return -1;
    }

  assert(console_engine_thread_count < IPMICONSOLE_THREAD_COUNT_MAX);

  if ((perr = pthread_mutex_unlock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      goto cleanup;
    }

  if ((perr = pthread_attr_init(&attr)))
    {
      IPMICONSOLE_DEBUG(("pthread_attr_init: %s", strerror(perr)));
      goto cleanup;
    }

  if ((perr = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)))
    {
      IPMICONSOLE_DEBUG(("pthread_attr_setdetachstate: %s", strerror(perr)));
      goto cleanup;
    }

  if (!(index = (unsigned int *)malloc(sizeof(unsigned int))))
    {
      IPMICONSOLE_DEBUG(("malloc: %s", strerror(errno)));
      goto cleanup;
    }
  *index = console_engine_thread_count;

  if ((perr = pthread_create(&thread, &attr, _ipmiconsole_engine, index)))
    {
      IPMICONSOLE_DEBUG(("pthread_create: %s", strerror(perr)));
      goto cleanup;
    }

  /* Who cares if this fails */
  if ((perr = pthread_attr_destroy(&attr)))
    IPMICONSOLE_DEBUG(("pthread_attr_destroy: %s", strerror(perr)));

  console_engine_thread_count++;

  rv = 0;
 cleanup:
  /* XXX destroy thread on error? */
  if ((perr = pthread_mutex_unlock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      return -1;
    }

  return rv;
}

int 
ipmiconsole_engine_submit_ctx(ipmiconsole_ctx_t c)
{
  void *ptr;
  int i, perr, ret = -1;
  unsigned int min_submitted = UINT_MAX;
  int index = 0;

  assert(c);
  assert(c->magic == IPMICONSOLE_CTX_MAGIC);
  assert(!(c->session_submitted));
  assert(console_engine_is_setup);

  /* XXX
   *
   * Consider adding a queue of pending submissions so users will not
   * have to "block" here.
   */

  if ((perr = pthread_mutex_lock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      return -1;
    }

  for (i = 0; i < console_engine_thread_count; i++)
    {
      if ((perr = pthread_mutex_lock(&console_engine_ctxs_mutex[i])))
        {
          IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
          c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
          goto cleanup_thread_count;
        }
      
      if (console_engine_ctxs_count[i] < min_submitted)
        {
          min_submitted = console_engine_ctxs_count[i];
          index = i;
        }

      if ((perr = pthread_mutex_unlock(&console_engine_ctxs_mutex[i])))
        {
          IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
          c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
          goto cleanup_thread_count;
        }
    }

  if ((perr = pthread_mutex_lock(&console_engine_ctxs_mutex[index])))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      goto cleanup_thread_count;
    }

  if (!(ptr = list_append(console_engine_ctxs[index], c)))
    {
      /* Note: Don't do a CTX debug, this is more of a global debug */
      IPMICONSOLE_DEBUG(("list_append: %s", strerror(errno)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      goto cleanup_ctxs;
    }

  if (ptr != (void *)c)
    {
      IPMICONSOLE_DEBUG(("list_append: invalid pointer: ptr=%p; c=%p", ptr, c));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      goto cleanup_ctxs;
    }

  console_engine_ctxs_count[index]++;

  ret = 0;

  /* achu:
   *
   * Ok to set the flag here b/c we are in the 'console_engine_ctxs_mutex'
   */
  c->session_submitted++;

  /* "Interrupt" the engine and tell it to get moving along w/ the new context */
  if (write(console_engine_ctxs_notifier[index][1], "1", 1) < 0)
    IPMICONSOLE_DEBUG(("write: %s", strerror(errno)));

 cleanup_ctxs:
  if ((perr = pthread_mutex_unlock(&console_engine_ctxs_mutex[index])))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      c->errnum = IPMICONSOLE_ERR_INTERNAL_ERROR;
      goto cleanup_thread_count;
    }
  
 cleanup_thread_count:
  if ((perr = pthread_mutex_unlock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      return -1;
    }

  return ret;
}

int
ipmiconsole_engine_cleanup(int cleanup_sol_sessions)
{
  int thread_count, perr, i, rv = -1;

  if ((perr = pthread_mutex_lock(&console_engine_is_setup_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      return -1;
    }

  if (!console_engine_is_setup)
    goto unlock_is_setup_mutex;

  if ((perr = pthread_mutex_lock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      goto unlock_is_setup_mutex;
    }

  thread_count = console_engine_thread_count;

  if ((perr = pthread_mutex_unlock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      goto unlock_is_setup_mutex;
    }
 
  if (!thread_count)
    {
      rv = 0;
      goto engine_cleanup;
    }

  if ((perr = pthread_mutex_lock(&console_engine_teardown_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      goto engine_cleanup;
    }

  console_engine_teardown++;
  if (!cleanup_sol_sessions)
    console_engine_teardown_immediate++;

  if ((perr = pthread_mutex_unlock(&console_engine_teardown_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      goto engine_cleanup;
    }

  /* "Interrupt" the engine and tell it to get moving along */
  for (i = 0; i < console_engine_ctxs_notifier_num; i++)
    {
      if (write(console_engine_ctxs_notifier[i][1], "1", 1) < 0)
        IPMICONSOLE_DEBUG(("write: %s", strerror(errno)));
    }

  if ((perr = pthread_mutex_lock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      goto engine_cleanup;
    }

  while (console_engine_thread_count)
    {
      if ((perr = pthread_mutex_unlock(&console_engine_thread_count_mutex)))
        {
          IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
          goto engine_cleanup;
        }

      /* Wait awhile then try again */
      /* XXX: Is this portable? */
      usleep(IPMICONSOLE_SPIN_WAIT_TIME);

      if ((perr = pthread_mutex_lock(&console_engine_thread_count_mutex)))
        {
          IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
          goto engine_cleanup;
        }
    }

  if ((perr = pthread_mutex_unlock(&console_engine_thread_count_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      goto engine_cleanup;
    }

 engine_cleanup:
  for (i = 0; i < IPMICONSOLE_THREAD_COUNT_MAX; i++)
    {
      if (console_engine_ctxs[i])
        list_destroy(console_engine_ctxs[i]);
      console_engine_ctxs[i] = NULL;
      console_engine_ctxs[i] = 0;
      pthread_mutex_destroy(&console_engine_ctxs_mutex[i]);
      close(console_engine_ctxs_notifier[i][0]);
      close(console_engine_ctxs_notifier[i][1]);
    }
  
  console_engine_is_setup = 0;

  if ((perr = pthread_mutex_lock(&console_engine_teardown_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_lock: %s", strerror(perr)));
      goto unlock_is_setup_mutex;
    }

  console_engine_teardown = 0;
  console_engine_teardown_immediate = 0;

  if ((perr = pthread_mutex_unlock(&console_engine_teardown_mutex)))
    {
      IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));
      goto unlock_is_setup_mutex;
    }

  rv = 0;
 unlock_is_setup_mutex:
  if ((perr = pthread_mutex_unlock(&console_engine_is_setup_mutex)))
    IPMICONSOLE_DEBUG(("pthread_mutex_unlock: %s", strerror(perr)));

  return rv;
}
