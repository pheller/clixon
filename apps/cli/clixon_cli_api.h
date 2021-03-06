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

  * Note, this is a CLICON API file, only exprorted function prototypes should appear here
 */

#ifndef _CLIXON_CLI_API_H_
#define _CLIXON_CLI_API_H_

/*
 * Constants
 */
/* Max prompt length */
#define CLI_PROMPT_LEN 64
#define CLI_DEFAULT_PROMPT	">"

/* 
 * Function Declarations 
 */
/* cli_plugin.c */
int cli_set_syntax_mode(clicon_handle h, const char *mode);
char *cli_syntax_mode(clicon_handle h);
int cli_syntax_load(clicon_handle h);
int cli_handler_err(FILE *fd);
int cli_set_prompt(clicon_handle h, const char *mode, const char *prompt);
char *cli_prompt(char *fmt);
int cli_exec(clicon_handle h, char *cmd, char **mode, int *result);
int cli_ptpush(clicon_handle h, char *mode, char *string, char *op);
int cli_ptpop(clicon_handle h, char *mode, char *op);

/* cli_handle.c */
char cli_set_comment(clicon_handle h, char c);
char cli_comment(clicon_handle h);
int cli_set_exiting(clicon_handle h, int exiting);
int cli_exiting(clicon_handle h);
clicon_handle cli_handle_init(void);
int cli_handle_exit(clicon_handle h);
cligen_handle cli_cligen(clicon_handle h);

/* cli_common.c */
int init_candidate_db(clicon_handle h);
int exit_candidate_db(clicon_handle h);
#define cli_output cligen_output
int cli_set (clicon_handle h, cvec *vars, cg_var *arg);
int cli_merge (clicon_handle h, cvec *vars, cg_var *arg);
int cli_del(clicon_handle h, cvec *vars, cg_var *argv);
int cli_debug_cli(clicon_handle h, cvec *vars, cg_var *argv);
int cli_debug_backend(clicon_handle h, cvec *vars, cg_var *argv);
int cli_record(clicon_handle h, cvec *vars, cg_var *argv);
int isrecording(void);
int record_command(char *str);
int cli_set_mode(clicon_handle h, cvec *vars, cg_var *argv);
int cli_start_shell(clicon_handle h, cvec *vars, cg_var *argv);
int cli_quit(clicon_handle h, cvec *vars, cg_var *arg);
int cli_commit(clicon_handle h, cvec *vars, cg_var *arg);
int cli_validate(clicon_handle h, cvec *vars, cg_var *arg);
int compare_dbs(clicon_handle h, cvec *vars, cg_var *arg);

int load_config_file(clicon_handle h, cvec *vars, cg_var *arg);
int save_config_file(clicon_handle h, cvec *vars, cg_var *arg);
int delete_all(clicon_handle h, cvec *vars, cg_var *arg);
int discard_changes(clicon_handle h, cvec *vars, cg_var *arg);
int cli_notify(clicon_handle h, cvec *cvv, cg_var *arg);
int cli_notification_register(clicon_handle h, char *stream, enum format_enum format,
			      char *filter, int status, 
			      int (*fn)(int, void*), void *arg);

/* In cli_show.c */
int expand_dir(char *dir, int *nr, char ***commands, mode_t flags, int detail);
int expand_dbvar(void *h, char *name, cvec *cvv, cg_var *arg, 
		 int *nr, char ***commands, char ***helptexts);

int show_conf_as_xml(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_netconf(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_json(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_text(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_cli(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_csv(clicon_handle h, cvec *vars, cg_var *arg);
int show_yang(clicon_handle h, cvec *vars, cg_var *arg);

#endif /* _CLIXON_CLI_API_H_ */
