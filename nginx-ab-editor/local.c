#include "nginx-ab-editor.h"

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

    char *local_port = parts[1];
    free(parts[0]);
    free(parts);

    struct entity ents[255];
    int i, len = ls(cfg, ents, _ls_cb);
    qsort(&ents, len, sizeof(struct entity), entities_cmp_by_ip);

    int first_ip_octet_int = 0;
    char *first_ip_octet = get_last_ip_octet(cfg->first_local_ip);
    int first_ip_octet_len = 0;
    if (first_ip_octet) {
        first_ip_octet_len = strlen(first_ip_octet);
        first_ip_octet_int = atoi(first_ip_octet);
        free(first_ip_octet);
    }

    for (i = 0; i < 255 - first_ip_octet_int; i++) {
        if ( i == len || get_last_ip_octet_int( ents[i].ip ) != first_ip_octet_int+i ) {
            break;
        }
    }

    char *ip = malloc( strlen(cfg->first_local_ip) + 9 );
    strncpy(ip, cfg->first_local_ip, strlen(cfg->first_local_ip) - first_ip_octet_len );
    sprintf(ip + strlen(cfg->first_local_ip) - first_ip_octet_len, "%d:%s", first_ip_octet_int+i, local_port);

    if (add( cfg, name, ip, proxy_pass )) {
        if (!nginx_reload(cfg)) {
            strcpy(rv, "ERROR: nginx not reloaded\n");
        }
        else {
            sprintf(rv, "%s\n", ip);
        }
    }
    else {
        strcpy(rv, "ERROR: not added\n");
    }

    free(ip);
    free(name);
    free(proxy_pass);
    free(local_port);

    return rv;
}

int main(int argc, char **argv) {
    return process_commands(_ls_processor, _add_processor, _rm_processor);
}
