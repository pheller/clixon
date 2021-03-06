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

 * XML support functions.
 */
#ifndef _CLIXON_XML_H
#define _CLIXON_XML_H

/*
 * Types
 */
/* Netconf operation type */
enum operation_type{ /* edit-config */
    OP_MERGE,  /* merge config-data */
    OP_REPLACE,/* replace or create config-data */
    OP_CREATE, /* create config data, error if exist */
    OP_DELETE, /* delete config data, error if it does not exist */
    OP_REMOVE, /* delete config data */
    OP_NONE
};

enum cxobj_type {CX_ERROR=-1, 
		 CX_ELMNT, 
		 CX_ATTR, 
		 CX_BODY};
#define CX_ANY CX_ERROR /* catch all and error is same */

typedef struct xml cxobj; /* struct defined in clicon_xml.c */

/*! Callback function type for xml_apply */
typedef int (xml_applyfn_t)(cxobj *yn, void *arg);

/*
 * xml_flag() flags:
 */
#define XML_FLAG_MARK   0x01  /* Marker for dynamic algorithms, eg expand */
#define XML_FLAG_ADD    0x02  /* Node is added (commits) or parent added rec*/
#define XML_FLAG_DEL    0x04  /* Node is deleted (commits) or parent deleted rec */
#define XML_FLAG_CHANGE 0x08  /* Node is changed (commits) or child changed rec */

/*
 * Prototypes
 */
char     *xml_name(cxobj *xn);
int       xml_name_set(cxobj *xn, char *name);
char     *xml_namespace(cxobj *xn);
int       xml_namespace_set(cxobj *xn, char *name);
cxobj    *xml_parent(cxobj *xn);
int       xml_parent_set(cxobj *xn, cxobj *parent);

uint16_t  xml_flag(cxobj *xn, uint16_t flag);
int       xml_flag_set(cxobj *xn, uint16_t flag);
int       xml_flag_reset(cxobj *xn, uint16_t flag);

char     *xml_value(cxobj *xn);
int       xml_value_set(cxobj *xn, char *val);
char     *xml_value_append(cxobj *xn, char *val);
enum cxobj_type xml_type(cxobj *xn);
int       xml_type_set(cxobj *xn, enum cxobj_type type);
int       xml_index(cxobj *xn);
int       xml_index_set(cxobj *xn, int index);

cg_var *xml_cv_get(cxobj *xn);
int     xml_cv_set(cxobj  *xn, cg_var *cv);

int       xml_child_nr(cxobj *xn);
cxobj    *xml_child_i(cxobj *xn, int i);
cxobj    *xml_child_i_set(cxobj *xt, int i, cxobj *xc);
cxobj    *xml_child_each(cxobj *xparent, cxobj *xprev,  enum cxobj_type type);

int       xml_childvec_set(cxobj *x, int len);
cxobj    *xml_new(char *name, cxobj *xn_parent);
cxobj    *xml_new_spec(char *name, cxobj *xn_parent, void *spec);
void     *xml_spec(cxobj *x);
cxobj    *xml_find(cxobj *xn_parent, char *name);

int       xml_addsub(cxobj *xp, cxobj *xc);
cxobj    *xml_insert(cxobj *xt, char *tag);
int       xml_purge(cxobj *xc);
int       xml_child_rm(cxobj *xp, int i);
int       xml_rm(cxobj *xc);
int       xml_rootchild(cxobj  *xp, int i, cxobj **xcp);

char     *xml_body(cxobj *xn);
char     *xml_find_value(cxobj *xn_parent, char *name);
char     *xml_find_body(cxobj *xn, char *name);

int       xml_free(cxobj *xn);

int       xml_print(FILE  *f, cxobj *xn);
int       clicon_xml2file(FILE *f, cxobj *xn, int level, int prettyprint);
int       clicon_xml2cbuf(cbuf *xf, cxobj *xn, int level, int prettyprint);
int       clicon_xml_parse_file(int fd, cxobj **xml_top, char *endtag);
/* XXX obsolete */
#define clicon_xml_parse_string(str, x) clicon_xml_parse_str((*str), x) 
int       clicon_xml_parse_str(char *str, cxobj **xml_top);

int       xml_copy(cxobj *x0, cxobj *x1);
cxobj    *xml_dup(cxobj *x0);

int       cxvec_dup(cxobj **vec0, size_t len0, cxobj ***vec1, size_t *len1);
int       cxvec_append(cxobj *x, cxobj ***vec, size_t  *len);
int       xml_apply(cxobj *xn, enum cxobj_type type, xml_applyfn_t fn, void *arg);
int       xml_apply_ancestor(cxobj *xn, xml_applyfn_t fn, void *arg);

int       xml_body_parse(cxobj *xb, enum cv_type type, cg_var **cvp);
int       xml_body_int32(cxobj *xb, int32_t *val);
int       xml_body_uint32(cxobj *xb, uint32_t *val);

#endif /* _CLIXON_XML_H */
