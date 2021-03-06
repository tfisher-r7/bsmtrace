/*-
 * Copyright (c) 2007 Aaron L. Meihm
 * Copyright (c) 2007 Christian S.J. Peron
 * All rights reserved.
 *
 * $Id$
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "includes.h"

static const struct _settype_tab {
	char	*stt_str;
	int	 stt_val;
} settype_tab[] = {
	{ "auditclass",		SET_TYPE_AUCLASS },
	{ "auditevent",		SET_TYPE_AUEVENT },
	{ "auid",		SET_TYPE_AUID },
	{ "egid",		SET_TYPE_EGID },
	{ "euid",		SET_TYPE_EUID },
	{ "path",		SET_TYPE_PATH },
#ifdef PCRE
	{ "pcre",		SET_TYPE_PCRE },
#endif
	{ "rgid",		SET_TYPE_RGID },
	{ "ruid",		SET_TYPE_RUID },
	{ "logchannel",		SET_TYPE_LOGCHANNEL },
	{ NULL,			0 }
};

extern FILE	*yyin;
extern char	*yytext;
extern int	 yyparse(void);
bsm_set_head_t	 bsm_set_head;
int		 lineno = 1;
char		*conffile;

/*
 * Return BSM set named str, or NULL if the set was not found in the BSM set
 * queue.
 */
struct bsm_set *
conf_get_bsm_set(char *str)
{
	struct bsm_set *ptr;

	TAILQ_FOREACH(ptr, &bsm_set_head, bss_glue) {
		if (strcmp(str, ptr->bss_name) == 0)
			return (ptr);
	}
	return (NULL);
}

/*
 * Return sequence with name str from parent queue, or NULL if sequence is not
 * found.
 */
struct bsm_sequence *
conf_get_parent_sequence(char *str)
{

	return (NULL);
}

/*
 * Load configuration file from path.
 */
void
conf_load(char *path)
{
	FILE *f;

	f = fopen(path, "r");
	if (f == NULL)
		bsmtrace_error(1, "%s: %s", path, strerror(errno));
	conffile = path;
	yyin = f;
	TAILQ_INIT(&bsm_set_head);
	yyparse();
	(void) fclose(f);
}

/*
 * Configuration file error reporting, non-zero ln overrides lineno.
 */
void
conf_detail(int ln, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	if (ln == 0)
		ln = lineno;
	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	bsmtrace_error(1, "%s:%d: %s", conffile, ln, buf);
}

/*
 * Add value str to array a.  We verify the value str points at is suitable for
 * an insertion into an array of type.
 */
void
conf_array_add(const char *str, struct array *a, int type)
{
	int e, value;
	char *ptr, *r, *estring;
	struct au_event_ent *aue;
	struct au_class_ent *auc;
	struct passwd *pwd;
	struct group *gr;
#ifdef PCRE
	const char *error;
	int erroffset;
	pcre *re;
#endif

	e = 0;
	switch (type) {
	case SET_TYPE_AUCLASS:
		auc = getauclassnam(str);
		if (auc == NULL) {
			estring = "audit class";
			e++;
			break;
		}
		value = auc->ac_class;
		break;
	case SET_TYPE_AUEVENT:
		aue = getauevnam(str);
		if (aue == NULL) {
			estring = "audit event";
			e++;
			break;
		}
		value = aue->ae_number;
		break;
	case SET_TYPE_AUID:
	case SET_TYPE_EUID:
	case SET_TYPE_RUID:
		pwd = getpwnam(str);
		endpwent();
		if (pwd == NULL) {
			estring = "user";
			e++;
			break;
		}
		value = pwd->pw_uid;
		break;
	case SET_TYPE_EGID:
	case SET_TYPE_RGID:
		gr = getgrnam(str);
		endgrent();
		if (gr == NULL) {
			estring = "group";
			e++;
			break;
		}
		value = gr->gr_gid;
		break;
#ifdef PCRE
	case SET_TYPE_PCRE:
		re = pcre_compile(str, 0, &error, &erroffset, NULL);
		if (error != 0)
			bsmtrace_error(1, "%s: pcre_compile failed", __func__);
		break;
#endif
	case SET_TYPE_PATH:
	case SET_TYPE_LOGCHANNEL:
		ptr = strdup(str);
		if (ptr == NULL)
			bsmtrace_error(1, "%s: strdup failed", __func__);
		break;
	}
	if (e != 0) {
		value = strtoul(str, &r, 10);
		/* XXX look for overflows */
		if (*r != '\0')
			conf_detail(0, "%s: invalid %s name\n", str, estring);
	}
	if (type == SET_TYPE_PATH || type == SET_TYPE_LOGCHANNEL) {
		a->a_data.string[a->a_cnt++] = ptr;
		a->a_type = STRING_ARRAY;
#ifdef PCRE
	} else if (type == SET_TYPE_PCRE) {
		a->a_data.pcre[a->a_cnt++] = re;
		a->a_type = PCRE_ARRAY;
#endif
	} else {
		a->a_data.value[a->a_cnt++] = value;
		a->a_type = INTEGER_ARRAY;
	}
}

