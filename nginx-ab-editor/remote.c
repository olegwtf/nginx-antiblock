#include "nginx-ab-editor.h"

int entities_cmp_by_port(const void *a, const void *b) {
    return ((struct entity *)a)->port - ((struct entity *)b)->port;
}

void _ls_cb(struct config *cfg, struct entity *entity, char *name) {
    char *listen_param = read_entity_param(cfg, name, "listen ");
    if (!listen_param) return;
    entity->port = atoi(listen_param);
    free(listen_param);
}

char *_ls_processor(struct config *cfg) {
    struct entity ents[255];
    int i, ents_len = ls(cfg, ents, _ls_cb);
    qsort(&ents, ents_len, sizeof(struct entity), entities_cmp_by_name);

    size_t allocated_len = 255, res_len = 0;
    char *res = malloc(allocated_len);

    for ( i=0; i<ents_len; i++ ) {
        int add_len = strlen(ents[i].name) + 5 + 5 + 1;
        if ( add_len > allocated_len - res_len ) {
            allocated_len += add_len > 255 ? add_len : 255;
            res = realloc(res, allocated_len);
        }
        sprintf(res+res_len, "%s -> %d\n", ents[i].name, ents[i].port);
        res_len = strlen(res);
    }

    if (!ents_len) strcpy(res, "\n");

    return res;
}

char *_add_processor(struct config *cfg, char *param) {
    struct entity ents[255];
    int i, len = ls(cfg, ents, _ls_cb);
    qsort(&ents, len, sizeof(struct entity), entities_cmp_by_port);
    for (i = 0; i < 65535 - cfg->first_local_port; i++) {
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

char *_rm_processor(struct config *cfg, char *param) {
    char *res = malloc(64);

    if (rm(cfg, param)) {
        if (!nginx_reload(cfg)) {
            strcpy(res, "ERROR: nginx not reloaded\n");
        }
        else {
            strcpy(res, "SUCCESS\n");
        }
    }
    else {
        strcpy(res, "ERROR: not removed\n");
    }

    return res;
}

int main (int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    return process_commands(_ls_processor, _add_processor, _rm_processor);
}
