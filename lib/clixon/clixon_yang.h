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

#ifndef _CLIXON_YANG_H_
#define _CLIXON_YANG_H_


/*
 * Actually cligen variable stuff XXX
 */
#define V_UNIQUE	0x01	/* Variable flag */
#define V_UNSET		0x08	/* Variable is unset, ie no default */

/*
 * Types
 */
/*! YANG keywords from RFC6020.
 * See also keywords generated by yacc/bison in clicon_yang_parse.tab.h, but they start with K_
 * instead of Y_
 * Wanted to unify these (K_ and Y_) but gave up for several reasons:
 * - Dont want to expose a generated yacc file to the API
 * - Cant use the symbols in this file because yacc needs token definitions
 */
enum rfc_6020{
    Y_ANYXML,
    Y_ARGUMENT,
    Y_AUGMENT,
    Y_BASE,
    Y_BELONGS_TO,
    Y_BIT,
    Y_CASE,
    Y_CHOICE,
    Y_CONFIG,
    Y_CONTACT,
    Y_CONTAINER,
    Y_DEFAULT,
    Y_DESCRIPTION,
    Y_DEVIATE,
    Y_DEVIATION,
    Y_ENUM,
    Y_ERROR_APP_TAG,
    Y_ERROR_MESSAGE,
    Y_EXTENSION,
    Y_FEATURE,
    Y_FRACTION_DIGITS,
    Y_GROUPING,
    Y_IDENTITY,
    Y_IF_FEATURE,
    Y_IMPORT,
    Y_INCLUDE,
    Y_INPUT,
    Y_KEY,
    Y_LEAF,
    Y_LEAF_LIST,
    Y_LENGTH,
    Y_LIST,
    Y_MANDATORY,
    Y_MAX_ELEMENTS,
    Y_MIN_ELEMENTS,
    Y_MODULE,
    Y_MUST,
    Y_NAMESPACE,
    Y_NOTIFICATION,
    Y_ORDERED_BY,
    Y_ORGANIZATION,
    Y_OUTPUT,
    Y_PATH,
    Y_PATTERN,
    Y_POSITION,
    Y_PREFIX,
    Y_PRESENCE,
    Y_RANGE,
    Y_REFERENCE,
    Y_REFINE,
    Y_REQUIRE_INSTANCE,
    Y_REVISION,
    Y_REVISION_DATE,
    Y_RPC,
    Y_STATUS,
    Y_SUBMODULE,
    Y_TYPE,
    Y_TYPEDEF,
    Y_UNIQUE,
    Y_UNITS,
    Y_USES,
    Y_VALUE,
    Y_WHEN,
    Y_YANG_VERSION,
    Y_YIN_ELEMENT,
    Y_SPEC  /* XXX: NOTE NOT YANG STATEMENT, reserved for top level spec */
};

#define YANG_FLAG_MARK 0x01  /* Marker for dynamic algorithms, eg expand */

typedef struct yang_stmt yang_stmt; /* forward */

/*! Yang type cache. Yang type statements can cache all typedef info here
*/
struct yang_type_cache{
    int        yc_options;
    cg_var    *yc_mincv;
    cg_var    *yc_maxcv;
    char      *yc_pattern;
    uint8_t    yc_fraction;
    yang_stmt *yc_resolved; /* Resolved type object, can be NULL - note direct ptr */
};
typedef struct yang_type_cache yang_type_cache;

/*! yang statement 
 */
struct yang_stmt{
    int                ys_len;       /* Number of children */
    struct yang_stmt **ys_stmt;      /* Vector of children statement pointers */
    struct yang_node  *ys_parent;    /* Backpointer to parent: yang-stmt or yang-spec */
    enum rfc_6020      ys_keyword;   /* See clicon_yang_parse.tab.h */