/*
 * Set subject for parent sequence seq to subj.
 */
void
conf_sequence_set_subj(struct bsm_sequence *seq, struct bsm_set *subj,
    int negate)
{
	struct array *src, *dst;

	/*
	 * Make sure the passed subj actually contains subject information.
	 */
	if (subj->bss_type != SET_TYPE_AUID &&
	    subj->bss_type != SET_TYPE_RUID &&
	    subj->bss_type != SET_TYPE_EUID &&
	    subj->bss_type != SET_TYPE_RGID &&
	    subj->bss_type != SET_TYPE_EGID)
		conf_detail(0, "supplied set does not contain subjects\n");
	src = &subj->bss_data;
	dst = &seq->bs_subj.bs_par_subj;
	*dst = *src;
	seq->bs_subj.bs_par_subj.a_negated = negate;
	seq->bs_subj_type = subj->bss_type;
}

/*
 * Return set type token from string str.
 */
int
conf_set_type(char *str)
{
	const struct _settype_tab *setptr;

	for (setptr = settype_tab; setptr->stt_str != NULL; setptr++) {
		if (strcmp(setptr->stt_str, str) == 0)
			return (setptr->stt_val);
	}
	return (-1);
}

void
conf_handle_multiplier(struct bsm_sequence *bs, struct bsm_state *bm)
{
	struct bsm_state *vec, *dst;
	int i;

	assert(bm != NULL);
	if (bm->bm_multiplier == 0)
		bm->bm_multiplier = 1;
	vec = calloc(bm->bm_multiplier, sizeof(*bm));
	if (vec == NULL)
		bsmtrace_error(1, "%s: calloc failed", __func__);
	for (i = 0; i < bm->bm_multiplier; i++) {
		dst = &vec[i];
		*dst = *bm;
		TAILQ_INSERT_TAIL(&bs->bs_mhead, dst, bm_glue);
	}
}

void
yyerror(const char *str)
{

	conf_detail(0, "syntax error near '%s'", yytext);
}

int
yywrap()
{

	return (1);
}

void
conf_set_log_channel(struct bsm_set *bss, struct bsm_sequence *bs)
{
	struct array *a;
	int i;
	struct logchannel *lc;

	a = &bss->bss_data;
	for (i = 0; i < a->a_cnt; i++) {
		lc = log_lookup_channel(a->a_data.string[i]);
		if (lc == NULL)
			conf_detail(0, "unable to lookup channel");
		TAILQ_INSERT_HEAD(&bs->bs_log_channel, lc, log_glue);
	}
}

int
conf_return_scope(char *scopestr)
{

	if (strcmp(scopestr, "global") == 0)
		return (BSM_SCOPE_GLOBAL);
	else if (strcmp(scopestr, "process") == 0)
		return (BSM_SCOPE_PROCESS);
	else if (strcmp(scopestr, "session") == 0)
		return (BSM_SCOPE_SESSION);
	else if (strcmp(scopestr, "thread") == 0)
		return (BSM_SCOPE_THREAD);
	return (-1);
}
