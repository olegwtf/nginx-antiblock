#include "ninx-ab-editor.h"

int entities_cmp_alphabetic(const void *a, const void *b) {
    return strcmp( ((struct entity *)a)->name, ((struct entity *)b)->name ) ;
}

int entities_cmp_numeric(const void *a, const void *b) {
    return ((struct entity *)a)->port - ((struct entity *)b)->port;
}

void _ls_helper(struct config *cfg, struct entity *entity, char *name) {
    char *listen_param = read_entity_listen_param(cfg, name);
    entity->port = atoi(listen_param);
    free(listen_param);
}

char *_ls_processor(struct config *cfg) {
    struct entity ents[255];
    int i, len = ls(cfg, ents, _ls_helper);
    qsort(&ents, len, sizeof(struct entity), entities_cmp_alphabetic);

    size_t len = 0, allocated_len = 255;
    char *res = malloc(allocated_len);

    for ( i=0; i<len; i++ ) {
        int add_len = strlen(ents[i].name) + 5 + 5 + 1;
        if ( add_len > allocated_len - len ) {
            allocated_len += add_len > 255 ? add_len : 255;
            res = realloc(res, allocated_len);
        }
        sprintf(res+len, "%s -> %d\n", ents[i].name, ents[i].port);
        len = strlen(res);
    }

    return res;
}

int main (int argc, char **argv) {
    struct config cfg = read_config("nginx-ab-editor.cfg");

    int server;
    if ((server = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return 1;
    }

    int on = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT); // convert from host byte order to network byte order
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 2;
    }

    if (listen(server, 10) != 0) {
        perror("listen");
        return 3;
    }

    struct sigaction act;
    act.sa_handler = sig_alrm_handler;
    act.sa_flags   = 0;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        return 4;
    }

    char cmd[BUFF_LEN];
    int client, readed, cmdlen;

    while ((client = accept(server, NULL, NULL)) != -1) {
        cmdlen = 0;

        // for timeout
        alarm(5);

        while ((readed = recv(client, cmd + cmdlen, BUFF_LEN - cmdlen, 0)) > 0) {
            cmdlen += readed;
            if (cmd[cmdlen - 1] == '\n') {
                cmd[cmdlen - 1] = '\0';
                break;
            }
        }

        alarm(0);

        if (strcmp(cmd, "ls") == 0) {
            struct entity ents[255];
            int i, len = ls(&cfg, ents);
            qsort(&ents, len, sizeof(struct entity), entities_cmp_alphabetic);

            for ( i=0; i<len; i++ ) {
                sprintf(cmd, "%s -> %d\n", ents[i].name, ents[i].port);
                if ( send(client, cmd, strlen(cmd), 0) <= 0 ) break;
            }
        }
        else if ( strncmp(cmd, "add ", 4) == 0 ) {
            int port;

            if (port = add(&cfg, trim(cmd+4))) {
                if (!nginx_reload(&cfg)) {
                    strcpy(cmd, "ERROR: nginx not reloaded\n");
                }
                else {
                    sprintf(cmd, "%d\n", port);
                }
            }
            else {
                strcpy(cmd, "ERROR: not added\n");
            }

            send(client, cmd, strlen(cmd), 0);
        }
        else if ( strncmp(cmd, "rm ", 3) == 0 ) {
            if (rm(&cfg, trim(cmd+3))) {
                if (!nginx_reload(&cfg)) {
                    strcpy(cmd, "ERROR: nginx not reloaded\n");
                }
                else {
                    strcpy(cmd, "SUCCESS\n");
                }
            }
            else {
                strcpy(cmd, "ERROR: not removed\n");
            }

            send(client, cmd, strlen(cmd), 0);
        }
        else {
            strcpy(cmd, "ERROR: unsupported command\n");
            send(client, cmd, strlen(cmd), 0);
        }

        close(client);
    }
}
