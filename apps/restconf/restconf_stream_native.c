/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2020-2024 Olof Hagsand and Rubicon Communications, LLC(Netgate)

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****

  * nghttp2 callback mechanism
  *
  * nghttp2_session_mem_recv()
  *    on_begin_headers_callback() 
  *       create sd
  *    on_header_callback() NGHTTP2_HEADERS
  *       translate all headers
  *    on_data_chunk_recv_callback
  *       get indata
  *    on_frame_recv_callback NGHTTP2_FLAG_END_STREAM
  *       get method and call handler
  *       create rr
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <pwd.h>
#include <ctype.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/resource.h>

#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

/* cligen */
#include <cligen/cligen.h>

/* clixon */
#include <clixon/clixon.h>

#ifdef HAVE_LIBNGHTTP2
#include <nghttp2/nghttp2.h>
#endif

/* restconf */
#include "restconf_lib.h"       /* generic shared with plugins */
#include "restconf_handle.h"
#include "restconf_api.h"       /* generic not shared with plugins */
#include "restconf_err.h"
#include "restconf_root.h"
#include "restconf_native.h"    /* Restconf-openssl mode specific headers*/
#include "restconf_stream.h"

#ifdef HAVE_LIBNGHTTP2
#include "restconf_nghttp2.h"

static int
restconf_http2_send_notification(clixon_handle         h,
                                 restconf_stream_data *sd,
                                 restconf_conn        *rc)

{
    int                   retval = -1;
    nghttp2_session      *session;
    nghttp2_error         ngerr;
    nghttp2_data_provider data_prd;
    int32_t               stream_id;

    data_prd.source.ptr = sd;
    data_prd.read_callback = restconf_sd_read;
    session = rc->rc_ngsession;
    stream_id = 1;
    if ((ngerr = nghttp2_submit_data(session,
                                     0, // flags
                                     stream_id,
                                     (data_prd.source.ptr != NULL)?&data_prd:NULL
                                     )) < 0){
        clixon_err(OE_NGHTTP2, ngerr, "nghttp2_submit_response");
        goto done;
    }
    retval = 0;
 done:
    return retval;
}
#endif // NGHTTP2

/*! Callback when stream notifications arrive from backend
 *
 * @param[in]  s    Socket
 * @param[in]  req  Generic Www handle (can be part of clixon handle)
 * @retval     0    OK
 * @retval    -1    Error
 * @see netconf_notification_cb
 */
static int
stream_native_backend_cb(int   s,
                         void *arg)
{
    int                   retval = -1;
    restconf_stream_data *sd = (restconf_stream_data *)arg;
    int                   eof;
    cxobj                *xtop = NULL; /* top xml */
    cxobj                *xn;        /* notification xml */
    cbuf                 *cbx = NULL;
    cbuf                 *cb = NULL;
    cbuf                 *cbmsg = NULL;
    int                   pretty = 0;
    int                   ret;
    restconf_conn        *rc = sd->sd_conn;
    clixon_handle         h = rc->rc_h;
#ifdef HAVE_LIBNGHTTP2
    nghttp2_error         ngerr;
#endif

    clixon_debug(CLIXON_DBG_STREAM|CLIXON_DBG_DETAIL, "");
    pretty = restconf_pretty_get(h);
    if (clixon_msg_rcv11(s, NULL, 0, &cbmsg, &eof) < 0)
        goto done;
    clixon_debug(CLIXON_DBG_STREAM, "%s", cbuf_get(cbmsg));
    /* handle close from remote end: this will exit the client */
    if (eof){
        clixon_debug(CLIXON_DBG_STREAM, "eof");
        restconf_close_ssl_socket(rc, __func__, 0);
        goto ok;
    }
    if ((ret = clixon_xml_parse_string(cbuf_get(cbmsg), YB_NONE, NULL, &xtop, NULL)) < 0)
        goto done;
    if (ret == 0){
        clixon_err(OE_XML, EFAULT, "Invalid notification");
        goto done;
    }
    /* create event */
    if ((cb = cbuf_new()) == NULL){
        clixon_err(OE_PLUGIN, errno, "cbuf_new");
        goto done;
    }
    if ((cbx = cbuf_new()) == NULL){
        clixon_err(OE_PLUGIN, errno, "cbuf_new");
        goto done;
    }
    if ((xn = xpath_first(xtop, NULL, "notification")) == NULL)
        goto ok;
    cprintf(cb, "data: ");
    if (clixon_xml2cbuf(cb, xn, 0, pretty, NULL, -1, 0) < 0)
        goto done;
    cprintf(cb, "\r\n");
    cprintf(cb, "\r\n");
#ifdef HAVE_LIBNGHTTP2
    if (rc->rc_proto == HTTP_2){
        if (restconf_reply_send(sd, 200, cb, 0) < 0)
            goto done;
        cb = NULL;
        if (restconf_http2_send_notification(h, sd, rc) < 0)
            goto done;
        if ((ngerr = nghttp2_session_send(rc->rc_ngsession)) != 0)
            goto done;
        if (sd->sd_body){
            cbuf_free(sd->sd_body);
            sd->sd_body = NULL;
        }
    }
    else
#endif // HAVE_LIBNGHTTP2
        if ((ret = native_buf_write(h, cbuf_get(cb), cbuf_len(cb), rc, "native stream")) < 0)
        goto done;
 ok:
    retval = 0;
 done:
    clixon_debug(CLIXON_DBG_STREAM|CLIXON_DBG_DETAIL, "retval: %d", retval);
    if (xtop != NULL)
        xml_free(xtop);
    if (cbmsg)
        cbuf_free(cbmsg);
    if (cb)
        cbuf_free(cb);
    if (cbx)
        cbuf_free(cbx);
    return retval;
}

