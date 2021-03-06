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
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#define __USE_GNU /* strverscmp */
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

#include "clixon_backend_transaction.h"
#include "backend_plugin.h"
#include "backend_commit.h"

/*
 * Types
 */
/* Following are specific to backend. For common see clicon_plugin.h 
 * @note  the following should match the prototypes in clicon_backend.h
 */
#define PLUGIN_RESET           "plugin_reset"
typedef int (plgreset_t)(clicon_handle h, char *dbname); /* Reset system status */

#define PLUGIN_TRANS_BEGIN     "transaction_begin"
#define PLUGIN_TRANS_VALIDATE  "transaction_validate"
#define PLUGIN_TRANS_COMPLETE  "transaction_complete"
#define PLUGIN_TRANS_COMMIT    "transaction_commit"
#define PLUGIN_TRANS_END       "transaction_end"
#define PLUGIN_TRANS_ABORT     "transaction_abort"

typedef int (trans_cb_t)(clicon_handle h, transaction_data td); /* Transaction cbs */

/* Backend (config) plugins */
struct plugin {
    char	       p_name[PATH_MAX]; /* Plugin name */
    void	      *p_handle;	 /* Dynamic object handle */
    plginit_t	      *p_init;		 /* Init */
    plgstart_t	      *p_start;		 /* Start */
    plgexit_t         *p_exit;		 /* Exit */
    plgreset_t	      *p_reset;		 /* Reset state */
    trans_cb_t        *p_trans_begin;	 /* Transaction start */
    trans_cb_t        *p_trans_validate; /* Transaction validation */
    trans_cb_t        *p_trans_complete; /* Transaction validation complete */
    trans_cb_t        *p_trans_commit;   /* Transaction commit */
    trans_cb_t        *p_trans_end;	 /* Transaction completed  */
    trans_cb_t        *p_trans_abort;	 /* Transaction aborted */
};

/*
 * Local variables
 */
static int nplugins = 0;
static struct plugin *plugins = NULL;

/*! Find a plugin by name and return the dlsym handl
 * Used by libclicon code to find callback funcctions in plugins.
 * @param[in]  h       Clicon handle
 * @param[in]  h       Name of plugin
 * @retval     handle  Plugin handle if found
 * @retval     NULL    Not found
 */
static void *
config_find_plugin(clicon_handle h, 
		   char         *name)
{
    int            i;
    struct plugin *p;

    for (i = 0; i < nplugins; i++){
	p = &plugins[i];
	if (strcmp(p->p_name, name) == 0)
	    return p->p_handle;
    }
    return NULL;
}

/*! Initialize plugin code (not the plugins themselves)
 * @param[in]  h       Clicon handle
 * @retval     0       OK
 * @retval    -1       Error
 */
int
config_plugin_init(clicon_handle h)
{
    find_plugin_t *fp   = config_find_plugin;
    clicon_hash_t *data = clicon_data(h);

    /* Register CLICON_FIND_PLUGIN in data hash */
    if (hash_add(data, "CLICON_FIND_PLUGIN", &fp, sizeof(fp)) == NULL) {
	clicon_err(OE_UNIX, errno, "failed to register CLICON_FIND_PLUGIN");
	return -1;
    }
    return 0;
}

/*! Unload a plugin
 * @param[in]  h       Clicon handle
 * @param[in]  plg     Plugin structure
 * @retval     0       OK
 * @retval    -1       Error
 */
static int
plugin_unload(clicon_handle  h, 
	      struct plugin *plg)
{
    char *error;

    /* Call exit function is it exists */
    if (plg->p_exit)
	plg->p_exit(h);
    
    dlerror();    /* Clear any existing error */
    if (dlclose(plg->p_handle) != 0) {
	error = (char*)dlerror();
	clicon_err(OE_UNIX, 0, "dlclose: %s", error?error:"Unknown error");
	return -1;
	/* Just report */
    }
    else 
	clicon_debug(1, "Plugin '%s' unloaded.", plg->p_name);
    return 0;
}


