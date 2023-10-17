#include "nginx-ab-editor.h"

char dnsmasq_reload(struct config *cfg) {
    if (!cfg->dnsmasq_reload_cmd) return 0;
    return system(cfg->dnsmasq_reload_cmd) == 0 ? 1 : 0;
}

char add_to_dnsmasq(struct config *cfg, char *ip, char *host) {
    if (!cfg->dnsmasq_hosts_path) return 0;

    FILE *fh = fopen(cfg->dnsmasq_hosts_path, "a");
    if (fh == NULL) {
        perror("fopen");
        return 0;
    }

    fprintf(fh, "%s %s\n", ip, host);
    fclose(fh);

    return 1;
}

char rm_from_dnsmasq(struct config *cfg, char *ip) {
    if (!cfg->dnsmasq_hosts_path) return 0;

    FILE *fh_orig = fopen(cfg->dnsmasq_hosts_path, "r");
    if (fh_orig == NULL) {
        perror("fopen");
        return 0;
    }

    char *tmp_name = malloc(strlen(cfg->dnsmasq_hosts_path) + 5), rv = 1;
    sprintf(tmp_name, "%s.tmp", cfg->dnsmasq_hosts_path);

    FILE *fh_tmp = fopen(tmp_name, "w");
    if (fh_tmp == NULL) {
        perror("fopen");
        rv = 0;
        goto END;
    }

    size_t len = 0;
    char *line = NULL;
    int ip_len = strlen(ip);

    while (getline(&line, &len, fh_orig) > 0 ) {
        char *tmp_line = trim(line);
        if (strncmp(tmp_line, ip, ip_len) == 0 && strlen(tmp_line) > ip_len && isspace(tmp_line[ip_len]))
            continue;

        fputs(line, fh_tmp);
        fputs("\n", fh_tmp);

        free(line);
        line = NULL;
        len = 0;
    }

    END:
    if (fh_orig) fclose(fh_orig);
    if (fh_tmp)  fclose(fh_tmp);

    if (rv) {
        if (rename(tmp_name, cfg->dnsmasq_hosts_path) == -1) rv = 0;
    }

    free(tmp_name);

    return rv;
}

char *get_last_ip_octet(char *ip) {
    char **parts, *rv = NULL;
    int len = split(ip, ".", &parts), i;
    if (len != 4) {
        goto END;
    }
    rv = parts[3];
    len--;

    END:
    free_splitted_parts(parts, len);
    return rv;
}

int get_last_ip_octet_int(char *ip) {
    char *octet = get_last_ip_octet(ip);
    if (!octet) return 0;

    int rv = atoi(octet);
    free(octet);

    return rv;
}

int entities_cmp_by_ip(const void *a, const void *b) {
    char *a_ip = ((struct entity *)a)->ip;
    char *b_ip = ((struct entity *)b)->ip;

    char *a_octet = NULL, *b_octet = NULL;
    int rv = 0;

    a_octet = get_last_ip_octet(a_ip);
    b_octet = get_last_ip_octet(b_ip);

    if (!a_octet || !b_octet) goto END;

    rv = atoi(a_octet) - atoi(b_octet);

    END:
    if (a_octet) free(a_octet);
    if (b_octet) free(b_octet);

    return rv;
}

void _ls_cb(struct config *cfg, struct entity *entity, char *name) {
    char **proxy_pass_parts = NULL, *listen_param = NULL, *proxy_pass_param = NULL;
    char **listen_parts = NULL;
    int proxy_pass_parts_len = 0, listen_parts_len = 0, i;

    listen_param = read_entity_param(cfg, name, "listen ");
    if (!listen_param) goto END;
    proxy_pass_param = read_entity_param(cfg, name, "proxy_pass ");
    if (!proxy_pass_param) goto END;

    proxy_pass_parts_len = split(proxy_pass_param, ":", &proxy_pass_parts);
    if (proxy_pass_parts_len != 2) goto END;
    entity->port = atoi(proxy_pass_parts[1]);

    listen_parts_len = split(listen_param, ":", &listen_parts);
    if (listen_parts_len != 2) goto END;
    strcpy(entity->ip, listen_parts[0]);

    END:
    if (listen_param) (listen_param);
    if (proxy_pass_param) free(proxy_pass_param);
    if (proxy_pass_parts) {
        free_splitted_parts(proxy_pass_parts, proxy_pass_parts_len);
    }
    if (listen_parts) {
        free_splitted_parts(listen_parts, listen_parts_len);
    }
}

char *_ls_processor(struct config *cfg) {
    struct entity ents[255];
    int i, ents_len = ls(cfg, ents, _ls_cb);
    qsort(&ents, ents_len, sizeof(struct entity), entities_cmp_by_name);

    size_t allocated_len = 255, res_len = 0;
    char *res = malloc(allocated_len);

    for ( i=0; i<ents_len; i++ ) {
        int add_len = strlen(ents[i].name) + strlen(ents[i].ip) + 9 + 5 + 1;
        if ( add_len > allocated_len - res_len ) {
            allocated_len += add_len > 255 ? add_len : 255;
            res = realloc(res, allocated_len);
        }
        sprintf(res+res_len, "%s -> %s -> %d\n", ents[i].name, ents[i].ip, ents[i].port);
        res_len = strlen(res);
    }

    if (!ents_len) strcpy(res, "\n");

    return res;
}

