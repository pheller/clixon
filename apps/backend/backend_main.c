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

 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>
#include <ifaddrs.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/types.h>
#include <grp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

#include "clixon_backend_handle.h"
#include "backend_socket.h"
#include "backend_client.h"
#include "backend_commit.h"
#include "backend_plugin.h"
#include "backend_handle.h"

/* Command line options to be passed to getopt(3) */
#define BACKEND_OPTS "hD:f:d:Fzu:P:1IRCc::rg:ptx:"

/*! Terminate. Cannot use h after this */
static int
config_terminate(clicon_handle h)
{
    yang_spec      *yspec;
    char           *pidfile = clicon_backend_pidfile(h);
    char           *sockpath = clicon_sock(h);

    clicon_debug(1, "%s", __FUNCTION__);
    if ((yspec = clicon_dbspec_yang(h)) != NULL)
	yspec_free(yspec);
    plugin_finish(h);
    if (pidfile)
	unlink(pidfile);   
    if (sockpath)
	unlink(sockpath);   
    backend_handle_exit(h); /* Cannot use h after this */
    clicon_log_register_callback(NULL, NULL);
    clicon_debug(1, "%s done", __FUNCTION__); 
    if (debug)
	chunk_check(stderr, NULL);
    return 0;
}

/*! Unlink pidfile and quit
 */
static void
config_sig_term(int arg)
{
    static int i=0;
    if (i++ == 0)
	clicon_log(LOG_NOTICE, "%s: %s: pid: %u Signal %d", 
		   __PROGRAM__, __FUNCTION__, getpid(), arg);
    clicon_exit_set(); /* checked in event_loop() */
}

/*
 * usage
 */
static void
usage(char *argv0, clicon_handle h)
{
    char *plgdir   = clicon_backend_dir(h);
    char *confsock = clicon_sock(h);
    char *confpid  = clicon_backend_pidfile(h);
    char *startup  = clicon_startup_config(h);
    char *group    = clicon_sock_group(h);

    fprintf(stderr, "usage:%s\n"
	    "where options are\n"
            "    -h\t\tHelp\n"
    	    "    -D <level>\tdebug\n"
    	    "    -f <file>\tCLICON config file (mandatory)\n"
	    "    -d <dir>\tSpecify backend plugin directory (default: %s)\n"
    	    "    -z\t\tKill other config daemon and exit\n"
    	    "    -F\t\tforeground\n"
    	    "    -1\t\tonce (dont wait for events)\n"
    	    "    -u <path>\tconfig UNIX domain path / ip address (default: %s)\n"
    	    "    -P <file>\tPid filename (default: %s)\n"
    	    "    -I\t\tInitialize running state database\n"
    	    "    -R\t\tCall plugin_reset() in plugins to reset system state in running db (use with -I)\n"
	    "    -C\t\tCall plugin_reset() in plugins to reset system state in candidate db (use with -I)\n"
	    "    -c [<file>]\tLoad specified application config. Default is\n"
	    "              \t\"CLICON_STARTUP_CONFIG\" = %s\n"
	    "    -r\t\tReload running database\n"
	    "    -p \t\tPrint database yang specification\n"
	    "    -t \t\tPrint alternate spec translation (eg if YANG print KEY, if KEY print YANG)\n"
	    "    -g <group>\tClient membership required to this group (default: %s)\n"
	    "    -x <status>\tSet CLICON_XMLDB_RPC to 0 or 1.\n",
	    argv0,
	    plgdir ? plgdir : "none",
	    confsock ? confsock : "none",
	    confpid ? confpid : "none",
	    startup ? startup : "none",
	    group ? group : "none"
	    );
    exit(-1);
}

static int
rundb_init(clicon_handle h)
{
    if (xmldb_delete(h, "running") != 0 && errno != ENOENT) 
	return -1;
    if (xmldb_init(h, "running") < 0)
	return -1;
    return 0;
}

/*! Initialize running-config from file application configuration
 *
 * @param[in] h                clicon handle
 * @param[in] app_config_file  clicon application configuration file
 * @param[in] running_db       Name of running db
 * @retval    0                OK
 * @retval   -1                Error. clicon_err set
 */
