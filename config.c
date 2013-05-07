/*
 * Pound - the reverse-proxy load-balancer
 * Copyright (C) 2002-2010 Apsis GmbH
 *
 * This file is part of Pound.
 *
 * Pound is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Pound is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Contact information:
 * Apsis GmbH
 * P.O.Box
 * 8707 Uetikon am See
 * Switzerland
 * EMail: roseg@apsis.ch
 */

#ifndef MISS_FACILITYNAMES
#define SYSLOG_NAMES    1
#endif

#include    "pound.h"

#ifdef MISS_FACILITYNAMES

/* This is lifted verbatim from the Linux sys/syslog.h */

typedef struct _code {
	char	*c_name;
	int	c_val;
} CODE;

static CODE facilitynames[] = {
    { "auth", LOG_AUTH },
#ifdef  LOG_AUTHPRIV
    { "authpriv", LOG_AUTHPRIV },
#endif
    { "cron", LOG_CRON },
    { "daemon", LOG_DAEMON },
#ifdef  LOG_FTP
    { "ftp", LOG_FTP },
#endif
    { "kern", LOG_KERN },
    { "lpr", LOG_LPR },
    { "mail", LOG_MAIL },
    { "mark", 0 },                  /* never used! */
    { "news", LOG_NEWS },
    { "security", LOG_AUTH },       /* DEPRECATED */
    { "syslog", LOG_SYSLOG },
    { "user", LOG_USER },
    { "uucp", LOG_UUCP },
    { "local0", LOG_LOCAL0 },
    { "local1", LOG_LOCAL1 },
    { "local2", LOG_LOCAL2 },
    { "local3", LOG_LOCAL3 },
    { "local4", LOG_LOCAL4 },
    { "local5", LOG_LOCAL5 },
    { "local6", LOG_LOCAL6 },
    { "local7", LOG_LOCAL7 },
    { NULL, -1 }
};
#endif

static regex_t  Empty, Comment, User, Group, RootJail, Daemon, LogThreads, LogRedirects, LogFacility, LogLevel, Alive, SSLEngine, Control;
static regex_t  ListenHTTP, ListenHTTPS, End, Address, Port, Cert, HostCert, LogSNI, xHTTP, Client, CheckURL, DefaultHost;
static regex_t  Err414, Err500, Err501, Err503, ErrNoSsl, NoSslRedirect, MaxRequest, HeadRemove, RewriteLocation, RewriteDestination;
static regex_t  Service, ServiceName, URL, HeadRequire, HeadDeny, BackEnd, Emergency, Priority, HAport, HAportAddr;
static regex_t  Redirect, TimeOut, Session, Type, TTL, DeathTTL, ID, DynScale;
static regex_t  ClientCert, AddHeader, SSLAllowClientRenegotiation, SSLHonorCipherOrder, Ciphers, CAlist, VerifyList, CRLlist, NoHTTPS11;
static regex_t  ForceHTTP10, SSLUncleanShutdown, IPFreebind, IPTransparent;
static regex_t  Grace, Include, IncludeDir, ConnTO, IgnoreCase, HTTPS, HTTPSCert;
static regex_t  Enabled;

static regex_t  AuthTypeBasic, AuthTypeColdfusion, AuthTypeCFAuthToken;
static regex_t  LBInfoHeader, EndSessionHeader;

static regex_t  InitScript;

static regex_t  ControlGroup, ControlUser, ControlMode;

static regex_t  BackendKey, BackendCookie;

static regmatch_t   matches[5];

static char *xhttp[] = {
    "^(GET|POST|HEAD) ([^ ]+) HTTP/1.[01]$",
    "^(GET|POST|HEAD|PUT|DELETE) ([^ ]+) HTTP/1.[01]$",
    "^(GET|POST|HEAD|PUT|DELETE|LOCK|UNLOCK|PROPFIND|PROPPATCH|SEARCH|MKCOL|MOVE|COPY|OPTIONS|TRACE|MKACTIVITY|CHECKOUT|MERGE|REPORT) ([^ ]+) HTTP/1.[01]$",
    "^(GET|POST|HEAD|PUT|DELETE|LOCK|UNLOCK|PROPFIND|PROPPATCH|SEARCH|MKCOL|MOVE|COPY|OPTIONS|TRACE|MKACTIVITY|CHECKOUT|MERGE|REPORT|SUBSCRIBE|UNSUBSCRIBE|BPROPPATCH|POLL|BMOVE|BCOPY|BDELETE|BPROPFIND|NOTIFY|CONNECT) ([^ ]+) HTTP/1.[01]$",
    "^(GET|POST|HEAD|PUT|DELETE|LOCK|UNLOCK|PROPFIND|PROPPATCH|SEARCH|MKCOL|MOVE|COPY|OPTIONS|TRACE|MKACTIVITY|CHECKOUT|MERGE|REPORT|SUBSCRIBE|UNSUBSCRIBE|BPROPPATCH|POLL|BMOVE|BCOPY|BDELETE|BPROPFIND|NOTIFY|CONNECT|RPC_IN_DATA|RPC_OUT_DATA) ([^ ]+) HTTP/1.[01]$",
};

static int  log_level = 1;
static int  def_facility = LOG_DAEMON;
static int  clnt_to = 10;
static int  be_to = 15;
static int  be_connto = 15;
static int  dynscale = 0;
static int  ignore_case = 0;

#define MAX_FIN 100

static FILE *f_in[MAX_FIN];
static char *f_name[MAX_FIN];
static int  n_lin[MAX_FIN];
static int  cur_fin;

static int
conf_init(const char *name)
{
    if((f_name[0] = strdup(name)) == NULL) {
        logmsg(LOG_ERR, "open %s: out of memory", name);
        exit(1);
    }
    if((f_in[0] = fopen(name, "rt")) == NULL) {
        logmsg(LOG_ERR, "can't open open %s", name);
        exit(1);
    }
    n_lin[0] = 0;
    cur_fin = 0;
    return 0;
}

void
conf_err(const char *msg)
{
    logmsg(LOG_ERR, "%s line %d: %s", f_name[cur_fin], n_lin[cur_fin], msg);
    exit(1);
}

static void include_dir(const char *conf_path) {
    DIR * dp;
    struct dirent *de;

    char buf[512];
    char *files[200], *cp;
    int filecnt = 0;
    int idx,use;

    logmsg(LOG_DEBUG, "Including Dir %s", conf_path);

    if((dp = opendir(conf_path)) == NULL) {
        conf_err("can't open IncludeDir directory");
        exit(1);
    }

    while((de = readdir(dp))!=NULL) {
        if (de->d_name[0] == '.') continue;
        if ( (strlen(de->d_name) >= 5 && !strncmp(de->d_name + strlen(de->d_name) - 4, ".cfg", 4)) ||
             (strlen(de->d_name) >= 6 && !strncmp(de->d_name + strlen(de->d_name) - 5, ".conf", 5))
           ){
            snprintf(buf, sizeof(buf), "%s%s%s", conf_path, (conf_path[strlen(conf_path)-1]=='/')?"":"/", de->d_name);
            buf[sizeof(buf)-1] = 0;
            if (filecnt == sizeof(files)/sizeof(*files)) {
                conf_err("Max config files per directory reached");
            }
            if ((files[filecnt++] = strdup(buf)) == NULL) {
                conf_err("IncludeDir out of memory");
            }
            continue;
        }
    }
    /* We order the list, and include in reverse order, because include_file adds to the top of the list */
    while(filecnt) {
        use = 0;
        for(idx = 1; idx<filecnt; idx++)
            if (strcmp(files[use], files[idx])<0)
                use=idx;

        logmsg(LOG_DEBUG, " I==> %s", files[use]);

        // Copied from Include logic
        if(cur_fin == (MAX_FIN - 1))
            conf_err("Include nesting too deep");
        cur_fin++;
        f_name[cur_fin] = files[use];
        if((f_in[cur_fin] = fopen(files[use], "rt")) == NULL) {
            logmsg(LOG_ERR, "%s line %d: Can't open included file %s", f_name[cur_fin], n_lin[cur_fin], files[use]);
            exit(1);
        }
        n_lin[cur_fin] = 0;
        files[use] = files[--filecnt];
    }

    closedir(dp);
}

static char *
conf_fgets(char *buf, const int max)
{
    int i;

    for(;;) {
        if(fgets(buf, max, f_in[cur_fin]) == NULL) {
            fclose(f_in[cur_fin]);
            free(f_name[cur_fin]);
            if(cur_fin > 0) {
                cur_fin--;
                continue;
            } else
                return NULL;
        }
        n_lin[cur_fin]++;
        for(i = 0; i < max; i++)
            if(buf[i] == '\n' || buf[i] == '\r') {
                buf[i] = '\0';
                break;
            }
        if(!regexec(&Empty, buf, 4, matches, 0) || !regexec(&Comment, buf, 4, matches, 0))
            /* comment or empty line */
            continue;
        if(!regexec(&Include, buf, 4, matches, 0)) {
            buf[matches[1].rm_eo] = '\0';
            if(cur_fin == (MAX_FIN - 1))
                conf_err("Include nesting too deep");
            cur_fin++;
            if((f_name[cur_fin] = strdup(&buf[matches[1].rm_so])) == NULL)
                conf_err("Include out of memory");
            if((f_in[cur_fin] = fopen(&buf[matches[1].rm_so], "rt")) == NULL)
                conf_err("can't open included file");
            n_lin[cur_fin] = 0;
            continue;
        }
        if(!regexec(&IncludeDir, buf, 4, matches, 0)) {
            buf[matches[1].rm_eo] = '\0';
            include_dir(buf + matches[1].rm_so);
            continue;
        }
        return buf;
    }
}

/*
 * parse a back-end
 */