    char              *ys_argument;  /* String / argument depending on keyword */   
    int                ys_flags;     /* Flags according to YANG_FLAG_* above */
    cg_var            *ys_cv;        /* cligen variable. The following stmts have cvs::
				        leaf, leaf-list, mandatory, fraction-digits */
    cvec              *ys_cvec;      /* List of stmt-specific variables 
					Y_RANGE: range_min, range_max */
    yang_type_cache   *ys_typecache; /* If ys_keyword==Y_TYPE, cache all typedef data */
};


/*! top-level yang parse-tree */
struct yang_spec{
    int                yp_len;       /* Number of children */
    struct yang_stmt **yp_stmt;      /* Vector of children statement pointers */
    struct yang_node  *yp_parent;    /* Backpointer to parent: always NULL. See yang_stmt */
    enum rfc_6020      yp_keyword;   /* SHOULD BE Y_SPEC */
    char              *yp_argument;  /* XXX String / argument depending on keyword */   
    int                yp_flags;     /* Flags according to YANG_FLAG_* above */
};
typedef struct yang_spec yang_spec;

/*! super-class of yang_stmt and yang_spec: it must start exactly as those two classes */
struct yang_node{
    int                yn_len;       /* Number of children */
    struct yang_stmt **yn_stmt;      /* Vector of children statement pointers */
    struct yang_node  *yn_parent;    /* Backpointer to parent: yang-stmt or yang-spec */
    enum rfc_6020      yn_keyword;   /* See clicon_yang_parse.tab.h */
    char              *yn_argument;  /* XXX String / argument depending on keyword */   
    int                yn_flags;     /* Flags according to YANG_FLAG_* above */
};
typedef struct yang_node yang_node;

typedef int (yang_applyfn_t)(yang_stmt *ys, void *arg);

/*
 * Prototypes
 */
yang_spec *yspec_new(void);
yang_stmt *ys_new(enum rfc_6020 keyw);
int        ys_free(yang_stmt *ys);
int        yspec_free(yang_spec *yspec);
int        ys_cp(yang_stmt *new, yang_stmt *old);
yang_stmt *ys_dup(yang_stmt *old);
int        yn_insert(yang_node *yn_parent, yang_stmt *ys_child);
yang_stmt *yn_each(yang_node *yn, yang_stmt *ys);
char      *yang_key2str(int keyword);
char      *ytype_prefix(yang_stmt *ys);
char      *ytype_id(yang_stmt *ys);
yang_stmt *ys_module(yang_stmt *ys);
yang_spec *ys_spec(yang_stmt *ys);
yang_stmt *ys_module_import(yang_stmt *ymod, char *prefix);
yang_stmt *yang_find(yang_node *yn, int keyword, char *argument);
yang_stmt *yang_find_syntax(yang_node *yn, char *argument);
yang_stmt *yang_find_topnode(yang_spec *ysp, char *name);

int        yang_print_cbuf(cbuf *cb, yang_node *yn, int marginal);
int        yang_print(FILE *f, yang_node *yn, int marginal);
int        yang_parse(clicon_handle h, const char *yang_dir, 
		      const char *module, const char *revision, yang_spec *ysp);
int        yang_apply(yang_node *yn, yang_applyfn_t fn, void *arg);
yang_stmt *dbkey2yang(yang_node *yn, char *dbkey);
yang_node *yang_xpath_abs(yang_node *yn, char *xpath);
yang_node *yang_xpath(yang_node *yn, char *xpath);
cg_var    *ys_parse(yang_stmt *ys, enum cv_type cvtype);
int        ys_parse_sub(yang_stmt *ys);
int        yang_mandatory(yang_stmt *ys);
int        yang_config(yang_stmt *ys);
int        yang_spec_main(clicon_handle h, FILE *f, int printspec);
cvec      *yang_arg2cvec(yang_stmt *ys, char *delimi);
int        yang_key_match(yang_node *yn, char *name);

#endif  /* _CLIXON_YANG_H_ */