static int
rundb_main(clicon_handle h, 
	   char         *app_config_file)
{
    int        retval = -1;
    int        fd = -1;
    cxobj     *xt = NULL;
    cxobj     *xn;

    if (xmldb_init(h, "tmp") < 0)
	goto done;
    if (xmldb_copy(h, "running", "tmp") < 0){
	clicon_err(OE_UNIX, errno, "file copy");
	goto done;
    }
    if ((fd = open(app_config_file, O_RDONLY)) < 0){
	clicon_err(OE_UNIX, errno, "open(%s)", app_config_file);
	goto done;
    }
    if (clicon_xml_parse_file(fd, &xt, "</clicon>") < 0)
	goto done;
    if ((xn = xml_child_i(xt, 0)) != NULL)
	if (xmldb_put(h, "tmp", xn, OP_MERGE) < 0)
	    goto done;
    if (candidate_commit(h, "tmp") < 0)
	goto done;
    if (xmldb_delete(h, "tmp") < 0)
	goto done;
    retval = 0;
done:
    if (xt)
	xml_free(xt);
    if (fd != -1)
	close(fd);
    unchunk_group(__FUNCTION__);
    return retval;
}

static int
candb_reset(clicon_handle h)
{
    int retval = -1;

    if (xmldb_copy(h, "running", "tmp") < 0){
	clicon_err(OE_UNIX, errno, "file copy");
	goto done;
    }
    /* Request plugins to reset system state, eg initiate running from system 
     * -R
     */
    if (plugin_reset_state(h, "tmp") < 0)  
	goto done;
    if (candidate_commit(h, "tmp") < 0)
	goto done;
    retval = 0;
  done:
    return retval;
}


/*! Create backend server socket and register callback
 */
static int
server_socket(clicon_handle h)
{
    int ss;

    /* Open control socket */
    if ((ss = config_socket_init(h)) < 0)
	return -1;
    /* ss is a server socket that the clients connect to. The callback
       therefore accepts clients on ss */
    if (event_reg_fd(ss, config_accept_client, h, "server socket") < 0) {
	close(ss);
	return -1;
    }
    return ss;
}

/*! Callback for CLICON log events
 * If you make a subscription to CLICON stream, this function is called for every
 * log event.
 */
static int
config_log_cb(int level, char *msg, void *arg)
{
    size_t n;
    char *ptr;
    char *nptr;
    char *newmsg = NULL;
    int retval = -1;

    /* backend_notify() will go through all clients and see if any has registered "CLICON",
       and if so make a clicon_proto notify message to those clients.   */
    

    /* Sanitize '%' into "%%" to prevent segvfaults in vsnprintf later.
       At this stage all formatting is already done */
    n = 0;
    for(ptr=msg; *ptr; ptr++)
	if (*ptr == '%')
	    n++;
    if ((newmsg = malloc(strlen(msg) + n + 1)) == NULL) {
	clicon_err(OE_UNIX, errno, "malloc");
	return -1;
    }
    for(ptr=msg, nptr=newmsg; *ptr; ptr++) {
	*nptr++ = *ptr;
	if (*ptr == '%')
	    *nptr++ = '%';
    }
    
    retval = backend_notify(arg, "CLICON", level, newmsg);
    free(newmsg);

    return retval;
}