static BACKEND *
parse_be(const int is_emergency)
{
    char        lin[MAXBUF];
    char        *cp;
    BACKEND     *res;
    int         has_addr, has_port;
    struct hostent      *host;
    struct sockaddr_in  in;
    struct sockaddr_in6 in6;

    if((res = (BACKEND *)malloc(sizeof(BACKEND))) == NULL)
        conf_err("BackEnd config: out of memory - aborted");
    memset(res, 0, sizeof(BACKEND));
    res->be_type = 0;
    res->addr.ai_socktype = SOCK_STREAM;
    res->to = is_emergency? 120: be_to;
    res->conn_to = is_emergency? 120: be_connto;
    res->alive = 1;
    memset(&res->addr, 0, sizeof(res->addr));
    res->priority = 5;
    memset(&res->ha_addr, 0, sizeof(res->ha_addr));
    res->url = NULL;
    res->bekey = NULL;
    res->next = NULL;
    res->ctx = NULL;
    has_addr = has_port = 0;
    pthread_mutex_init(&res->mut, NULL);
    while(conf_fgets(lin, MAXBUF)) {
        if(strlen(lin) > 0 && lin[strlen(lin) - 1] == '\n')
            lin[strlen(lin) - 1] = '\0';
        if(!regexec(&Address, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if(get_host(lin + matches[1].rm_so, &res->addr)) {
                /* if we can't resolve it assume this is a UNIX domain socket */
                res->addr.ai_socktype = SOCK_STREAM;
                res->addr.ai_family = AF_UNIX;
                res->addr.ai_protocol = 0;
                if((res->addr.ai_addr = (struct sockaddr *)malloc(sizeof(struct sockaddr_un))) == NULL)
                    conf_err("out of memory");
                if((strlen(lin + matches[1].rm_so) + 1) > UNIX_PATH_MAX)
                    conf_err("UNIX path name too long");
                res->addr.ai_addrlen = strlen(lin + matches[1].rm_so) + 1;
                res->addr.ai_addr->sa_family = AF_UNIX;
                strcpy(res->addr.ai_addr->sa_data, lin + matches[1].rm_so);
                res->addr.ai_addrlen = sizeof( struct sockaddr_un );
            }
            has_addr = 1;
        } else if(!regexec(&Port, lin, 4, matches, 0)) {
            switch(res->addr.ai_family) {
            case AF_INET:
                memcpy(&in, res->addr.ai_addr, sizeof(in));
                in.sin_port = (in_port_t)htons(atoi(lin + matches[1].rm_so));
                memcpy(res->addr.ai_addr, &in, sizeof(in));
                break;
            case AF_INET6:
                memcpy(&in6, res->addr.ai_addr, sizeof(in6));
                in6.sin6_port = (in_port_t)htons(atoi(lin + matches[1].rm_so));
                memcpy(res->addr.ai_addr, &in6, sizeof(in6));
                break;
            default:
                conf_err("Port is supported only for INET/INET6 back-ends");
            }
            has_port = 1;
        } else if(!regexec(&BackendKey, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if ((res->bekey = strdup(lin + matches[1].rm_so))==NULL)
                conf_err("out of memory");
        } else if(!regexec(&Priority, lin, 4, matches, 0)) {
            if(is_emergency)
                conf_err("Priority is not supported for Emergency back-ends");
            res->priority = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&Enabled, lin, 4, matches, 0)) {
            if(is_emergency)
                conf_err("Enabled is not supported for Emergency back-ends");
            res->disabled = 1-atoi(lin + matches[1].rm_so);
        } else if(!regexec(&TimeOut, lin, 4, matches, 0)) {
            res->to = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&ConnTO, lin, 4, matches, 0)) {
            res->conn_to = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&HAport, lin, 4, matches, 0)) {
            if(is_emergency)
                conf_err("HAport is not supported for Emergency back-ends");
            res->ha_addr = res->addr;
            if((res->ha_addr.ai_addr = (struct sockaddr *)malloc(res->addr.ai_addrlen)) == NULL)
                conf_err("out of memory");
            memcpy(res->ha_addr.ai_addr, res->addr.ai_addr, res->addr.ai_addrlen);
            switch(res->addr.ai_family) {
            case AF_INET:
                memcpy(&in, res->ha_addr.ai_addr, sizeof(in));
                in.sin_port = (in_port_t)htons(atoi(lin + matches[1].rm_so));
                memcpy(res->ha_addr.ai_addr, &in, sizeof(in));
                break;
            case AF_INET6:
                memcpy(&in6, res->addr.ai_addr, sizeof(in6));
                in6.sin6_port = (in_port_t)htons(atoi(lin + matches[1].rm_so));
                memcpy(res->addr.ai_addr, &in6, sizeof(in6));
                break;
            default:
                conf_err("HAport is supported only for INET/INET6 back-ends");
            }
        } else if(!regexec(&HAportAddr, lin, 4, matches, 0)) {
            if(is_emergency)
                conf_err("HAportAddr is not supported for Emergency back-ends");
            lin[matches[1].rm_eo] = '\0';
            if(get_host(lin + matches[1].rm_so, &res->ha_addr)) {
                /* if we can't resolve it assume this is a UNIX domain socket */
                res->addr.ai_socktype = SOCK_STREAM;
                res->ha_addr.ai_family = AF_UNIX;
                res->ha_addr.ai_protocol = 0;
                if((res->ha_addr.ai_addr = (struct sockaddr *)strdup(lin + matches[1].rm_so)) == NULL)
                    conf_err("out of memory");
                res->addr.ai_addrlen = strlen(lin + matches[1].rm_so) + 1;
            } else switch(res->ha_addr.ai_family) {
            case AF_INET:
                memcpy(&in, res->ha_addr.ai_addr, sizeof(in));
                in.sin_port = (in_port_t)htons(atoi(lin + matches[2].rm_so));
                memcpy(res->ha_addr.ai_addr, &in, sizeof(in));
                break;
            case AF_INET6:
                memcpy(&in6, res->ha_addr.ai_addr, sizeof(in6));
                in6.sin6_port = (in_port_t)htons(atoi(lin + matches[2].rm_so));
                memcpy(res->ha_addr.ai_addr, &in6, sizeof(in6));
                break;
            default:
                conf_err("Unknown HA address type");
            }
        } else if(!regexec(&HTTPS, lin, 4, matches, 0)) {
            if((res->ctx = SSL_CTX_new(SSLv23_client_method())) == NULL)
                conf_err("SSL_CTX_new failed - aborted");
	    SSL_CTX_set_app_data(res->ctx, res);
            SSL_CTX_set_verify(res->ctx, SSL_VERIFY_NONE, NULL);
            SSL_CTX_set_mode(res->ctx, SSL_MODE_AUTO_RETRY);
            SSL_CTX_set_options(res->ctx, SSL_OP_ALL);
#ifdef SSL_OP_NO_COMPRESSION
            SSL_CTX_set_options(res->ctx, SSL_OP_NO_COMPRESSION);
#endif
            SSL_CTX_clear_options(res->ctx, SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);
            SSL_CTX_clear_options(res->ctx, SSL_OP_LEGACY_SERVER_CONNECT);
            sprintf(lin, "%d-Pound-%ld", getpid(), random());
            SSL_CTX_set_session_id_context(res->ctx, (unsigned char *)lin, strlen(lin));
            SSL_CTX_set_tmp_rsa_callback(res->ctx, RSA_tmp_callback);
            SSL_CTX_set_tmp_dh_callback(res->ctx, DH_tmp_callback);
        } else if(!regexec(&HTTPSCert, lin, 4, matches, 0)) {
            if((res->ctx = SSL_CTX_new(SSLv23_client_method())) == NULL)
                conf_err("SSL_CTX_new failed - aborted");
	    SSL_CTX_set_app_data(res->ctx, res);
            lin[matches[1].rm_eo] = '\0';
            if(SSL_CTX_use_certificate_chain_file(res->ctx, lin + matches[1].rm_so) != 1)
                conf_err("SSL_CTX_use_certificate_chain_file failed - aborted");
            if(SSL_CTX_use_PrivateKey_file(res->ctx, lin + matches[1].rm_so, SSL_FILETYPE_PEM) != 1)
                conf_err("SSL_CTX_use_PrivateKey_file failed - aborted");
            if(SSL_CTX_check_private_key(res->ctx) != 1)
                conf_err("SSL_CTX_check_private_key failed - aborted");
            SSL_CTX_set_verify(res->ctx, SSL_VERIFY_NONE, NULL);
            SSL_CTX_set_mode(res->ctx, SSL_MODE_AUTO_RETRY);
            SSL_CTX_set_options(res->ctx, SSL_OP_ALL);
#ifdef SSL_OP_NO_COMPRESSION
            SSL_CTX_set_options(res->ctx, SSL_OP_NO_COMPRESSION);
#endif
            SSL_CTX_clear_options(res->ctx, SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);
            SSL_CTX_clear_options(res->ctx, SSL_OP_LEGACY_SERVER_CONNECT);
            sprintf(lin, "%d-Pound-%ld", getpid(), random());
            SSL_CTX_set_session_id_context(res->ctx, (unsigned char *)lin, strlen(lin));
            SSL_CTX_set_tmp_rsa_callback(res->ctx, RSA_tmp_callback);
            SSL_CTX_set_tmp_dh_callback(res->ctx, DH_tmp_callback);
        } else if(!regexec(&End, lin, 4, matches, 0)) {
            if(!has_addr)
                conf_err("BackEnd missing Address - aborted");
            if((res->addr.ai_family == AF_INET || res->addr.ai_family == AF_INET6) && !has_port)
                conf_err("BackEnd missing Port - aborted");
            if(!res->priority)
                return NULL;
            if(!res->bekey) {
                if (res->addr.ai_family == AF_INET)
                    snprintf(lin, MAXBUF-1, "4-%08x-%x",htonl(((struct sockaddr_in *)(res->addr.ai_addr))->sin_addr.s_addr), htons(((struct sockaddr_in *)(res->addr.ai_addr))->sin_port));
                else if (res->addr.ai_family == AF_INET6) {
                    cp = (char*) &(((struct sockaddr_in6 *)(res->addr.ai_addr))->sin6_addr);
                    snprintf(lin, MAXBUF-1, "6-%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x-%x",cp[0],cp[1],cp[2],cp[3],cp[4],cp[5],cp[6],cp[7],cp[8],cp[9],cp[10],cp[11],cp[12],cp[13],cp[14],cp[15],htons(((struct sockaddr_in6 *)(res->addr.ai_addr))->sin6_port));
                } else
                    conf_err("cannot autogenerate backendkey, please specify one");

                if ((res->bekey = strdup(lin))==NULL)
                    conf_err("out of memory autogenerating backendkey");
            }
            printf("Key %s\n",res->bekey);
            return res;
        } else {
            conf_err("unknown directive");
        }
    }

    conf_err("BackEnd premature EOF");
    return NULL;
}

