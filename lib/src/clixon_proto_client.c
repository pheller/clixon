/*
 *
  Copyright (C) 2009-2016 Olof Hagsand and Benny Holmgren

  This file is part of CLIXON.

  CLIXON is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  CLIXON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with CLIXON; see the file LICENSE.  If not, see
  <http://www.gnu.org/licenses/>.

 * 
 * Client-side functions for clicon_proto protocol
 * Historically this code was part of the clicon_cli application. But
 * it should (is?) be general enough to be used by other applications.
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clixon_queue.h"
#include "clixon_chunk.h"
#include "clixon_hash.h"
#include "clixon_handle.h"
#include "clixon_yang.h"
#include "clixon_options.h"
#include "clixon_proto.h"
#include "clixon_err.h"
#include "clixon_xml.h"
#include "clixon_proto_encode.h"
#include "clixon_proto_client.h"

/*! Internal rpc function
 * @param[in]    h      CLICON handle
 * @param[in]    msg    Encoded message
 * @param[out]   ret    Return value from backend server (reply)
 * @param[out]   retlen Length of return value
 * @param[inout] sock0  If pointer exists, do not close socket to backend on success 
 *                      and return it here. For keeping a notify socket open
 * @param[in]    label  Chunk label for deallocating return values
 * Deallocate with unchunk_group(label)
 * Note: sock0 is if connection should be persistent, like a notification/subscribe api
 */
static int
clicon_rpc_msg(clicon_handle      h, 
	       struct clicon_msg *msg, 
	       char             **ret, 
	       uint16_t          *retlen, 
	       int               *sock0, 
	       const char        *label)
{
    int                retval = -1;
    char              *sock;
    int                port;

    if ((sock = clicon_sock(h)) == NULL){
	clicon_err(OE_FATAL, 0, "CLICON_SOCK option not set");
	goto done;
    }
    /* What to do if inet socket? */
    switch (clicon_sock_family(h)){
    case AF_UNIX:
	if (clicon_rpc_connect_unix(msg, sock, ret, retlen, sock0, label) < 0){
#if 0
	    if (errno == ESHUTDOWN)
		/* Maybe could reconnect on a higher layer, but lets fail
		   loud and proud */
		cli_set_exiting(1);
#endif
	    goto done;
	}
	break;
    case AF_INET:
	if ((port = clicon_sock_port(h)) < 0){
	    clicon_err(OE_FATAL, 0, "CLICON_SOCK option not set");
	    goto done;
	}
	if (port < 0){
	    clicon_err(OE_FATAL, 0, "CLICON_SOCK_PORT not set");
	    goto done;
	}
	if (clicon_rpc_connect_inet(msg, sock, port, ret, retlen, sock0, label) < 0)
	    goto done;
	break;
    }
    retval = 0;
 done:
    return retval;
}

/*! Commit changes send a commit request to backend daemon
 * @param[in] h          CLICON handle
 * @param[in] from       name of 'from' database (eg "candidate")
 * @param[in] db         name of 'to' database (eg "running")
 * @param[in] snapshot   Make a snapshot copy of db state
 * @param[in] startup    Make a copy to startup.
 * @retval 0             Copy current->candidate
 */