int
main(int argc, char **argv)
{
    char          c;
    int           zap;
    int           foreground;
    int           once;
    int           init_rundb;
    int           reload_running;
    int           reset_state_running;
    int           reset_state_candidate;
    char         *app_config_file = NULL;
    char         *config_group;
    char         *argv0 = argv[0];
    char         *tmp;
    struct stat   st;
    clicon_handle h;
    int           help = 0;
    int           printspec = 0;
    int           printalt = 0;
    int           pid;
    char         *pidfile;
    char         *sock;
    int           sockfamily;

    /* In the startup, logs to stderr & syslog and debug flag set later */
    clicon_log_init(__PROGRAM__, LOG_INFO, CLICON_LOG_STDERR|CLICON_LOG_SYSLOG); 
    /* Initiate CLICON handle */
    if ((h = backend_handle_init()) == NULL)
	return -1;
    if (config_plugin_init(h) != 0) 
	return -1;
    foreground = 0;
    once = 0;
    zap = 0;
    init_rundb = 0;
    reload_running = 0;
    reset_state_running = 0;
    reset_state_candidate = 0;

    /*
     * Command-line options for help, debug, and config-file
     */
    opterr = 0;
    optind = 1;
    while ((c = getopt(argc, argv, BACKEND_OPTS)) != -1)
	switch (c) {
	case '?':
	case 'h':
	    /* Defer the call to usage() to later. Reason is that for helpful
	       text messages, default dirs, etc, are not set until later.
	       But this measn that we need to check if 'help' is set before 
	       exiting, and then call usage() before exit.
	    */
	    help = 1; 
	    break;
	case 'D' : /* debug */
	    if (sscanf(optarg, "%d", &debug) != 1)
		usage(argv[0], h);
	    break;
	case 'f': /* config file */
	    if (!strlen(optarg))
		usage(argv[0], h);
	    clicon_option_str_set(h, "CLICON_CONFIGFILE", optarg);
	    break;
	}
    /* 
     * Syslogs also to stderr, but later turn stderr off in daemon mode. 
     * error only to syslog. debug to syslog
     */
    clicon_log_init(__PROGRAM__, debug?LOG_DEBUG:LOG_INFO, CLICON_LOG_STDERR|CLICON_LOG_SYSLOG); 
    clicon_debug_init(debug, NULL);

    /* Find and read configfile */
    if (clicon_options_main(h) < 0){
	if (help)
	    usage(argv[0], h);
	return -1;
    }

    /* Now run through the operational args */
    opterr = 1;
    optind = 1;
    while ((c = getopt(argc, argv, BACKEND_OPTS)) != -1)
	switch (c) {
	case 'D' : /* debug */
	case 'f': /* config file */
	    break; /* see above */
	case 'd':  /* Plugin directory */
	    if (!strlen(optarg))
		usage(argv[0], h);
	    clicon_option_str_set(h, "CLICON_BACKEND_DIR", optarg);
	    break;
	case 'F' : /* foreground */
	    foreground = 1;
	    break;
	case '1' : /* Quit after reading database once - dont wait for events */
	    once = 1;
	    break;
	case 'z': /* Zap other process */
	    zap++;
	    break;
	 case 'u': /* config unix domain path / ip address */
	    if (!strlen(optarg))
		usage(argv[0], h);
	    clicon_option_str_set(h, "CLICON_SOCK", optarg);
	     break;
	 case 'P': /* pidfile */
	     clicon_option_str_set(h, "CLICON_BACKEND_PIDFILE", optarg);
	     break;
	 case 'I': /* Initiate running db */
	     init_rundb++;
	     break;
	 case 'R': /* Reset state directly into running */
	     reset_state_running++;
	     break;
	 case 'C': /* Reset state into candidate and then commit it */
	     reset_state_candidate++;
	     break;
	 case 'c': /* Load application config */
	     app_config_file = optarg ? optarg : clicon_startup_config(h);
	     if (app_config_file == NULL) {
		 fprintf(stderr, "Option \"CLICON_STARTUP_CONFIG\" not set\n");
		 return -1;
	     }
	     break;
	 case 'r': /* Reload running */
	     reload_running++;
	     break;
	 case 'g': /* config socket group */
	     clicon_option_str_set(h, "CLICON_SOCK_GROUP", optarg);
	     break;
	case 'p' : /* Print spec */
	    printspec++;
	    break;
	case 't' : /* Print alternative dbspec format (eg if YANG, print KEY) */
	    printalt++;
	    break;
	case 'x' : /* set xmldb rpc on */
	    {
		int i;
		if (sscanf(optarg, "%d", &i) != 1)
		    usage(argv[0], h);
		clicon_option_int_set(h, "CLICON_XMLDB_RPC", i);
	    }
	    break;
	default:
	    usage(argv[0], h);
	    break;
	}

    argc -= optind;
    argv += optind;

    /* Defer: Wait to the last minute to print help message */
    if (help)
	usage(argv[0], h);

    /* Check pid-file, if zap kil the old daemon, else return here */
    if ((pidfile = clicon_backend_pidfile(h)) == NULL){
	clicon_err(OE_FATAL, 0, "pidfile not set");
	goto done;
    }
    sockfamily = clicon_sock_family(h);
    if ((sock = clicon_sock(h)) == NULL){
	clicon_err(OE_FATAL, 0, "sock not set");
	goto done;
    }
    if (pidfile_get(pidfile, &pid) < 0)
	return -1;
    if (zap){
	if (pid && pidfile_zapold(pid) < 0)
	    return -1;
	if (lstat(pidfile, &st) == 0)
	    unlink(pidfile);   
	if (sockfamily==AF_UNIX && lstat(sock, &st) == 0)
	    unlink(sock);   
	exit(0); /* OK */
    }
    else
	if (pid){
	    clicon_err(OE_DEMON, 0, "Daemon already running with pid %d\n(Try killing it with %s -z)", 
		       pid, argv0);
	    return -1; /* goto done deletes pidfile */
	}

    /* After this point we can goto done on error 
     * Here there is either no old process or we have killed it,.. 
     */
    if (lstat(pidfile, &st) == 0)
	unlink(pidfile);   
    if (sockfamily==AF_UNIX && lstat(sock, &st) == 0)
	unlink(sock);   

    /* Sanity check: config group exists */
    if ((config_group = clicon_sock_group(h)) == NULL){
	clicon_err(OE_FATAL, 0, "clicon_sock_group option not set");
	return -1;
    }

    if (group_name2gid(config_group, NULL) < 0){
	clicon_log(LOG_ERR, "'%s' does not seem to be a valid user group.\n" 
		"The config demon requires a valid group to create a server UNIX socket\n"
		"Define a valid CLICON_SOCK_GROUP in %s or via the -g option\n"
		"or create the group and add the user to it. On linux for example:"
		"  sudo groupadd %s\n" 
		"  sudo usermod -a -G %s user\n", 
		   config_group, clicon_configfile(h), config_group, config_group);
	return -1;
    }

    /* Parse db spec file */
    if (yang_spec_main(h, stdout, printspec) < 0)
	goto done;

    /* If running exists and reload_running set, make a copy to candidate */
    if (reload_running){
	if (xmldb_exists(h, "running") != 1){
	    clicon_log(LOG_NOTICE, "%s: -r (reload running) option given but no running_db found, proceeding without", __PROGRAM__);
	    reload_running = 0; /* void it, so we dont commit candidate below */
	}
	else
	    if (xmldb_copy(h, "running", "candidate") < 0)
		goto done;
    }
    /* Init running db 
     * -I or if it isnt there
     */
    if (init_rundb || xmldb_exists(h, "running") != 1)
	if (rundb_init(h) < 0)
	    goto done;

    /* Initialize plugins 
       (also calls plugin_init() and plugin_start(argc,argv) in each plugin */
    if (plugin_initiate(h) != 0) 
	goto done;
    
    if (reset_state_candidate){
	if (candb_reset(h) < 0) 
	    goto done;
    }
    else
	if (reset_state_running){
	    if (plugin_reset_state(h, "running") < 0) 
		goto done;
	}
    /* Call plugin_start */
    tmp = *(argv-1);
    *(argv-1) = argv0;
    if (plugin_start_hooks(h, argc+1, argv-1) < 0) 
	goto done;
    *(argv-1) = tmp;

    if (reload_running){
	if (candidate_commit(h, "candidate") < 0)
	    goto done;
    }

    /* Have we specified a config file to load? eg 
       -c <file>  
       -r replace running (obsolete)
    */
    if (app_config_file)
	if (rundb_main(h, app_config_file) < 0)
	    goto done;

    /* Initiate the shared candidate. Maybe we should not do this? 
     * Too strict access
     */
    if (xmldb_copy(h, "running", "candidate") < 0)
	goto done;
    if (once)
	goto done;

    /* Daemonize and initiate logging. Note error is initiated here to make
       demonized errors OK. Before this stage, errors are logged on stderr 
       also */
    if (foreground==0){
	clicon_log_init(__PROGRAM__, debug?LOG_DEBUG:LOG_INFO, CLICON_LOG_SYSLOG); 
	if (daemon(0, 0) < 0){
	    fprintf(stderr, "config: daemon");
	    exit(-1);
	}
    }
    /* Write pid-file */

    if ((pid = pidfile_write(pidfile)) <  0)
	goto done;

    /* Register log notifications */
    if (clicon_log_register_callback(config_log_cb, h) < 0)
	goto done;
    clicon_log(LOG_NOTICE, "%s: %u Started", __PROGRAM__, getpid());
    if (set_signal(SIGTERM, config_sig_term, NULL) < 0){
	clicon_err(OE_DEMON, errno, "Setting signal");
	goto done;
    }
    if (set_signal(SIGINT, config_sig_term, NULL) < 0){
	clicon_err(OE_DEMON, errno, "Setting signal");
	goto done;
    }
	
    /* Initialize server socket */
    if (server_socket(h) < 0)
	goto done;

    if (debug)
	clicon_option_dump(h, debug);

    if (event_loop() < 0)
	goto done;
  done:
    clicon_log(LOG_NOTICE, "%s: %u Terminated", __PROGRAM__, getpid());
    config_terminate(h); /* Cannot use h after this */

    return 0;
}