/*! Load a dynamic plugin and call its init-function
 * @param[in]  h       Clicon handle
 * @param[in]  file    The plugin (.so) to load
 * @param[in]  dlflags Arguments to dlopen(3)
 * @param[in]  label   Chunk label
 * @retval     plugin  Plugin struct
 * @retval     NULL    Error
 */
static struct plugin *
plugin_load (clicon_handle h, 
	     char         *file, 
	     int           dlflags, 
	     const char   *label)
{
    char          *error;
    void          *handle;
    char          *name;
    struct plugin *new;
    plginit_t     *initfun;

    dlerror();    /* Clear any existing error */
    if ((handle = dlopen (file, dlflags)) == NULL) {
        error = (char*)dlerror();
	clicon_err(OE_UNIX, 0, "dlopen: %s", error?error:"Unknown error");
        return NULL;
    }
    
    initfun = dlsym(handle, PLUGIN_INIT);
    if ((error = (char*)dlerror()) != NULL) {
	clicon_err(OE_UNIX, 0, "dlsym: %s: %s", file, error);
        return NULL;
    }

    if (initfun(h) != 0) {
	dlclose(handle);
	if (!clicon_errno) 	/* sanity: log if clicon_err() is not called ! */
	    clicon_err(OE_DB, 0, "Unknown error: %s: plugin_init does not make clicon_err call on error",
		       file);
        return NULL;
    }

    if ((new = chunk(sizeof(*new), label)) == NULL) {
	clicon_err(OE_UNIX, errno, "dhunk: %s", strerror(errno));
	dlclose(handle);
	return NULL;
    }
    memset(new, 0, sizeof(*new));
    name = strrchr(file, '/') ? strrchr(file, '/')+1 : file;
    clicon_debug(2, "Loading plugin '%s'.", name);
    snprintf(new->p_name, sizeof(new->p_name), "%*s",
	     (int)strlen(name)-2, name);
    new->p_handle   = handle;
    new->p_init    = initfun;
    if ((new->p_start    = dlsym(handle, PLUGIN_START)) != NULL)
	clicon_debug(2, "%s callback registered.", PLUGIN_START);
    if ((new->p_exit     = dlsym(handle, PLUGIN_EXIT)) != NULL)
	 clicon_debug(2, "%s callback registered.", PLUGIN_EXIT);
    if ((new->p_reset    = dlsym(handle, PLUGIN_RESET)) != NULL)
	 clicon_debug(2, "%s callback registered.", PLUGIN_RESET);
    if ((new->p_trans_begin    = dlsym(handle, PLUGIN_TRANS_BEGIN)) != NULL)
	clicon_debug(2, "%s callback registered.", PLUGIN_TRANS_BEGIN);
    if ((new->p_trans_validate = dlsym(handle, PLUGIN_TRANS_VALIDATE)) != NULL)
	clicon_debug(2, "%s callback registered.", PLUGIN_TRANS_VALIDATE);
    if ((new->p_trans_complete = dlsym(handle, PLUGIN_TRANS_COMPLETE)) != NULL)
	clicon_debug(2, "%s callback registered.", PLUGIN_TRANS_COMPLETE);
    if ((new->p_trans_commit   = dlsym(handle, PLUGIN_TRANS_COMMIT)) != NULL)
	clicon_debug(2, "%s callback registered.", PLUGIN_TRANS_COMMIT);
    if ((new->p_trans_end      = dlsym(handle, PLUGIN_TRANS_END)) != NULL)
	clicon_debug(2, "%s callback registered.", PLUGIN_TRANS_END);
    if ((new->p_trans_abort    = dlsym(handle, PLUGIN_TRANS_ABORT)) != NULL)
	clicon_debug(2, "%s callback registered.", PLUGIN_TRANS_ABORT);
    clicon_debug(2, "Plugin '%s' loaded.\n", name);

    return new;
}

/*! Request plugins to reset system state
 * The system 'state' should be the same as the contents of running_db
 * @param[in]  h       Clicon handle
 * @param[in]  dbname  Name of database
 * @retval     0       OK
 * @retval    -1       Error
 */