/*
 * parse a session
 */
static void
parse_sess(SERVICE *const svc)
{
    char        lin[MAXBUF], *cp, *parm;

    parm = NULL;
    while(conf_fgets(lin, MAXBUF)) {
        if(strlen(lin) > 0 && lin[strlen(lin) - 1] == '\n')
            lin[strlen(lin) - 1] = '\0';
        if(!regexec(&Type, lin, 4, matches, 0)) {
            if(svc->sess_type != SESS_NONE)
                conf_err("Multiple Session types in one Service - aborted");
            lin[matches[1].rm_eo] = '\0';
            cp = lin + matches[1].rm_so;
            if(!strcasecmp(cp, "IP"))
                svc->sess_type = SESS_IP;
            else if(!strcasecmp(cp, "COOKIE"))
                svc->sess_type = SESS_COOKIE;
            else if(!strcasecmp(cp, "URL"))
                svc->sess_type = SESS_URL;
            else if(!strcasecmp(cp, "PARM"))
                svc->sess_type = SESS_PARM;
            else if(!strcasecmp(cp, "BASIC"))
                svc->sess_type = SESS_BASIC;
            else if(!strcasecmp(cp, "HEADER"))
                svc->sess_type = SESS_HEADER;
            else
                conf_err("Unknown Session type");
        } else if(!regexec(&TTL, lin, 4, matches, 0)) {
            svc->sess_ttl = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&DeathTTL, lin, 4, matches, 0)) {
            svc->death_ttl = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&EndSessionHeader, lin, 4, matches, 0)) {
            if(svc->sess_end_hdr>0)
                conf_err("Can only have one EndSessionHeader per session type");
            lin[matches[1].rm_eo] = '\0';
            if(regcomp(&svc->sess_end, lin+matches[1].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("EndSessionHeader pattern failed - aborted");
            svc->sess_end_hdr++;
        } else if(!regexec(&ID, lin, 4, matches, 0)) {
            if(svc->sess_type != SESS_COOKIE && svc->sess_type != SESS_URL && svc->sess_type != SESS_HEADER)
                conf_err("no ID permitted unless COOKIE/URL/HEADER Session - aborted");
            lin[matches[1].rm_eo] = '\0';
            if((parm = strdup(lin + matches[1].rm_so)) == NULL)
                conf_err("ID config: out of memory - aborted");
        } else if(!regexec(&End, lin, 4, matches, 0)) {
            if(svc->sess_type == SESS_NONE)
                conf_err("Session type not defined - aborted");
            if(svc->sess_ttl == 0)
                conf_err("Session TTL not defined - aborted");
            if((svc->sess_type == SESS_COOKIE || svc->sess_type == SESS_URL || svc->sess_type == SESS_HEADER)
            && parm == NULL)
                conf_err("Session ID not defined - aborted");
            if(svc->sess_type == SESS_COOKIE) {
                snprintf(lin, MAXBUF - 1, "Cookie[^:]*:.*[ \t]%s=", parm);
                if(regcomp(&svc->sess_start, lin, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("COOKIE pattern failed - aborted");
                if(regcomp(&svc->sess_pat, "([^;]*)", REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("COOKIE pattern failed - aborted");
            } else if(svc->sess_type == SESS_URL) {
                snprintf(lin, MAXBUF - 1, "[?&]%s=", parm);
                if(regcomp(&svc->sess_start, lin, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("URL pattern failed - aborted");
                if(regcomp(&svc->sess_pat, "([^&;#]*)", REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("URL pattern failed - aborted");
            } else if(svc->sess_type == SESS_PARM) {
                if(regcomp(&svc->sess_start, ";", REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("PARM pattern failed - aborted");
                if(regcomp(&svc->sess_pat, "([^?]*)", REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("PARM pattern failed - aborted");
            } else if(svc->sess_type == SESS_BASIC) {
                if(regcomp(&svc->sess_start, "Authorization:[ \t]*Basic[ \t]*", REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("BASIC pattern failed - aborted");
                if(regcomp(&svc->sess_pat, "([^ \t]*)", REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("BASIC pattern failed - aborted");
            } else if(svc->sess_type == SESS_HEADER) {
                snprintf(lin, MAXBUF - 1, "%s:[ \t]*", parm);
                if(regcomp(&svc->sess_start, lin, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("HEADER pattern failed - aborted");
                if(regcomp(&svc->sess_pat, "([^ \t]*)", REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("HEADER pattern failed - aborted");
            }
            if(parm != NULL)
                free(parm);
            return;
        } else {
            conf_err("unknown directive");
        }
    }

    conf_err("Session premature EOF");
    return;
}

/*
 * basic hashing function, based on fmv
 */
static unsigned long
t_hash(const TABNODE *e)
{
    unsigned long   res;
    char            *k;

    k = e->key;
    res = 2166136261;
    while(*k)
        res = (res ^ *k++) * 16777619;
    return res;
}
#if OPENSSL_VERSION_NUMBER >= 0x10000000L
static IMPLEMENT_LHASH_HASH_FN(t, TABNODE)
#else
static IMPLEMENT_LHASH_HASH_FN(t_hash, const TABNODE *)
#endif

static int
t_cmp(const TABNODE *d1, const TABNODE *d2)
{
    return strcmp(d1->key, d2->key);
}
#if OPENSSL_VERSION_NUMBER >= 0x10000000L
static IMPLEMENT_LHASH_COMP_FN(t, TABNODE)
#else
static IMPLEMENT_LHASH_COMP_FN(t_cmp, const TABNODE *)
#endif

/*
 * parse a service
 */
static SERVICE *
parse_service(const char *svc_name, int global)
{
    char        lin[MAXBUF];
    char        pat[MAXBUF];
    SERVICE     *res;
    BACKEND     *be;
    MATCHER     *m;
    int         ign_case;

    if((res = (SERVICE *)malloc(sizeof(SERVICE))) == NULL)
        conf_err("Service config: out of memory - aborted");
    memset(res, 0, sizeof(SERVICE));
    res->sess_type = SESS_NONE;
    res->dynscale = dynscale;
    res->global = global;
    res->requests = 0;
    res->hits = 0;
    res->misses = 0;
    res->user_type = UserBasic;
    pthread_mutex_init(&res->mut, NULL);
    if(svc_name)
        strncpy(res->name, svc_name, KEY_SIZE);
#if OPENSSL_VERSION_NUMBER >= 0x10000000L
    if((res->sessions = LHM_lh_new(TABNODE, t)) == NULL)
#else
    if((res->sessions = lh_new(LHASH_HASH_FN(t_hash), LHASH_COMP_FN(t_cmp))) == NULL)
#endif
        conf_err("lh_new failed - aborted");
    res->del_sessions = NULL;
    res->becookie = res->becdomain = res->becpath = NULL;
    res->becage = 0;
    ign_case = ignore_case;
    while(conf_fgets(lin, MAXBUF)) {
        if(strlen(lin) > 0 && lin[strlen(lin) - 1] == '\n')
            lin[strlen(lin) - 1] = '\0';
        if(!regexec(&URL, lin, 4, matches, 0)) {
            if(res->url) {
                for(m = res->url; m->next; m = m->next)
                    ;
                if((m->next = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("URL config: out of memory - aborted");
                m = m->next;
            } else {
                if((res->url = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("URL config: out of memory - aborted");
                m = res->url;
            }
            memset(m, 0, sizeof(MATCHER));
            lin[matches[2].rm_eo] = '\0';
            if(regcomp(&m->pat, lin + matches[2].rm_so, REG_NEWLINE | REG_EXTENDED | (ign_case || (matches[1].rm_eo-matches[1].rm_so)>0 ? REG_ICASE: 0)))
                conf_err("URL bad pattern - aborted");
        } else if(!regexec(&HeadRequire, lin, 4, matches, 0)) {
            if(res->req_head) {
                for(m = res->req_head; m->next; m = m->next)
                    ;
                if((m->next = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("HeadRequire config: out of memory - aborted");
                m = m->next;
            } else {
                if((res->req_head = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("HeadRequire config: out of memory - aborted");
                m = res->req_head;
            }
            memset(m, 0, sizeof(MATCHER));
            lin[matches[1].rm_eo] = '\0';
            if(regcomp(&m->pat, lin + matches[1].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("HeadRequire bad pattern - aborted");
        } else if(!regexec(&HeadDeny, lin, 4, matches, 0)) {
            if(res->deny_head) {
                for(m = res->deny_head; m->next; m = m->next)
                    ;
                if((m->next = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("HeadDeny config: out of memory - aborted");
                m = m->next;
            } else {
                if((res->deny_head = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("HeadDeny config: out of memory - aborted");
                m = res->deny_head;
            }
            memset(m, 0, sizeof(MATCHER));
            lin[matches[1].rm_eo] = '\0';
            if(regcomp(&m->pat, lin + matches[1].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("HeadDeny bad pattern - aborted");
        } else if(!regexec(&Redirect, lin, 4, matches, 0)) {
            if(res->backends) {
                for(be = res->backends; be->next; be = be->next)
                    ;
                if((be->next = (BACKEND *)malloc(sizeof(BACKEND))) == NULL)
                    conf_err("Redirect config: out of memory - aborted");
                be = be->next;
            } else {
                if((res->backends = (BACKEND *)malloc(sizeof(BACKEND))) == NULL)
                    conf_err("Redirect config: out of memory - aborted");
                be = res->backends;
            }
            // 1 - Dynamic or not, 2 - Request Redirect #, 3 - Destination URL
            memset(be, 0, sizeof(BACKEND));
            be->be_type = 302;
            be->redir_req = 0;
            if (matches[1].rm_eo != matches[1].rm_so) {
                if((lin[matches[1].rm_so] & ~0x20)=='D') {
                    be->redir_req = 2;
                    if(!res->url || res->url->next)
                        conf_err("Dynamic Redirect must be preceeded by a URL line");
                } else if((lin[matches[1].rm_so] & ~0x20)=='A')
                    be->redir_req = 1;
            }
            if (matches[2].rm_eo != matches[2].rm_so)
                be->be_type = atoi(lin + matches[2].rm_so);
            be->priority = 1;
            be->alive = 1;
            be->bekey = NULL;
            pthread_mutex_init(& be->mut, NULL);
            lin[matches[3].rm_eo] = '\0';
            if((be->url = strdup(lin + matches[3].rm_so)) == NULL)
                conf_err("Redirector config: out of memory - aborted");
            /* split the URL into its fields */
            if(regexec(&LOCATION, be->url, 4, matches, 0))
                conf_err("Redirect bad URL - aborted");
            if((matches[3].rm_eo - matches[3].rm_so) == 1)
                /* the path is a single '/', so remove it */
                be->url[matches[3].rm_so] = '\0';
        } else if(!regexec(&BackEnd, lin, 4, matches, 0)) {
            if(res->backends) {
                for(be = res->backends; be->next; be = be->next)
                    ;
                be->next = parse_be(0);
            } else
                res->backends = parse_be(0);
        } else if(!regexec(&Emergency, lin, 4, matches, 0)) {
            res->emergency = parse_be(1);
        } else if(!regexec(&AuthTypeBasic, lin, 4, matches, 0)) {
            if (res->user_type!=UserBasic)
                conf_err("Multiple authtypes defined");

            // This is the default and the regexp is compiled in the End case
            res->user_type = UserBasic;
        } else if(!regexec(&BackendCookie, lin, 5, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            lin[matches[2].rm_eo] = '\0';
            lin[matches[3].rm_eo] = '\0';
            lin[matches[4].rm_eo] = '\0';
            snprintf(pat, MAXBUF - 1, "Cookie[^:]*:.*[; \t]%s=\"?([^\";]*)\"?", lin + matches[1].rm_so);
            if(matches[1].rm_so==matches[1].rm_eo)
                conf_err("Backend cookie must have a name");
            if((res->becookie=strdup(lin+matches[1].rm_so))==NULL)
                conf_err("out of memory");
            if(regcomp(&res->becookie_match, pat, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("AuthType Coldfusion pattern failed - aborted");
            if(matches[2].rm_so!=matches[2].rm_eo && (res->becdomain=strdup(lin+matches[2].rm_so))==NULL)
                conf_err("out of memory");
            if(matches[3].rm_so!=matches[3].rm_eo && (res->becpath=strdup(lin+matches[3].rm_so))==NULL)
                conf_err("out of memory");
            if ((lin[matches[4].rm_so]&~0x20)=='S')
                res->becage = -1;
            else
                res->becage = atoi(lin+matches[4].rm_so);
        } else if(!regexec(&AuthTypeColdfusion, lin, 4, matches, 0)) {
            if (res->user_type!=UserBasic)
                conf_err("Multiple authtypes defined");

            lin[matches[1].rm_eo] = '\0';
            snprintf(pat, MAXBUF - 1, "Cookie[^:]*:.*[; \t]CFAUTHORIZATION_%s=\"?([^\";]*)\"?", lin + matches[1].rm_so);
            if(regcomp(&res->auth_pat, pat, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("AuthType Coldfusion pattern failed - aborted");
            res->user_type = UserCFAUTH;
        } else if(!regexec(&AuthTypeCFAuthToken, lin, 4, matches, 0)) {
            if (res->user_type!=UserBasic)
                conf_err("Multiple authtypes defined");

            lin[matches[2].rm_eo] = '\0';
            snprintf(pat, MAXBUF - 1, "Cookie[^:]*:.*[ \t]%s=\"?([^\";]*)\"?", lin + matches[2].rm_so);
            if (regcomp(&res->auth_pat, pat, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("setting AuthType Token");
            res->user_type = UserCFAUTHToken;
        } else if(!regexec(&LBInfoHeader, lin, 4, matches, 0)) {
            if(res->lbinfo) {
                for(m = res->lbinfo; m->next; m = m->next)
                    ;
                if((m->next = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("LBInfoHeader config: out of memory - aborted");
                m = m->next;
            } else {
                if((res->lbinfo = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("LBInfoHeader config: out of memory - aborted");
                m = res->lbinfo;
		m->next = NULL;
            }
            memset(m, 0, sizeof(MATCHER));
            lin[matches[1].rm_eo] = '\0';
            snprintf(pat, MAXBUF - 1, "%s:[ \t]*([^ \t]*)", lin + matches[1].rm_so);
            if(regcomp(&m->pat, pat, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("LBInfoHeader pattern failed - aborted");
        } else if(!regexec(&Session, lin, 4, matches, 0)) {
            parse_sess(res);
        } else if(!regexec(&Enabled, lin, 4, matches, 0)) {
            res->disabled = 1-atoi(lin + matches[1].rm_so);
        } else if(!regexec(&End, lin, 4, matches, 0)) {
            for(be = res->backends; be; be = be->next) {
                res->abs_pri += be->priority;
                if (be->alive && !be->disabled)
                    res->tot_pri += be->priority;
            }
            if (res->user_type == UserBasic)
                if(regcomp(&res->auth_pat, "Authorization:[ \t]*Basic[ \t]*([^ \t]*)", REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                    conf_err("Auth BASIC pattern failed - aborted");
            return res;
        } else if(!regexec(&DynScale, lin, 4, matches, 0)) {
            res->dynscale = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&IgnoreCase, lin, 4, matches, 0)) {
            ign_case = atoi(lin + matches[1].rm_so);
        } else {
            conf_err("unknown directive");
        }
    }

    conf_err("Service premature EOF");
    return NULL;
}

/*
 * return the file contents as a string
 */
static char *
file2str(const char *fname)
{
    char    *res;
    struct stat st;
    int     fin;

    if(stat(fname, &st))
        conf_err("can't stat Err file - aborted");
    if((fin = open(fname, O_RDONLY)) < 0)
        conf_err("can't open Err file - aborted");
    if((res = malloc(st.st_size + 1)) == NULL)
        conf_err("can't alloc Err file (out of memory) - aborted");
    if(read(fin, res, st.st_size) != st.st_size)
        conf_err("can't read Err file - aborted");
    res[st.st_size] = '\0';
    close(fin);
    return res;
}

/*
 * parse an HTTP listener
 */
static LISTENER *
parse_HTTP(void)
{
    char        lin[MAXBUF];
    LISTENER    *res;
    SERVICE     *svc;
    MATCHER     *m;
    int         has_addr, has_port;
    int         ign_case;
    struct sockaddr_in  in;
    struct sockaddr_in6 in6;

    if((res = (LISTENER *)malloc(sizeof(LISTENER))) == NULL)
        conf_err("ListenHTTP config: out of memory - aborted");
    memset(res, 0, sizeof(LISTENER));
    res->to = clnt_to;
    res->def_host = NULL;
    res->rewr_loc = 1;
    res->sni = NULL;
    res->err414 = "Request URI is too long";
    res->err500 = "An internal server error occurred. Please try again later.";
    res->err501 = "This method may not be used.";
    res->err503 = "The service is not available. Please try again later.";
    res->errnossl= "Please use HTTPS.";
    res->nossl_url = NULL;
    res->nossl_redir = 0;
    res->log_level = log_level;
    if(regcomp(&res->verb, xhttp[0], REG_ICASE | REG_NEWLINE | REG_EXTENDED))
        conf_err("xHTTP bad default pattern - aborted");
    has_addr = has_port = 0;
    ign_case = ignore_case;
    while(conf_fgets(lin, MAXBUF)) {
        if(strlen(lin) > 0 && lin[strlen(lin) - 1] == '\n')
            lin[strlen(lin) - 1] = '\0';
        if(!regexec(&Address, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if(get_host(lin + matches[1].rm_so, &res->addr))
                conf_err("Unknown Listener address");
            if(res->addr.ai_family != AF_INET && res->addr.ai_family != AF_INET6)
                conf_err("Unknown Listener address family");
            has_addr = 1;
        } else if(!regexec(&Port, lin, 4, matches, 0)) {
            switch(res->addr.ai_family) {
            case AF_INET:
                memcpy(&in, res->addr.ai_addr, sizeof(in));
                in.sin_port = (in_port_t)htons(atoi(lin + matches[1].rm_so));
                memcpy(res->addr.ai_addr, &in, sizeof(in));
                break;
            case AF_INET6:
                memcpy(&in6, res->addr.ai_addr, sizeof(in6));
                in6.sin6_port = htons(atoi(lin + matches[1].rm_so));
                memcpy(res->addr.ai_addr, &in6, sizeof(in6));
                break;
            default:
                conf_err("Unknown Listener address family");
            }
            has_port = 1;
        } else if(!regexec(&DefaultHost, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if ((res->def_host = strdup(lin + matches[1].rm_so))==NULL)
                conf_err("Out of memory");
        } else if(!regexec(&xHTTP, lin, 4, matches, 0)) {
            int n;

            n = atoi(lin + matches[1].rm_so);
            regfree(&res->verb);
            if(regcomp(&res->verb, xhttp[n], REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("xHTTP bad pattern - aborted");
        } else if(!regexec(&Client, lin, 4, matches, 0)) {
            res->to = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&CheckURL, lin, 4, matches, 0)) {
            if(res->has_pat)
                conf_err("CheckURL multiple pattern - aborted");
            lin[matches[2].rm_eo] = '\0';
            if(regcomp(&res->url_pat, lin + matches[2].rm_so, REG_NEWLINE | REG_EXTENDED | (ign_case || (matches[1].rm_eo-matches[1].rm_so)>0 ? REG_ICASE: 0) ))
                conf_err("CheckURL bad pattern - aborted");
            res->has_pat = 1;
        } else if(!regexec(&Err414, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->err414 = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&Err500, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->err500 = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&Err501, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->err501 = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&Err503, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->err503 = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&MaxRequest, lin, 4, matches, 0)) {
            res->max_req = atol(lin + matches[1].rm_so);
        } else if(!regexec(&HeadRemove, lin, 4, matches, 0)) {
            if(res->head_off) {
                for(m = res->head_off; m->next; m = m->next)
                    ;
                if((m->next = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("HeadRemove config: out of memory - aborted");
                m = m->next;
            } else {
                if((res->head_off = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("HeadRemove config: out of memory - aborted");
                m = res->head_off;
            }
            memset(m, 0, sizeof(MATCHER));
            lin[matches[1].rm_eo] = '\0';
            if(regcomp(&m->pat, lin + matches[1].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("HeadRemove bad pattern - aborted");
        } else if(!regexec(&AddHeader, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if((res->add_head = strdup(lin + matches[1].rm_so)) == NULL)
                conf_err("AddHeader config: out of memory - aborted");
        } else if(!regexec(&RewriteLocation, lin, 4, matches, 0)) {
            res->rewr_loc = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&RewriteDestination, lin, 4, matches, 0)) {
            res->rewr_dest = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&LogLevel, lin, 4, matches, 0)) {
            res->log_level = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&IPFreebind, lin, 4, matches, 0)) {
#ifdef IP_FREEBIND
            res->freebind = atoi(lin + matches[1].rm_so);
#else
            conf_err("Compiled without IP_FREEBIND support");
#endif
        } else if(!regexec(&IPTransparent, lin, 4, matches, 0)) {
#ifdef IP_TRANSPARENT
            res->transparent = atoi(lin + matches[1].rm_so);
#else
            conf_err("Compiled without IP_TRANSPARENT support");
#endif
        } else if(!regexec(&ForceHTTP10, lin, 4, matches, 0)) {
            if((m = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                conf_err("out of memory");
            memset(m, 0, sizeof(MATCHER));
            m->next = res->forcehttp10;
            res->forcehttp10 = m;
            lin[matches[1].rm_eo] = '\0';
            if(regcomp(&m->pat, lin + matches[1].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("ForceHTTP10 bad pattern");
        } else if(!regexec(&Service, lin, 4, matches, 0)) {
            if(res->services == NULL)
                res->services = parse_service(NULL, 0);
            else {
                for(svc = res->services; svc->next; svc = svc->next)
                    ;
                svc->next = parse_service(NULL, 0);
            }
        } else if(!regexec(&ServiceName, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if(res->services == NULL)
                res->services = parse_service(lin + matches[1].rm_so, 0);
            else {
                for(svc = res->services; svc->next; svc = svc->next)
                    ;
                svc->next = parse_service(lin + matches[1].rm_so, 0);
            }
        } else if(!regexec(&End, lin, 4, matches, 0)) {
            if(!has_addr || !has_port)
                conf_err("ListenHTTP missing Address or Port - aborted");
            return res;
        } else if(!regexec(&IgnoreCase, lin, 4, matches, 0)) {
            ign_case = atoi(lin + matches[1].rm_so);
        } else {
            conf_err("unknown directive - aborted");
        }
    }

    conf_err("ListenHTTP premature EOF");
    return NULL;
}
/*
 * Dummy certificate verification - always OK
 */
static int
verify_OK(int pre_ok, X509_STORE_CTX *ctx)
{
    return 1;
}

/*
 * parse an HTTPS listener
 */
static LISTENER *
parse_HTTPS(void)
{
    char        lin[MAXBUF];
    LISTENER    *res;
    SERVICE     *svc;
    MATCHER     *m;
    SNIMATCHER  *snim;
    int         has_addr, has_port, has_cert, had_ctxspec;
    int         ign_case;
    long	ssl_op_enable, ssl_op_disable;
    struct hostent      *host;
    struct sockaddr_in  in;
    struct sockaddr_in6 in6;

    ssl_op_enable = SSL_OP_ALL;
#ifdef SSL_OP_NO_COMPRESSION
    ssl_op_enable |= SSL_OP_NO_COMPRESSION;
#endif
    ssl_op_disable = SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION | SSL_OP_LEGACY_SERVER_CONNECT | SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS;

    if((res = (LISTENER *)malloc(sizeof(LISTENER))) == NULL)
        conf_err("ListenHTTPS config: out of memory - aborted");
    memset(res, 0, sizeof(LISTENER));
    if((res->ctx = SSL_CTX_new(SSLv23_server_method())) == NULL)
        conf_err("SSL_CTX_new failed - aborted");

    res->to = clnt_to;
    res->def_host = NULL;
    res->rewr_loc = 1;
    res->sni = NULL;
    res->err414 = "Request URI is too long";
    res->err500 = "An internal server error occurred. Please try again later.";
    res->err501 = "This method may not be used.";
    res->err503 = "The service is not available. Please try again later.";
    res->errnossl = "Please use HTTPS.";
    res->nossl_url = NULL;
    res->nossl_redir = 0;
    res->allow_client_reneg = 0;
    res->log_level = log_level;
    res->freebind = 0;
    res->transparent = 0;
    had_ctxspec = 0;
    if(regcomp(&res->verb, xhttp[0], REG_ICASE | REG_NEWLINE | REG_EXTENDED))
        conf_err("xHTTP bad default pattern - aborted");
    has_addr = has_port = has_cert = 0;
    ign_case = ignore_case;
    while(conf_fgets(lin, MAXBUF)) {
        if(strlen(lin) > 0 && lin[strlen(lin) - 1] == '\n')
            lin[strlen(lin) - 1] = '\0';
        if(!regexec(&Address, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if(get_host(lin + matches[1].rm_so, &res->addr))
                conf_err("Unknown Listener address");
            if(res->addr.ai_family != AF_INET && res->addr.ai_family != AF_INET6)
                conf_err("Unknown Listener address family");
            has_addr = 1;
        } else if(!regexec(&Port, lin, 4, matches, 0)) {
            if(res->addr.ai_family == AF_INET) {
                memcpy(&in, res->addr.ai_addr, sizeof(in));
                in.sin_port = (in_port_t)htons(atoi(lin + matches[1].rm_so));
                memcpy(res->addr.ai_addr, &in, sizeof(in));
            } else {
                memcpy(&in6, res->addr.ai_addr, sizeof(in6));
                in6.sin6_port = htons(atoi(lin + matches[1].rm_so));
                memcpy(res->addr.ai_addr, &in6, sizeof(in6));
            }
            has_port = 1;
        } else if(!regexec(&DefaultHost, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if ((res->def_host = strdup(lin + matches[1].rm_so))==NULL)
                conf_err("Out of memory");
        } else if(!regexec(&xHTTP, lin, 4, matches, 0)) {
            int n;

            n = atoi(lin + matches[1].rm_so);
            regfree(&res->verb);
            if(regcomp(&res->verb, xhttp[n], REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("xHTTP bad pattern - aborted");
        } else if(!regexec(&Client, lin, 4, matches, 0)) {
            res->to = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&CheckURL, lin, 4, matches, 0)) {
            if(res->has_pat)
                conf_err("CheckURL multiple pattern - aborted");
            lin[matches[2].rm_eo] = '\0';
            if(regcomp(&res->url_pat, lin + matches[2].rm_so, REG_NEWLINE | REG_EXTENDED | (ign_case || (matches[1].rm_eo-matches[1].rm_so)>0 ? REG_ICASE: 0)))
                conf_err("CheckURL bad pattern - aborted");
            res->has_pat = 1;
        } else if(!regexec(&Err414, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->err414 = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&Err500, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->err500 = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&Err501, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->err501 = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&Err503, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->err503 = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&ErrNoSsl, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            res->errnossl = file2str(lin + matches[1].rm_so);
        } else if(!regexec(&NoSslRedirect, lin, 4, matches, 0)) {
            res->nossl_redir = 302;
            if (matches[1].rm_eo != matches[1].rm_so)
                res->nossl_redir = atoi(lin + matches[1].rm_so);
            lin[matches[2].rm_eo] = '\0';
            if((res->nossl_url = strdup(lin + matches[2].rm_so))==NULL)
                conf_err("NoSslRedirect out of memory");
            if(regexec(&LOCATION, res->nossl_url, 4, matches, 0))
                conf_err("Redirect bad URL - aborted");
            if((matches[3].rm_eo - matches[3].rm_so) == 1)
                /* the path is a single '/', so remove it */
                res->nossl_url[matches[3].rm_so] = '\0';
        } else if(!regexec(&MaxRequest, lin, 4, matches, 0)) {
            res->max_req = atol(lin + matches[1].rm_so);
        } else if(!regexec(&HeadRemove, lin, 4, matches, 0)) {
            if(res->head_off) {
                for(m = res->head_off; m->next; m = m->next)
                    ;
                if((m->next = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("HeadRemove config: out of memory - aborted");
                m = m->next;
            } else {
                if((res->head_off = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                    conf_err("HeadRemove config: out of memory - aborted");
                m = res->head_off;
            }
            memset(m, 0, sizeof(MATCHER));
            lin[matches[1].rm_eo] = '\0';
            if(regcomp(&m->pat, lin + matches[1].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("HeadRemove bad pattern - aborted");
        } else if(!regexec(&RewriteLocation, lin, 4, matches, 0)) {
            res->rewr_loc = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&RewriteDestination, lin, 4, matches, 0)) {
            res->rewr_dest = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&LogLevel, lin, 4, matches, 0)) {
            res->log_level = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&Cert, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if(SSL_CTX_use_certificate_chain_file(res->ctx, lin + matches[1].rm_so) != 1)
                conf_err("SSL_CTX_use_certificate_chain_file failed - aborted");
            if(SSL_CTX_use_PrivateKey_file(res->ctx, lin + matches[1].rm_so, SSL_FILETYPE_PEM) != 1)
                conf_err("SSL_CTX_use_PrivateKey_file failed - aborted");
            if(SSL_CTX_check_private_key(res->ctx) != 1)
                conf_err("SSL_CTX_check_private_key failed - aborted");
            has_cert = 1;
        } else if(!regexec(&HostCert, lin, 4, matches, 0)) {
            if(had_ctxspec)
                conf_err("HostCert directives must preceed any Verification, Cipher, Or SSL specific directives");
#ifndef OPENSSL_NO_TLSEXT
            if((snim = (SNIMATCHER *)malloc(sizeof(SNIMATCHER))) == NULL)
                conf_err("out of memory");
            memset(snim, 0, sizeof(SNIMATCHER));
            snim->next = res->sni;
            res->sni = snim;
            if((snim->ctx = SSL_CTX_new(SSLv23_server_method())) == NULL)
                conf_err("SSL_CTX_new failed - aborted");
	    SSL_CTX_set_app_data(snim->ctx, res);
            lin[matches[2].rm_eo] = '\0';
            if(regcomp(&snim->pat, lin + matches[2].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("bad pattern");

            lin[matches[1].rm_eo] = '\0';
            if(SSL_CTX_use_certificate_chain_file(snim->ctx, lin + matches[1].rm_so) != 1)
                conf_err("SSL_CTX_use_certificate_chain_file failed - aborted");
            if(SSL_CTX_use_PrivateKey_file(snim->ctx, lin + matches[1].rm_so, SSL_FILETYPE_PEM) != 1)
                conf_err("SSL_CTX_use_PrivateKey_file failed - aborted");
            if(SSL_CTX_check_private_key(snim->ctx) != 1)
                conf_err("SSL_CTX_check_private_key failed - aborted");
#else
            conf_err("your version of OpenSSL does not support SNI");
#endif
        } else if(!regexec(&ClientCert, lin, 4, matches, 0)) {
            had_ctxspec++;
            switch(res->clnt_check = atoi(lin + matches[1].rm_so)) {
            case 0:
                /* don't ask */
                SSL_CTX_set_verify(res->ctx, SSL_VERIFY_NONE, NULL);
                for(snim=res->sni; snim; snim=snim->next)
                    SSL_CTX_set_verify(snim->ctx, SSL_VERIFY_NONE, NULL);
                break;
            case 1:
                /* ask but OK if no client certificate */
                SSL_CTX_set_verify(res->ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, NULL);
                SSL_CTX_set_verify_depth(res->ctx, atoi(lin + matches[2].rm_so));
                for(snim=res->sni; snim; snim=snim->next) {
                    SSL_CTX_set_verify(snim->ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, NULL);
                    SSL_CTX_set_verify_depth(snim->ctx, atoi(lin + matches[2].rm_so));
                }
                break;
            case 2:
                /* ask and fail if no client certificate */
                SSL_CTX_set_verify(res->ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
                SSL_CTX_set_verify_depth(res->ctx, atoi(lin + matches[2].rm_so));
                for(snim=res->sni; snim; snim=snim->next) {
                    SSL_CTX_set_verify(snim->ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
                    SSL_CTX_set_verify_depth(snim->ctx, atoi(lin + matches[2].rm_so));
                }
                break;
            case 3:
                /* ask but do not verify client certificate */
                SSL_CTX_set_verify(res->ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, verify_OK);
                SSL_CTX_set_verify_depth(res->ctx, atoi(lin + matches[2].rm_so));
                for(snim=res->sni; snim; snim=snim->next) {
                    SSL_CTX_set_verify(snim->ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, verify_OK);
                    SSL_CTX_set_verify_depth(snim->ctx, atoi(lin + matches[2].rm_so));
                }
                break;
            }
        } else if(!regexec(&AddHeader, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if((res->add_head = strdup(lin + matches[1].rm_so)) == NULL)
                conf_err("AddHeader config: out of memory - aborted");
        } else if(!regexec(&SSLAllowClientRenegotiation, lin, 4, matches, 0)) {
            res->allow_client_reneg = atoi(lin + matches[1].rm_so);
	    if (res->allow_client_reneg == 2) {
		ssl_op_enable |= SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION;
		ssl_op_disable &= ~SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION;
	    } else {
		ssl_op_disable |= SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION;
		ssl_op_enable &= ~SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION;
	    }
        } else if(!regexec(&SSLHonorCipherOrder, lin, 4, matches, 0)) {
            if (atoi(lin + matches[1].rm_so)) {
		ssl_op_enable |= SSL_OP_CIPHER_SERVER_PREFERENCE;
		ssl_op_disable &= ~SSL_OP_CIPHER_SERVER_PREFERENCE;
	    } else {
		ssl_op_disable |= SSL_OP_CIPHER_SERVER_PREFERENCE;
		ssl_op_enable &= ~SSL_OP_CIPHER_SERVER_PREFERENCE;
	    }
        } else if(!regexec(&Ciphers, lin, 4, matches, 0)) {
            had_ctxspec++;
            lin[matches[1].rm_eo] = '\0';
            SSL_CTX_set_cipher_list(res->ctx, lin + matches[1].rm_so);
        } else if(!regexec(&CAlist, lin, 4, matches, 0)) {
            had_ctxspec++;
            STACK_OF(X509_NAME) *cert_names;

            lin[matches[1].rm_eo] = '\0';
            if((cert_names = SSL_load_client_CA_file(lin + matches[1].rm_so)) == NULL)
                conf_err("SSL_load_client_CA_file failed - aborted");
            SSL_CTX_set_client_CA_list(res->ctx, cert_names);
            for(snim=res->sni; snim; snim=snim->next)
                SSL_CTX_set_client_CA_list(snim->ctx, cert_names);
        } else if(!regexec(&VerifyList, lin, 4, matches, 0)) {
            had_ctxspec++;
            lin[matches[1].rm_eo] = '\0';
            for(snim=res->sni; snim; snim=snim->next)
                if(SSL_CTX_load_verify_locations(snim->ctx, lin + matches[1].rm_so, NULL) != 1)
                    conf_err("SSL_CTX_load_verify_locations failed - aborted");
        } else if(!regexec(&CRLlist, lin, 4, matches, 0)) {
            had_ctxspec++;
#if HAVE_X509_STORE_SET_FLAGS
            X509_STORE *store;
            X509_LOOKUP *lookup;

            lin[matches[1].rm_eo] = '\0';
            store = SSL_CTX_get_cert_store(res->ctx);
            if((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) == NULL)
                conf_err("X509_STORE_add_lookup failed - aborted");
            if(X509_load_crl_file(lookup, lin + matches[1].rm_so, X509_FILETYPE_PEM) != 1)
                conf_err("X509_load_crl_file failed - aborted");
            X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
#else
            conf_err("your version of OpenSSL does not support CRL checking");
#endif
        } else if(!regexec(&NoHTTPS11, lin, 4, matches, 0)) {
            res->noHTTPS11 = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&ForceHTTP10, lin, 4, matches, 0)) {
            if((m = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                conf_err("out of memory");
            memset(m, 0, sizeof(MATCHER));
            m->next = res->forcehttp10;
            res->forcehttp10 = m;
            lin[matches[1].rm_eo] = '\0';
            if(regcomp(&m->pat, lin + matches[1].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("bad pattern");
        } else if(!regexec(&SSLUncleanShutdown, lin, 4, matches, 0)) {
            if((m = (MATCHER *)malloc(sizeof(MATCHER))) == NULL)
                conf_err("out of memory");
            memset(m, 0, sizeof(MATCHER));
            m->next = res->ssl_unclean_shutdown;
            res->ssl_unclean_shutdown = m;
            lin[matches[1].rm_eo] = '\0';
            if(regcomp(&m->pat, lin + matches[1].rm_so, REG_ICASE | REG_NEWLINE | REG_EXTENDED))
                conf_err("bad pattern");
        } else if(!regexec(&IPFreebind, lin, 4, matches, 0)) {
#ifdef IP_FREEBIND
            res->freebind = atoi(lin + matches[1].rm_so);
#else
            conf_err("Compiled without IP_FREEBIND support");
#endif
        } else if(!regexec(&IPTransparent, lin, 4, matches, 0)) {
#ifdef IP_TRANSPARENT
            res->transparent = atoi(lin + matches[1].rm_so);
#else
            conf_err("Compiled without IP_TRANSPARENT support");
#endif
        } else if(!regexec(&Service, lin, 4, matches, 0)) {
            if(res->services == NULL)
                res->services = parse_service(NULL, 0);
            else {
                for(svc = res->services; svc->next; svc = svc->next)
                    ;
                svc->next = parse_service(NULL, 0);
            }
        } else if(!regexec(&ServiceName, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if(res->services == NULL)
                res->services = parse_service(lin + matches[1].rm_so, 0);
            else {
                for(svc = res->services; svc->next; svc = svc->next)
                    ;
                svc->next = parse_service(lin + matches[1].rm_so, 0);
            }
        } else if(!regexec(&End, lin, 4, matches, 0)) {
            X509_STORE  *store;

            if(!has_addr || !has_port || !has_cert)
                conf_err("ListenHTTPS missing Address, Port or Certificate - aborted");
	    SSL_CTX_set_app_data(res->ctx, res);
            SSL_CTX_set_mode(res->ctx, SSL_MODE_AUTO_RETRY);
            SSL_CTX_set_options(res->ctx, ssl_op_enable);
            SSL_CTX_clear_options(res->ctx, ssl_op_disable);
            sprintf(lin, "%d-Pound-%ld", getpid(), random());
            SSL_CTX_set_session_id_context(res->ctx, (unsigned char *)lin, strlen(lin));
            SSL_CTX_set_tmp_rsa_callback(res->ctx, RSA_tmp_callback);
            SSL_CTX_set_tmp_dh_callback(res->ctx, DH_tmp_callback);
            SSL_CTX_set_info_callback(res->ctx, SSLINFO_callback);
#ifndef OPENSSL_NO_TLSEXT
            if(res->sni) {
                if (!SSL_CTX_set_tlsext_servername_callback(res->ctx, SNI_servername_callback) ||
                    !SSL_CTX_set_tlsext_servername_arg(res->ctx, res))
                    conf_err("Unable to initialize SSL library for SNI feature");
		for(snim = res->sni; snim = snim->next; snim) {
	            SSL_CTX_set_mode(snim->ctx, SSL_MODE_AUTO_RETRY);
        	    SSL_CTX_set_options(snim->ctx, ssl_op_enable);
        	    SSL_CTX_clear_options(snim->ctx, ssl_op_disable);
        	    sprintf(lin, "%d-Pound-%ld", getpid(), random());
	            SSL_CTX_set_session_id_context(snim->ctx, (unsigned char *)lin, strlen(lin));
        	    SSL_CTX_set_tmp_rsa_callback(snim->ctx, RSA_tmp_callback);
	            SSL_CTX_set_tmp_dh_callback(snim->ctx, DH_tmp_callback);
        	    SSL_CTX_set_info_callback(snim->ctx, SSLINFO_callback);
		}
	    }
#endif
            return res;
        } else if(!regexec(&IgnoreCase, lin, 4, matches, 0)) {
            ign_case = atoi(lin + matches[1].rm_so);
        } else {
            conf_err("unknown directive");
        }
    }

    conf_err("ListenHTTPS premature EOF");
    return NULL;
}

/*
 * parse the config file
 */
static void
parse_file(void)
{
    char        lin[MAXBUF];
    SERVICE     *svc;
    LISTENER    *lstn;
    int         i;
#if HAVE_OPENSSL_ENGINE_H
    ENGINE      *e;
#endif

    while(conf_fgets(lin, MAXBUF)) {
        if(strlen(lin) > 0 && lin[strlen(lin) - 1] == '\n')
            lin[strlen(lin) - 1] = '\0';
        if(!regexec(&User, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if((user = strdup(lin + matches[1].rm_so)) == NULL)
                conf_err("User config: out of memory - aborted");
        } else if(!regexec(&Group, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if((group = strdup(lin + matches[1].rm_so)) == NULL)
                conf_err("Group config: out of memory - aborted");
        } else if(!regexec(&RootJail, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if((root_jail = strdup(lin + matches[1].rm_so)) == NULL)
                conf_err("RootJail config: out of memory - aborted");
        } else if(!regexec(&Daemon, lin, 4, matches, 0)) {
            daemonize = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&LogSNI, lin, 4, matches, 0)) {
            logsni = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&LogThreads, lin, 4, matches, 0)) {
            logthreads = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&LogRedirects, lin, 4, matches, 0)) {
            logredirects = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&LogFacility, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if(lin[matches[1].rm_so] == '-')
                def_facility = -1;
            else
                for(i = 0; facilitynames[i].c_name; i++)
                    if(!strcmp(facilitynames[i].c_name, lin + matches[1].rm_so)) {
                        def_facility = facilitynames[i].c_val;
                        break;
                    }
        } else if(!regexec(&Grace, lin, 4, matches, 0)) {
            grace = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&LogLevel, lin, 4, matches, 0)) {
            log_level = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&Client, lin, 4, matches, 0)) {
            clnt_to = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&Alive, lin, 4, matches, 0)) {
            alive_to = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&DynScale, lin, 4, matches, 0)) {
            dynscale = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&TimeOut, lin, 4, matches, 0)) {
            be_to = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&ConnTO, lin, 4, matches, 0)) {
            be_connto = atoi(lin + matches[1].rm_so);
        } else if(!regexec(&IgnoreCase, lin, 4, matches, 0)) {
            ignore_case = atoi(lin + matches[1].rm_so);
#if HAVE_OPENSSL_ENGINE_H
        } else if(!regexec(&SSLEngine, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
#if OPENSSL_VERSION_NUMBER >= 0x00907000L
            ENGINE_load_builtin_engines();
#endif
            if (!(e = ENGINE_by_id(lin + matches[1].rm_so)))
                conf_err("could not find engine");
            if(!ENGINE_init(e)) {
                ENGINE_free(e);
                conf_err("could not init engine");
            }
            if(!ENGINE_set_default(e, ENGINE_METHOD_ALL)) {
                ENGINE_free(e);
                conf_err("could not set all defaults");
            }
            ENGINE_finish(e);
            ENGINE_free(e);
#endif
        } else if(!regexec(&Control, lin, 4, matches, 0)) {
            if(ctrl_name != NULL)
                conf_err("Control multiply defined - aborted");
            lin[matches[1].rm_eo] = '\0';
            ctrl_name = strdup(lin + matches[1].rm_so);
        } else if(!regexec(&InitScript, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if((init_script = strdup(lin + matches[1].rm_so)) == NULL) {
                logmsg(LOG_ERR, "line %d: InitScript config: out of memory - aborted", n_lin);
                exit(1);
            }
        } else if(!regexec(&ControlUser, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if((control_user = strdup(lin + matches[1].rm_so)) == NULL) {
                logmsg(LOG_ERR, "line %d: ControlUser config: out of memory - aborted", n_lin);
                exit(1);
            }
        } else if(!regexec(&ControlGroup, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if((control_group = strdup(lin + matches[1].rm_so)) == NULL) {
                logmsg(LOG_ERR, "line %d: ControlGroup config: out of memory - aborted", n_lin);
                exit(1);
            }
        } else if(!regexec(&ControlMode, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            control_mode = strtol(lin+matches[1].rm_so, NULL, 8);
            if(errno==ERANGE || errno==EINVAL) {
                logmsg(LOG_ERR, "line %d: ControlMode config: %s - aborted", n_lin, strerror(errno));
                exit(1);
            }
        } else if(!regexec(&ListenHTTP, lin, 4, matches, 0)) {
            if(listeners == NULL)
                listeners = parse_HTTP();
            else {
                for(lstn = listeners; lstn->next; lstn = lstn->next)
                    ;
                lstn->next = parse_HTTP();
            }
        } else if(!regexec(&ListenHTTPS, lin, 4, matches, 0)) {
            if(listeners == NULL)
                listeners = parse_HTTPS();
            else {
                for(lstn = listeners; lstn->next; lstn = lstn->next)
                    ;
                lstn->next = parse_HTTPS();
            }
        } else if(!regexec(&Service, lin, 4, matches, 0)) {
            if(services == NULL)
                services = parse_service(NULL, 1);
            else {
                for(svc = services; svc->next; svc = svc->next)
                    ;
                svc->next = parse_service(NULL, 1);
            }
        } else if(!regexec(&ServiceName, lin, 4, matches, 0)) {
            lin[matches[1].rm_eo] = '\0';
            if(services == NULL)
                services = parse_service(lin + matches[1].rm_so, 1);
            else {
                for(svc = services; svc->next; svc = svc->next)
                    ;
                svc->next = parse_service(lin + matches[1].rm_so, 1);
            }
        } else {
            conf_err("unknown directive - aborted");
        }
    }
    return;
}

/*
 * prepare to parse the arguments/config file
 */
void
config_parse(const int argc, char **const argv)
{
    char    *conf_name;
    FILE    *f_conf;
    int     c_opt, check_only;

    if(regcomp(&Empty, "^[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Comment, "^[ \t]*#.*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&User, "^[ \t]*User[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Group, "^[ \t]*Group[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&RootJail, "^[ \t]*RootJail[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Daemon, "^[ \t]*Daemon[ \t]+([01])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&LogThreads, "^[ \t]*LogThreads[ \t]+([01])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&LogRedirects, "^[ \t]*LogRedirects[ \t]+([01])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&LogFacility, "^[ \t]*LogFacility[ \t]+([a-z0-9-]+)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&LogLevel, "^[ \t]*LogLevel[ \t]+([0-6])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Grace, "^[ \t]*Grace[ \t]+([0-9]+)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Alive, "^[ \t]*Alive[ \t]+([1-9][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&SSLEngine, "^[ \t]*SSLEngine[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&InitScript, "^[ \t]*InitScript[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Control, "^[ \t]*Control[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ControlUser, "^[ \t]*ControlUser[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ControlGroup, "^[ \t]*ControlGroup[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ControlMode, "^[ \t]*ControlMode[ \t]+([0-7]+)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ListenHTTP, "^[ \t]*ListenHTTP[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ListenHTTPS, "^[ \t]*ListenHTTPS[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&End, "^[ \t]*End[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&BackendKey, "^[ \t]*Key[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Address, "^[ \t]*Address[ \t]+([^ \t]+)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Port, "^[ \t]*Port[ \t]+([1-9][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Cert, "^[ \t]*Cert[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&LogSNI, "^[ \t]*LogSNI[ \t]+([01])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&HostCert, "^[ \t]*HostCert[ \t]+\"(.+)\"[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&xHTTP, "^[ \t]*xHTTP[ \t]+([01234])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Client, "^[ \t]*Client[ \t]+([1-9][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&CheckURL, "^[ \t]*CheckURL(|NoCase)[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&DefaultHost, "^[ \t]*DefaultHost[ \t]+\"(.*)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Err414, "^[ \t]*Err414[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Err500, "^[ \t]*Err500[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Err501, "^[ \t]*Err501[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Err503, "^[ \t]*Err503[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ErrNoSsl, "^[ \t]*ErrNoSsl[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&NoSslRedirect, "^[ \t]*NoSslRedirect[ \t]+(30[127][ \t]+)?\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&MaxRequest, "^[ \t]*MaxRequest[ \t]+([1-9][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&HeadRemove, "^[ \t]*HeadRemove[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&RewriteLocation, "^[ \t]*RewriteLocation[ \t]+([012])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&RewriteDestination, "^[ \t]*RewriteDestination[ \t]+([01])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Service, "^[ \t]*Service[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ServiceName, "^[ \t]*Service[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&URL, "^[ \t]*URL(|NoCase)[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&AuthTypeBasic, "^[ \t]*AuthType[ \t]+Basic[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&AuthTypeColdfusion, "^[ \t]*AuthType[ \t]+Coldfusion[ \t]+\"([A-Za-z0-9_]+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&AuthTypeCFAuthToken, "^[ \t]*AuthType[ \t]+(AuthToken|Token|CFAuthToken)[ \t]+\"([A-Za-z0-9_]+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&BackendCookie, "^[ \t]*BackendCookie[ \t]+\"(.+)\"[ \t]+\"(.*)\"[ \t]+\"(.*)\"[ \t]+([0-9]+|Session)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&LBInfoHeader, "^[ \t]*LBInfoHeader[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&HeadRequire, "^[ \t]*HeadRequire[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&HeadDeny, "^[ \t]*HeadDeny[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&BackEnd, "^[ \t]*BackEnd[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Emergency, "^[ \t]*Emergency[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Enabled, "^[ \t]*Enabled[ \t]+([0-1])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Priority, "^[ \t]*Priority[ \t]+([0-9])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&TimeOut, "^[ \t]*TimeOut[ \t]+([1-9][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&HAport, "^[ \t]*HAport[ \t]+([1-9][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&HAportAddr, "^[ \t]*HAport[ \t]+([^ \t]+)[ \t]+([1-9][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Redirect, "^[ \t]*Redirect(Append|Dynamic|)[ \t]+(30[127][ \t]+|)\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Session, "^[ \t]*Session[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&EndSessionHeader, "^[ \t]*EndOnHeaderMatch[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Type, "^[ \t]*Type[ \t]+([^ \t]+)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&TTL, "^[ \t]*TTL[ \t]+([1-9-][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&DeathTTL, "^[ \t]*EndOfLifeTTL[ \t]+([1-9-][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ID, "^[ \t]*ID[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&DynScale, "^[ \t]*DynScale[ \t]+([01])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ClientCert, "^[ \t]*ClientCert[ \t]+([0-3])[ \t]+([1-9])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&AddHeader, "^[ \t]*AddHeader[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&SSLAllowClientRenegotiation, "^[ \t]*SSLAllowClientRenegotiation[ \t]+([012])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&SSLHonorCipherOrder, "^[ \t]*SSLHonorCipherOrder[ \t]+([01])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Ciphers, "^[ \t]*Ciphers[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&CAlist, "^[ \t]*CAlist[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&VerifyList, "^[ \t]*VerifyList[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&CRLlist, "^[ \t]*CRLlist[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&NoHTTPS11, "^[ \t]*NoHTTPS11[ \t]+([0-2])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ForceHTTP10, "^[ \t]*ForceHTTP10[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&SSLUncleanShutdown, "^[ \t]*SSLUncleanShutdown[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&IPFreebind, "^[ \t]*IPFreebind[ \t]+([0-1])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&IPTransparent, "^[ \t]*IPTransparent[ \t]+([0-1])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&Include, "^[ \t]*Include[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&IncludeDir, "^[ \t]*IncludeDir[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&ConnTO, "^[ \t]*ConnTO[ \t]+([1-9][0-9]*)[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&IgnoreCase, "^[ \t]*IgnoreCase[ \t]+([01])[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&HTTPS, "^[ \t]*HTTPS[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    || regcomp(&HTTPSCert, "^[ \t]*HTTPS[ \t]+\"(.+)\"[ \t]*$", REG_ICASE | REG_NEWLINE | REG_EXTENDED)
    ) {
        logmsg(LOG_ERR, "bad config Regex - aborted");
        exit(1);
    }

    opterr = 0;
    check_only = 0;
    conf_name = F_CONF;
    pid_name = F_PID;

    while((c_opt = getopt(argc, argv, "f:cvVp:")) > 0)
        switch(c_opt) {
        case 'f':
            conf_name = optarg;
            break;
        case 'p':
            pid_name = optarg;
            break;
        case 'c':
            check_only = 1;
            break;
        case 'v':
            print_log = 1;
            break;
        case 'V':
            print_log = 1;
            logmsg(LOG_DEBUG, "Version %s", VERSION);
            logmsg(LOG_DEBUG, "  Configuration switches:");
#ifdef  C_SUPER
            if(strcmp(C_SUPER, "0"))
                logmsg(LOG_DEBUG, "    --disable-super");
#endif
#ifdef  C_CERT1L
            if(strcmp(C_CERT1L, "1"))
                logmsg(LOG_DEBUG, "    --enable-cert1l");
#endif
#ifdef  C_SSL
            if(strcmp(C_SSL, ""))
                logmsg(LOG_DEBUG, "    --with-ssl=%s", C_SSL);
#endif
#ifdef  C_T_RSA
            if(strcmp(C_T_RSA, "0"))
                logmsg(LOG_DEBUG, "    --with-t_rsa=%s", C_T_RSA);
#endif
#ifdef  C_MAXBUF
            if(strcmp(C_MAXBUF, "0"))
                logmsg(LOG_DEBUG, "    --with-maxbuf=%s", C_MAXBUF);
#endif
#ifdef  C_OWNER
            if(strcmp(C_OWNER, ""))
                logmsg(LOG_DEBUG, "    --with-owner=%s", C_OWNER);
#endif
#ifdef  C_GROUP
            if(strcmp(C_GROUP, ""))
                logmsg(LOG_DEBUG, "    --with-group=%s", C_GROUP);
#endif
            logmsg(LOG_DEBUG, "Exiting...");
            exit(0);
            break;
        default:
            logmsg(LOG_ERR, "bad flag -%c", optopt);
            exit(1);
            break;
        }
    if(optind < argc) {
        logmsg(LOG_ERR, "unknown extra arguments (%s...)", argv[optind]);
        exit(1);
    }

    conf_init(conf_name);

    user = NULL;
    group = NULL;
    root_jail = NULL;
    ctrl_name = NULL;

    alive_to = 30;
    daemonize = 1;
    logthreads = 0;
    logsni = 0;
    logredirects = 0;
    grace = 30;

    services = NULL;
    listeners = NULL;

    parse_file();

    if(check_only) {
        logmsg(LOG_INFO, "Config file %s is OK", conf_name);
        exit(0);
    }

    if(listeners == NULL) {
        logmsg(LOG_ERR, "no listeners defined - aborted");
        exit(1);
    }

    regfree(&Empty);
    regfree(&Comment);
    regfree(&User);
    regfree(&Group);
    regfree(&RootJail);
    regfree(&Daemon);
    regfree(&LogThreads);
    regfree(&LogRedirects);
    regfree(&LogFacility);
    regfree(&LogLevel);
    regfree(&Grace);
    regfree(&Alive);
    regfree(&SSLEngine);
    regfree(&InitScript);
    regfree(&Control);
    regfree(&ControlUser);
    regfree(&ControlGroup);
    regfree(&ControlMode);
    regfree(&ListenHTTP);
    regfree(&ListenHTTPS);
    regfree(&End);
    regfree(&BackendKey);
    regfree(&Address);
    regfree(&Port);
    regfree(&Cert);
    regfree(&LogSNI);
    regfree(&HostCert);
    regfree(&xHTTP);
    regfree(&Client);
    regfree(&CheckURL);
    regfree(&DefaultHost);
    regfree(&Err414);
    regfree(&Err500);
    regfree(&Err501);
    regfree(&Err503);
    regfree(&ErrNoSsl);
    regfree(&NoSslRedirect);
    regfree(&MaxRequest);
    regfree(&HeadRemove);
    regfree(&RewriteLocation);
    regfree(&RewriteDestination);
    regfree(&Service);
    regfree(&ServiceName);
    regfree(&URL);
    regfree(&AuthTypeBasic);
    regfree(&AuthTypeColdfusion);
    regfree(&AuthTypeCFAuthToken);
    regfree(&LBInfoHeader);
    regfree(&BackendCookie);
    regfree(&HeadRequire);
    regfree(&HeadDeny);
    regfree(&BackEnd);
    regfree(&Emergency);
    regfree(&Enabled);
    regfree(&Priority);
    regfree(&TimeOut);
    regfree(&HAport);
    regfree(&HAportAddr);
    regfree(&Redirect);
    regfree(&Session);
    regfree(&EndSessionHeader);
    regfree(&Type);
    regfree(&TTL);
    regfree(&DeathTTL);
    regfree(&ID);
    regfree(&DynScale);
    regfree(&ClientCert);
    regfree(&AddHeader);
    regfree(&SSLAllowClientRenegotiation);
    regfree(&SSLHonorCipherOrder);
    regfree(&Ciphers);
    regfree(&CAlist);
    regfree(&VerifyList);
    regfree(&CRLlist);
    regfree(&NoHTTPS11);
    regfree(&ForceHTTP10);
    regfree(&SSLUncleanShutdown);
    regfree(&IPFreebind);
    regfree(&IPTransparent);
    regfree(&Include);
    regfree(&IncludeDir);
    regfree(&ConnTO);
    regfree(&IgnoreCase);
    regfree(&HTTPS);
    regfree(&HTTPSCert);

    /* set the facility only here to ensure the syslog gets opened if necessary */
    log_facility = def_facility;

    return;
}
