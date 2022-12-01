#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <resolv.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>

#define SERVER_PORT 1025
#define BUFF_LEN 1024

void sig_alrm_handler (int sig) {}

struct config {
    char *nginx_dir;
    char *nginx_reload_cmd;
    int first_local_port;
};

struct entity {
    char name[256];
    int port;
};

int split (char *str, char *delimiter, char ***parts) {
    char *tok = strtok(str, delimiter);
    int len = 0;

    while (tok != NULL) {
        len++;
        *parts = realloc(*parts, len * sizeof(char*));
        (*parts)[len-1] = malloc((strlen(tok) + 1) * sizeof(char));
        strcpy((*parts)[len-1], tok);
        tok = strtok(NULL, delimiter);
    }

    return len;
}

struct config read_config (char *path) {
    struct config cfg;

    cfg.nginx_dir = cfg.nginx_reload_cmd = NULL;
    cfg.first_local_port = 0;

    FILE *fh = fopen(path, "r");
    if ( fh == NULL ) {
        perror("fopen");
        exit(255);
    }

    char *line = NULL;
    size_t len = 0, readed;
    char **parts = NULL;
    int parts_len;

    while ((readed = getline(&line, &len, fh)) != -1 ) {
        line[readed-1] = '\0';
        parts_len = split(line, "=", &parts);
        if (parts_len != 2) {
            fprintf(stderr, "confix syntax error\n");
            exit(255);
        }

        if (strcmp(parts[0], "nginx_dir") == 0) {
            cfg.nginx_dir = parts[1];
        }
        else if (strcmp(parts[0], "nginx_reload_cmd") == 0) {
            cfg.nginx_reload_cmd = parts[1];
        }
        else if (strcmp(parts[0], "first_local_port") == 0) {
            cfg.first_local_port = atoi(parts[1]);
            free(parts[1]);
        }
        else {
            fprintf(stderr, "unknown config option: %s\n", parts[0]);
            exit(255);
        }

        free(parts[0]);
        free(parts);
        free(line);
        parts = NULL;
        line = NULL;
        len = 0;
    }

    if ( cfg.nginx_dir == NULL || cfg.nginx_reload_cmd == NULL || cfg.first_local_port == 0 ) {
        fprintf(stderr, "incomplete config\n");
        exit(255);
    }

    fclose(fh);

    return cfg;
}

char *trim (char *str) {
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)  // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';

    return str;
}

int read_entity_port(struct config *cfg, char *fname) {
    char *fpath = malloc( (strlen(cfg->nginx_dir) + strlen(fname) + 2) * sizeof(char) );
    sprintf(fpath, "%s/%s", cfg->nginx_dir, fname);
    FILE *fh = fopen(fpath, "r");
    free(fpath);
    if (fh == NULL) {
        perror("fopen");
        return 0;
    }

    char *line = NULL, *tline;
    size_t len = 0, readed;
    int port = 0;
    while ((readed = getline(&line, &len, fh)) != -1) {
        line[readed-1] = '\0';
        tline = trim(line);
        if (strncmp(tline, "listen ", 7) == 0) {
            tline[strlen(tline)-1] = '\0'; // remove ";"
            port = atoi(tline + 7);
        }

        free(line);
        line = NULL;
        len = 0;
        if (port > 0) break;
    }

    fclose(fh);

    return port;
}

int entities_cmp_alphabetic(const void *a, const void *b) {
    return strcmp( ((struct entity *)a)->name, ((struct entity *)b)->name ) ;
}

int entities_cmp_numeric(const void *a, const void *b) {
    return ((struct entity *)a)->port - ((struct entity *)b)->port;
}

int ls (struct config *cfg, struct entity *entities) {
    DIR *dh = opendir(cfg->nginx_dir);
    if (dh == NULL) {
        perror("opendir");
        return 0;
    }

    struct dirent *de;
    int len = 0;

    while ( (de = readdir(dh)) != NULL ) {
        if ( strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0 ) continue;

        strcpy(entities[len].name, de->d_name);
        entities[len].port = read_entity_port(cfg, de->d_name);
        len++;
    }

    closedir(dh);
    return len;
}

char rm (struct config *cfg, char *fname) {
    char *fpath = malloc( (strlen(cfg->nginx_dir) + strlen(fname) + 2) * sizeof(char) );
    sprintf(fpath, "%s/%s", cfg->nginx_dir, fname);

    char rv = remove(fpath) == -1 ? 0 : 1;
    free(fpath);

    return rv;
}

int add (struct config *cfg, char *fname) {
    struct entity ents[255];
    int i, len = ls(cfg, ents);
    qsort(&ents, len, sizeof(struct entity), entities_cmp_numeric);
    for (i = 0; i < 65535 - cfg->first_local_port; i++) {
        if ( i == len || ents[i].port != cfg->first_local_port+i ) {
            break;
        }
    }

    int port = cfg->first_local_port + i;

    char tpl[] = "server {\n listen %d;\n proxy_pass %s;\n}\n";
    char *ngnx = malloc((strlen(tpl) + strlen(fname) + 5) * sizeof(char));
    sprintf(ngnx, tpl, port, fname);

    char *fpath = malloc( (strlen(cfg->nginx_dir) + strlen(fname) + 2) * sizeof(char) );
    sprintf(fpath, "%s/%s", cfg->nginx_dir, fname);

    FILE *fh = fopen(fpath, "w");
    if (fh != NULL) {
        fputs(ngnx, fh);
        fclose(fh);
    }
    else {
        port = 0;
        perror("fopen");
    }

    free(ngnx);
    free(fpath);

    return port;
}

char nginx_reload(struct config *cfg) {
    return system(cfg->nginx_reload_cmd) == 0 ? 1 : 0;
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