/*! Timeout of notification stream, limit lifetime, for debug
 *
 * XXX HTTP/2 not closed cleanly
 */
static int
stream_timeout_end(int   s,
                   void *arg)
{
    int            retval = -1;
    restconf_conn *rc = (restconf_conn *)arg;

    clixon_debug(CLIXON_DBG_STREAM, "");
    rc->rc_exit = 1;
#if 0 // Termination is not clean
    {
        int ngerr;
        int ret;
        if ((ngerr = nghttp2_session_terminate_session(rc->rc_ngsession, 0)) < 0){
            clixon_err(OE_NGHTTP2, ngerr, "nghttp2_session_terminate_session %d", ngerr);
            goto done; // XXX not here in original?
        }
        if ((ngerr = nghttp2_session_send(rc->rc_ngsession)) != 0){
            clixon_err(OE_NGHTTP2, ngerr, "nghttp2_session_send %d", ngerr);
            goto done; // XXX not here in original?
        }
        ret = SSL_pending(rc->rc_ssl);
        clixon_debug(CLIXON_DBG_STREAM, "SSL pending: %d", ret);
    }
#endif
    if (restconf_close_ssl_socket(rc, __func__, 0) < 0)
        goto done;
    retval = 0;
 done:
    return retval;
}

/*! Close notification stream
 *
 * Only stream aspects, to close full socket, call eg restconf_close_ssl_socket
 */
int
stream_close(clixon_handle h,
             void         *req)

{
    restconf_conn *rc = (restconf_conn *)req;

    clixon_debug(CLIXON_DBG_STREAM, "");
    clicon_rpc_close_session(h);
    clixon_event_unreg_fd(rc->rc_event_stream, stream_native_backend_cb);
    clixon_event_unreg_timeout(stream_timeout_end, req);
    close(rc->rc_event_stream);
    rc->rc_event_stream = 0;
    return 0;
}

/*! Native specific code for setting up stream sockets
 *
 * @param[in]  h       Clixon handle
 * @param[in]  req     Generic Www handle (can be part of clixon handle)
 * @param[in]  timeout Stream timeout
 * @param[in]  besock  Socket to backend
 * @param[out] finish  Set to zero, if request should not be finnished by upper layer
 * @retval     0       OK
 * @retval    -1       Error
 * Consider moving timeout and backend sock to generic code
 */
int
stream_sockets_setup(clixon_handle h,
                     void         *req,
                     int           timeout,
                     int           besock,
                     int          *finish)
{
    int                   retval = -1;
    restconf_stream_data *sd = (restconf_stream_data *)req;
    restconf_conn        *rc;

    /* Listen to backend socket */
    if (clixon_event_reg_fd(besock,
                            stream_native_backend_cb,
                            req,
                            "stream socket") < 0)
        goto done;
    rc = sd->sd_conn;
    rc->rc_event_stream = besock;
    /* Timeout of notification stream, close after limited lifetime, for debug */
    if (timeout){
        struct timeval   t;
        gettimeofday(&t, NULL);
        t.tv_sec += timeout;
        clixon_event_reg_timeout(t, stream_timeout_end, rc, "Stream timeout");
    }
    retval = 0;
 done:
    clixon_debug(CLIXON_DBG_STREAM, "retval:%d", retval);
    return retval;
}