int
plugin_reset_state(clicon_handle h, 
		   char *dbname)
{ 
    int            i;
    struct plugin *p;


    for (i = 0; i < nplugins; i++)  {
	p = &plugins[i];
	if (p->p_reset) {
	    clicon_debug(1, "Calling plugin_reset() for %s\n",
			 p->p_name);
	    if (((p->p_reset)(h, dbname)) < 0) {
		clicon_err(OE_FATAL, 0, "plugin_reset() failed for %s\n",
			   p->p_name);
		return -1;
	    }
	}
    }
    return 0;
}

/*! Call plugin_start in all plugins
 * @param[in]  h       Clicon handle
 * @param[in]  argc    Command-line arguments
 * @param[in]  argv    Command-line arguments
 * @retval     0       OK
 * @retval    -1       Error
 */
int
plugin_start_hooks(clicon_handle h, 
		   int           argc, 
		   char        **argv)
{
    int            i;
    struct plugin *p;

    for (i = 0; i < nplugins; i++)  {
	p = &plugins[i];
	if (p->p_start) {
	    optind = 0;
	    if (((p->p_start)(h, argc, argv)) < 0) {
		clicon_err(OE_FATAL, 0, "plugin_start() failed for %s\n",
			   p->p_name);
		return -1;
	    }
	}
    }
    return 0;
}
	
/*! Append plugin to list
 * @param[in]  p       Plugin
 * @retval     0       OK
 * @retval    -1       Error
 */