char *_add_processor(struct config *cfg, char *param) {
    char **parts;
    char *rv = malloc(strlen(param) + 64);
    int parts_len = split(param, " ", &parts);

    if (parts_len != 2) {
        free_splitted_parts(parts, parts_len);
        strcpy(rv, "ERROR: incorrect add param\n");
        return rv;
    }

    char *name = parts[0];
    char *proxy_pass = parts[1];
    free(parts);

    parts_len = split(name, ":", &parts);
    if (parts_len != 2) {
        free_splitted_parts(parts, parts_len);
        free(name);
        free(proxy_pass);
        strcpy(rv, "ERROR: incorrect name param\n");
        return rv;
    }

    char *host = parts[0];
    char *local_port = parts[1];
    free(parts);

    struct entity ents[255];
    int i, len = ls(cfg, ents, _ls_cb);

    // should reuse ip if such domain already exists with other port
    char *ip = malloc( strlen(cfg->first_local_ip) + 9 ), reused_ip = 0;
    int dname_len = strlen(name) - strlen(local_port);
    for (i = 0; i < len; i++) {
        if (strncmp(ents[i].name, name, dname_len) == 0) {
            sprintf(ip, "%s:%s", ents[i].ip, local_port);
            reused_ip = 1;
            break;
        }
    }

    if (!reused_ip) {
        qsort(&ents, len, sizeof(struct entity), entities_cmp_by_ip);

        int first_ip_octet_int = 0;
        char *first_ip_octet = get_last_ip_octet(cfg->first_local_ip);
        int first_ip_octet_len = 0;
        if (first_ip_octet) {
            first_ip_octet_len = strlen(first_ip_octet);
            first_ip_octet_int = atoi(first_ip_octet);
            free(first_ip_octet);
        }

        int j = 0, ip_octet;

        for (i = 0; j < 255 - first_ip_octet_int; i++, j++) {
            ip_octet = get_last_ip_octet_int(ents[i].ip);

            if (i > 0 && get_last_ip_octet_int(ents[i-1].ip) == ip_octet ) {
                // skip duplicate
                j--;
                continue;
            }

            if ( i == len || ip_octet != first_ip_octet_int+j ) {
                break;
            }
        }

        strncpy(ip, cfg->first_local_ip, strlen(cfg->first_local_ip) - first_ip_octet_len );
        sprintf(ip + strlen(cfg->first_local_ip) - first_ip_octet_len, "%d:%s", first_ip_octet_int+j, local_port);
    }

    if (!add( cfg, name, ip, proxy_pass )) {
        strcpy(rv, "ERROR: not added\n");
        goto END;
    }

    if (!nginx_reload(cfg)) {
        strcpy(rv, "ERROR: nginx not reloaded\n");
        goto END;
    }

    ip[strlen(ip) - strlen(local_port) - 1] = '\0';

    if (!reused_ip) {
        if (!add_to_dnsmasq( cfg, ip, host )) {
            strcpy(rv, "ERROR: host not added to dnsmasq\n");
            goto END;
        }

        if (!dnsmasq_reload(cfg)) {
            strcpy(rv, "ERROR: dnsmasq not reloaded\n");
            goto END;
        }
    }

    sprintf(rv, "%s\n", ip);

    END:
    free(ip);
    free(name);
    free(proxy_pass);
    free(host);
    free(local_port);

    return rv;
}

char *_rm_processor(struct config *cfg, char *param) {
    struct entity ents[255];
    int i, ents_len = ls(cfg, ents, _ls_cb);

    char *idx = strchr(param, ':'), name_is_uniq = 1;
    int cmp_len = 0;
    if (idx != NULL) {
        cmp_len = idx - param + 1;
    }

    for (i = 0; i< ents_len; i++) {
        if ( strcmp(ents[i].name, param) != 0 && strncmp(ents[i].name, param, cmp_len) == 0 ) {
            name_is_uniq = 0;
            break;
        }
    }

    char *res = malloc(64);
    char *ip = NULL;
    if (name_is_uniq) ip = read_entity_param(cfg, param, "listen ");

    if (!rm(cfg, param)) {
        strcpy(res, "ERROR: not removed\n");
        goto END;
    }

    if (!nginx_reload(cfg)) {
        strcpy(res, "ERROR: nginx not reloaded\n");
        goto END;
    }

    if (name_is_uniq) {
        if ( (idx = strchr(ip, ':')) != NULL ) {
            *idx = '\0';
        }

        if (!rm_from_dnsmasq(cfg, ip)) {
            strcpy(res, "ERROR: ip not removed from dnsmasq\n");
            goto END;
        }

        if (!dnsmasq_reload(cfg)) {
            strcpy(res, "ERROR: dnsmasq not reloaded\n");
            goto END;
        }
    }

    strcpy(res, "SUCCESS\n");

    END:
    if (ip) free(ip);

    return res;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    return process_commands(_ls_processor, _add_processor, _rm_processor);
}