int
clicon_rpc_commit(clicon_handle h, 
		  char         *from,  
		  char         *to,    
		  int           snapshot, 
		  int           startup)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg=clicon_msg_commit_encode(from, to, snapshot, startup, 
					  __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
 done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*! Send validate request to backend daemon
 * @param[in] h          CLICON handle
 * @param[in] db         Name of database
 * @retval 0        
 */
int
clicon_rpc_validate(clicon_handle h, 
		    char         *db)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg=clicon_msg_validate_encode(db, __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
 done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*! Send database change request to backend daemon, variant for xmldb
 * Same as clicon_proto_change just with a string
 * @param[in] h          CLICON handle
 * @param[in] db         Name of database
 * @param[in] op         Operation on database item: set, delete, (merge?)
 * @param[in] key        Database key
 * @param[in] value      value as string
 * @retval    0          OK
 * @retval   -1          Error
 * @note special case: remove all: key:"/" op:OP_REMOVE
 */
int
clicon_rpc_change(clicon_handle       h, 
		  char               *db, 
		  enum operation_type op,
		  char               *key, 
		  char               *val)
{
    int               retval = -1;
    struct clicon_msg *msg;

    if ((msg = clicon_msg_change_encode(db, 
					op, 
					key, 
					val, 
					strlen(val)+1,
				       __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}


/*! Send database entries as XML to backend daemon
 * Same as clicon_proto_change just with a cvec instead of lvec
 * @param[in] h          CLICON handle
 * @param[in] db         Name of database
 * @param[in] op         Operation on database item: OP_MERGE, OP_REPLACE
 * @param[in] api_path   restconf API Path (or "")
 * @param[in] xml        XML string. Ex: <a>..</a><b>...</b>
 * @retval    0          OK
 * @retval   -1          Error
 */
int
clicon_rpc_xmlput(clicon_handle       h, 
		  char               *db, 
		  enum operation_type op,
		  char               *api_path,
		  char               *xml)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg = clicon_msg_xmlput_encode(db, 
					(uint32_t)op, 
					api_path,
					xml,
				       __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}




/*! Send database save request to backend daemon
 * @param[in] h          CLICON handle
 * @param[in] db         Name of database
 * @param[in] snapshot   Save to snapshot file
 * @param[in] filename   Save to file (backend file-system)
 */
int
clicon_rpc_save(clicon_handle h, 
		char         *db, 
		int           snapshot, 
		char         *filename)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg=clicon_msg_save_encode(db, snapshot, filename,
				   __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
 done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*! Send database load request to backend daemon
 * @param[in] h          CLICON handle
 * @param[in] replace    0: merge with existing data, 1:replace completely
 * @param[in] db         Name of database
 * @param[in] filename   Load from file (backend file-system)
 */
int
clicon_rpc_load(clicon_handle h, 
		int           replace, 
		char         *db, 
		char         *filename)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg=clicon_msg_load_encode(replace, db, filename,
				   __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
 done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*! Send a request to backend to copy a file from one location to another 
 * Note this assumes the backend can access these files and (usually) assumes
 * clients and servers have the access to the same filesystem.
 * @param[in] h          CLICON handle
 * @param[in] db1        src database, eg "candidate"
 * @param[in] db2        dst database, eg "running"
 */
int
clicon_rpc_copy(clicon_handle h, 
		char         *db1, 
		char         *db2)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg=clicon_msg_copy_encode(db1, db2, __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
 done:
    unchunk_group(__FUNCTION__);
    return retval;
}


/*! Send a kill session request to backend server
 * @param[in] h            CLICON handle
 * @param[in] session_id   Id of session to kill
 */
int
clicon_rpc_kill(clicon_handle h, 
		int           session_id)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg=clicon_msg_kill_encode(session_id, __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
 done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*! Send a debug request to backend server
 * @param[in] h          CLICON handle
 * @param[in] level      Debug level
 */
int
clicon_rpc_debug(clicon_handle h, 
		int           level)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg=clicon_msg_debug_encode(level, __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, NULL, __FUNCTION__) < 0)
	goto done;
    retval = 0;
 done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*! An rpc call from a frontend module to a function in a backend module
 *
 * A CLI/netconf frontend module can make a functional call to a backend
 * module and get return value back. 
 * The backend module needs to be specified (XXX would be nice to avoid this)
 * parameters can be sent, and value returned.
 * A function (func) must be defined in the backend module (plugin)
 * An example signature of such a downcall function is:
 * @code
 *   char      name[16];
 *   char     *ret;
 *   uint16_t  retlen;
 *   clicon_rpc_call(h, 0, "my-backend-plugin", "my_fn", name, 16,
 *                   &ret, &retlen, __FUNCTION__);
 *   unchunk_group(__FUNCTION__); # deallocate 'ret'
 * @endcode
 * Backend example function:
 * @code
int
downcall(clicon_handle h, uint16_t op, uint16_t len, void *arg, 
         uint16_t *reply_data_len, void **reply_data)
 * @endcode
 *
 * @param[in]   h        Clicon handle
 * @param[in]   op       Generic application-defined operation
 * @param[in]   plugin   Name of backend plugin (XXX look in backend plugin dir)
 * @param[in]   func     Name of function i backend (ie downcall above) as string
 * @param[in]   param    Input parameter given to function (void* arg in downcall)
 * @param[in]   paramlen Length of input parameter
 * @param[out]  ret      Returned data as byte-string. Deallocate w unchunk...(..., label)
 * @param[out]  retlen   Length of returned data
 * @param[in]   label    Label used in chunk (de)allocation. Use:
 *                       unchunk_group(label) to deallocate
 */
int
clicon_rpc_call(clicon_handle h, 
		uint16_t      op, 
		char         *plugin, 
		char         *func,
		void         *param, 
		uint16_t      paramlen, 
		char        **ret, 
		uint16_t     *retlen,
		const void   *label)
{
    int                retval = -1;
    struct clicon_msg *msg;

    if ((msg = clicon_msg_call_encode(op, plugin, func, 
				      paramlen, param, 
				      label)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, (char**)ret, retlen, NULL, label) < 0)
	goto done;
    retval = 0;
done:
    return retval;
}

/*! Create a new notification subscription
 * @param[in]   h        Clicon handle
 * @param[in]   status   0: stop existing notification stream 1: start new stream.
 * @param{in]   stream   name of notificatio/log stream (CLICON is predefined)
 * @param{in]   filter   message filter, eg xpath for xml notifications
 * @param[out]  s0       socket returned where notification mesages will appear
 */
int
clicon_rpc_subscription(clicon_handle    h,
			int              status, 
			char            *stream, 
			enum format_enum format,
			char            *filter, 
			int             *s0)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_subscription_encode(status, stream, format, filter, 
					    __FUNCTION__)) == NULL)
	goto done;
    if (clicon_rpc_msg(h, msg, NULL, NULL, s0, __FUNCTION__) < 0)
	goto done;
    if (status == 0 && s0){
	close(*s0);
	*s0 = -1;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