static int
plugin_append(struct plugin *p)
{
    struct plugin *new;
    
    if ((new = rechunk(plugins, (nplugins+1) * sizeof (*p), NULL)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	return -1;
    }
    
    memset (&new[nplugins], 0, sizeof(new[nplugins]));
    memcpy (&new[nplugins], p, sizeof(new[nplugins]));
    plugins = new;
    nplugins++;

    return 0;
}

/*! Load backend plugins found in a directory 
 * The plugins must have the '.so' suffix
 * @param[in]  h       Clicon handle
 * @param[in]  dir     Backend plugin directory
 * @retval     0       OK
 * @retval    -1       Error
 */
static int
config_plugin_load_dir(clicon_handle h, 
		       const char   *dir)
{
    int            retval = -1;
    int            i;
    int            np = 0;
    int            ndp;
    struct stat    st;
    char          *filename;
    struct dirent *dp;
    struct plugin *new;
    struct plugin *p = NULL;
    char          *master;
    char          *master_plugin;

    /* Format master plugin path */
    if ((master_plugin = clicon_master_plugin(h)) == NULL){
	clicon_err(OE_PLUGIN, 0, "clicon_master_plugin option not set");
	goto quit;
    }
    master = chunk_sprintf(__FUNCTION__, "%s.so",  master_plugin);
    if (master == NULL) {
	clicon_err(OE_PLUGIN, errno, "chunk_sprintf master plugin");
	goto quit;
    }

    /* Allocate plugin group object */
    /* Get plugin objects names from plugin directory */
    if((ndp = clicon_file_dirent(dir, &dp, "(.so)$", S_IFREG, __FUNCTION__))<0)
	goto quit;
    
    /* reset num plugins */
    np = 0;

    /* Master plugin must be loaded first if it exists. */
    filename = chunk_sprintf(__FUNCTION__, "%s/%s", dir, master);
    if (filename == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	goto quit;
    }
    if (stat(filename, &st) == 0) {
	clicon_debug(1, "Loading master plugin '%.*s' ...", 
		     (int)strlen(filename), filename);

	new = plugin_load(h, filename, RTLD_NOW|RTLD_GLOBAL, __FUNCTION__);
	if (new == NULL)
	    goto quit;
	if (plugin_append(new) < 0)
	    goto quit;
    }  

    /* Now load the rest */
    for (i = 0; i < ndp; i++) {
	if (strcmp(dp[i].d_name, master) == 0)
	    continue; /* Skip master now */
	filename = chunk_sprintf(__FUNCTION__, "%s/%s", dir, dp[i].d_name);
	clicon_debug(1, "Loading plugin '%.*s' ...",  (int)strlen(filename), filename);
	if (filename == NULL) {
	    clicon_err(OE_UNIX, errno, "chunk");
	    goto quit;
	}
	new = plugin_load (h, filename, RTLD_NOW, __FUNCTION__);
	if (new == NULL) 
	    goto quit;
	if (plugin_append(new) < 0)
	    goto quit;
    }
    
    /* All good. */
    retval = 0;
    
quit:
    if (retval != 0) {
	if (p) {
	    while (--np >= 0)
		plugin_unload (h, &p[np]);
	    unchunk(p);
	}
    }
    unchunk_group(__FUNCTION__);
    return retval;
}


/*! Load a plugin group.
 * @param[in]  h       Clicon handle
 * @retval     0       OK
 * @retval    -1       Error
 */
int
plugin_initiate(clicon_handle h)
{
    char *dir;

    /* First load CLICON system plugins */
    if (config_plugin_load_dir(h, CLIXON_BACKEND_SYSDIR) < 0)
	return -1;

    /* Then load application plugins */
    if ((dir = clicon_backend_dir(h)) == NULL){
	clicon_err(OE_PLUGIN, 0, "backend_dir not defined");
	return -1;
    }
    if (config_plugin_load_dir(h, dir) < 0)
	return -1;
    
    return 0;
}

/*! Unload and deallocate all backend plugins
 * @param[in]  h       Clicon handle
 * @retval     0       OK
 * @retval    -1       Error
 */
int
plugin_finish(clicon_handle h)
{
    int            i;
    struct plugin *p;

    for (i = 0; i < nplugins; i++) {
	p = &plugins[i];
	plugin_unload(h, p);
    }
    if (plugins)
	unchunk(plugins);
    nplugins = 0;
    return 0;
}
	
/*! Call from frontend to function 'func' in plugin 'plugin'. 
 * Plugin function is supposed to populate 'retlen' and 'retarg' where
 * 'retarg' is malloc:ed data if non-NULL.
 * @param[in]  h       Clicon handle
 * @param[in]  req     Clicon message containing information about the downcall
 * @param[out] retlen  Length of return value
 * @param[out] ret     Return value
 * @retval     0       OK
 * @retval    -1       Error
 */
int
plugin_downcall(clicon_handle               h, 
		struct clicon_msg_call_req *req,
		uint16_t                   *retlen,  
		void                      **retarg)
{
    int            retval = -1;
    int            i;
    downcall_cb    funcp;
    char           name[PATH_MAX];
    char          *error;
    struct plugin *p;

    for (i = 0; i < nplugins; i++)  {
	p = &plugins[i];
	strncpy(name, p->p_name, sizeof(name)-1);
	if (!strcmp(name+strlen(name)-3, ".so"))
	    name[strlen(name)-3] = '\0';
	/* If no plugin is given or the plugin-name matches */
	if (req->cr_plugin == NULL || strlen(req->cr_plugin)==0 ||
	    strcmp(name, req->cr_plugin) == 0) {
	    funcp = dlsym(p->p_handle, req->cr_func);
	    if ((error = (char*)dlerror()) != NULL) {
		clicon_err(OE_PROTO, ENOENT,
			"Function does not exist: %s()", req->cr_func);
		return -1;
	    }
	    retval = funcp(h, req->cr_op, req->cr_arglen,  req->cr_arg, retlen, retarg);
	    goto done;
	}
    }
    clicon_err(OE_PROTO, ENOENT,"%s: %s(): Plugin does not exist: %s",
	       __FUNCTION__, req->cr_func, req->cr_plugin);
    return -1;
    
done:
    return retval;
}
	
/*! Create and initialize transaction */
transaction_data_t *
transaction_new(void)
{
    transaction_data_t *td;
    static uint64_t     id = 0; /* Global transaction id */

    if ((td = malloc(sizeof(*td))) == NULL){
	clicon_err(OE_CFG, errno, "malloc");
	return NULL;
    }
    memset(td, 0, sizeof(*td));
    td->td_id = id++;
    return td;
}

/*! Free transaction structure */
int 
transaction_free(transaction_data_t *td)
{
    if (td->td_src)
	xml_free(td->td_src);
    if (td->td_target)
	xml_free(td->td_target);
    if (td->td_dvec)
	free(td->td_dvec);
    if (td->td_avec)
	free(td->td_avec);
    if (td->td_scvec)
	free(td->td_scvec);
    if (td->td_tcvec)
	free(td->td_tcvec);
    free(td);
    return 0;
}

/* The plugin_transaction routines need access to struct plugin which is local to this file */

/*! Call transaction_begin() in all plugins before a validate/commit.
 * @param[in]  h       Clicon handle
 * @param[in]  td      Transaction data
 * @retval     0       OK
 * @retval    -1       Error: one of the plugin callbacks returned error
 */
int
plugin_transaction_begin(clicon_handle       h, 
			  transaction_data_t *td)
{
    int            i;
    int            retval = 0;
    struct plugin *p;

    for (i = 0; i < nplugins; i++) {
	p = &plugins[i];
	if (p->p_trans_begin) 
	    if ((retval = (p->p_trans_begin)(h, (transaction_data)td)) < 0){
		if (!clicon_errno) /* sanity: log if clicon_err() is not called ! */
		    clicon_log(LOG_NOTICE, "%s: Plugin '%s' %s callback does not make clicon_err call on error", 
			       __FUNCTION__, p->p_name, PLUGIN_TRANS_BEGIN);

		break;
	    }

    }
    return retval;
}

/*! Call transaction_validate callbacks in all backend plugins
 * @param[in]  h       Clicon handle
 * @param[in]  td      Transaction data
 * @retval     0       OK. Validation succeeded in all plugins
 * @retval    -1       Error: one of the plugin callbacks returned validation fail
 */
int
plugin_transaction_validate(clicon_handle       h, 	 
			    transaction_data_t *td)
{
    int            retval = 0;
    int            i;

    struct plugin *p;

    for (i = 0; i < nplugins; i++){
	p = &plugins[i];
	if (p->p_trans_validate) 
	    if ((retval = (p->p_trans_validate)(h, (transaction_data)td)) < 0){
		if (!clicon_errno) /* sanity: log if clicon_err() is not called ! */
		    clicon_log(LOG_NOTICE, "%s: Plugin '%s' %s callback does not make clicon_err call on error", 
			       __FUNCTION__, p->p_name, PLUGIN_TRANS_VALIDATE);

		break;
	    }
    }
    return retval;
}

/*! Call transaction_complete() in all plugins after validation (before commit)
 * @param[in]  h       Clicon handle
 * @param[in]  td      Transaction data
 * @retval     0       OK
 * @retval    -1       Error: one of the plugin callbacks returned error
 * @note Call plugins which have commit dependencies?
 * @note Rename to transaction_complete?
 */
int
plugin_transaction_complete(clicon_handle       h, 
			    transaction_data_t *td)
{
    int            i;
    int            retval = 0;
    struct plugin *p;
    
    for (i = 0; i < nplugins; i++){
	p = &plugins[i];
	if (p->p_trans_complete) 
	    if ((retval = (p->p_trans_complete)(h, (transaction_data)td)) < 0){
		if (!clicon_errno) /* sanity: log if clicon_err() is not called ! */
		    clicon_log(LOG_NOTICE, "%s: Plugin '%s' %s callback does not make clicon_err call on error", 
			       __FUNCTION__, p->p_name, PLUGIN_TRANS_COMPLETE);

		break;
	    }
    }
    return retval;
}

/*! Revert a commit
 * @param[in]  h   CLICON handle
 * @param[in]  td  Transaction data
 * @param[in]  nr  The plugin where an error occured. 
 * @retval     0       OK
 * @retval    -1       Error
 * The revert is made in plugin before this one. Eg if error occurred in
 * plugin 2, then the revert will be made in plugins 1 and 0.
 */
int
plugin_transaction_revert(clicon_handle       h, 
			  transaction_data_t *td,
			  int                 nr)
{
    int                retval = 0;
    transaction_data_t tr; /* revert transaction */
    int                i;
    struct plugin     *p;

    /* Create a new reversed transaction from the original where src and target
       are swapped */
    memcpy(&tr, td, sizeof(tr));
    tr.td_src   = td->td_target;
    tr.td_target = td->td_src;
    tr.td_dlen  = td->td_alen;
    tr.td_dvec  = td->td_avec;
    tr.td_alen  = td->td_dlen;
    tr.td_avec  = td->td_dvec;
    tr.td_clen  = td->td_clen;
    tr.td_scvec = td->td_tcvec;
    tr.td_tcvec = td->td_scvec;

    for (i = nr-1; i>=0; i--){
	p = &plugins[i];
	if (p->p_trans_commit) 
	    if ((p->p_trans_commit)(h, (transaction_data)&tr) < 0){
		clicon_log(LOG_NOTICE, "Plugin '%s' %s revert callback failed", 
			   p->p_name, PLUGIN_TRANS_COMMIT);
		break; 
	    }
    }
    return retval; /* ignore errors */
}

/*! Call transaction_commit callbacks in all backend plugins
 * @param[in]  h       Clicon handle
 * @param[in]  td      Transaction data
 * @retval     0       OK
 * @retval    -1       Error: one of the plugin callbacks returned error
 * If any of the commit callbacks fail by returning -1, a revert of the 
 * transaction is tried by calling the commit callbacsk with reverse arguments
 * and in reverse order.
 */
int
plugin_transaction_commit(clicon_handle       h, 
			  transaction_data_t *td)
{
    int            retval = 0;
    int            i;
    struct plugin *p;

    for (i = 0; i < nplugins; i++){
	p = &plugins[i];
	if (p->p_trans_commit) 
	    if ((retval = (p->p_trans_commit)(h, (transaction_data)td)) < 0){
		if (!clicon_errno) /* sanity: log if clicon_err() is not called ! */
		    clicon_log(LOG_NOTICE, "%s: Plugin '%s' %s callback does not make clicon_err call on error", 
			       __FUNCTION__, p->p_name, PLUGIN_TRANS_COMMIT);
		/* Make an effort to revert transaction */
		plugin_transaction_revert(h, td, i); 
		break;
	    }
    }
    return retval;
}

/*! Call transaction_end() in all plugins after a successful commit.
 * @param[in]  h       Clicon handle
 * @param[in]  td      Transaction data
 * @retval     0       OK
 * @retval    -1       Error
 */
int
plugin_transaction_end(clicon_handle h,
		       transaction_data_t *td)
{
    int            retval = 0;
    int            i;
    struct plugin *p;
    
    for (i = 0; i < nplugins; i++) {
	p = &plugins[i];
	if (p->p_trans_end) 
	    if ((retval = (p->p_trans_end)(h, (transaction_data)td)) < 0){
		if (!clicon_errno) /* sanity: log if clicon_err() is not called ! */
		    clicon_log(LOG_NOTICE, "%s: Plugin '%s' %s callback does not make clicon_err call on error", 
			       __FUNCTION__, p->p_name, PLUGIN_TRANS_END);

		break;
	    }
    }
    return retval;
}

/*! Call transaction_abort() in all plugins after a failed validation/commit.
 * @param[in]  h       Clicon handle
 * @param[in]  td      Transaction data
 * @retval     0       OK
 * @retval    -1       Error
 */
int
plugin_transaction_abort(clicon_handle       h, 
			 transaction_data_t *td)
{
    int            retval = 0;
    int            i;
    struct plugin *p;

    for (i = 0; i < nplugins; i++)  {
	p = &plugins[i];
	if (p->p_trans_abort) 
	    (p->p_trans_abort)(h, (transaction_data)td); /* dont abort on error */
    }
    return retval;
}
	
	
