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
    for (i = 0; i < len; i++) {
        free(parts[i]);
    }
    free(parts);

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
        for (i = 0; i<proxy_pass_parts_len; i++) free(proxy_pass_parts[i]);
        free(proxy_pass_parts);
    }
    if (listen_parts) {
        for (i = 0; i<listen_parts_len; i++) free(listen_parts[i]);
        free(listen_parts);
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
    struct entity ents[255];
    int i, len = ls(cfg, ents, _ls_cb);
    qsort(&ents, len, sizeof(struct entity), entities_cmp_by_ip);



    for (i = 0; i < 255 - cfg->first_local_ip; i++) {
        if ( i == len || ents[i].port != cfg->first_local_port+i ) {
            break;
        }
    }

    int port = cfg->first_local_port + i;
    char *port_str = malloc(6), *rv = malloc(64);
    sprintf(port_str, "%d", port);

    if (add( cfg, param, port_str, param )) {
        if (!nginx_reload(cfg)) {
            strcpy(rv, "ERROR: nginx not reloaded\n");
        }
        else {
            sprintf(rv, "%d\n", port);
        }
    }
    else {
        strcpy(rv, "ERROR: not added\n");
    }

    free(port_str);
    return rv;
}

int main (int argc, char **argv) {
    return process_commands(_ls_processor, _add_processor, _rm_processor);
}

/*
 server { # labs.strava.com
                listen 192.168.1.2:443;
                proxy_pass 178.62.25.145:6001;
        }
*/
