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

struct config {
    char *nginx_dir;
    char *nginx_reload_cmd;
    char *first_local_ip;
    int first_local_port;
};

struct entity {
    char name[256];
    char ip[17];
    int port;
};

void sig_alrm_handler(int sig) {}

int entities_cmp_by_name(const void *a, const void *b) {
    return strcmp( ((struct entity *)a)->name, ((struct entity *)b)->name ) ;
}

int split (char *str, char *delimiter, char ***parts) {
    // strtok modifies first param
    char *tmp_str = malloc(strlen(str) + 1);
    strcpy(tmp_str, str);

    *parts = NULL;
    char *tok = strtok(tmp_str, delimiter);
    int len = 0;

    while (tok != NULL) {
        len++;
        *parts = realloc(*parts, len * sizeof(char*));
        (*parts)[len-1] = malloc((strlen(tok) + 1) * sizeof(char));
        strcpy((*parts)[len-1], tok);
        tok = strtok(NULL, delimiter);
    }

    free(tmp_str);

    return len;
}

void free_splitted_parts(char **parts, int len) {
    int i;
    for (i = 0; i < len; i++) {
        free(parts[i]);
    }

    free(parts);
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

char nginx_reload(struct config *cfg) {
    return system(cfg->nginx_reload_cmd) == 0 ? 1 : 0;
}

struct config read_config (char *path) {
    struct config cfg;

    cfg.nginx_dir = cfg.nginx_reload_cmd = cfg.first_local_ip = NULL;
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

        char *key = trim(parts[0]), *val = trim(parts[1]);

        if (strcmp(key, "nginx_dir") == 0) {
            cfg.nginx_dir = val;
        }
        else if (strcmp(key, "nginx_reload_cmd") == 0) {
            cfg.nginx_reload_cmd = val;
        }
        else if (strcmp(key, "first_local_port") == 0) {
            cfg.first_local_port = atoi(val);
            free(parts[1]);
        }
        else if (strcmp(key, "first_local_ip") == 0) {
            cfg.first_local_ip = val;
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

    if ( cfg.nginx_dir == NULL || cfg.nginx_reload_cmd == NULL || ( cfg.first_local_port == 0 && cfg.first_local_ip == NULL ) ) {
        fprintf(stderr, "incomplete config\n");
        exit(255);
    }

    fclose(fh);
    return cfg;
}

char *read_entity_param(struct config *cfg, char *fname, char *param) {
    char *fpath = malloc( (strlen(cfg->nginx_dir) + strlen(fname) + 2) * sizeof(char) );
    sprintf(fpath, "%s/%s", cfg->nginx_dir, fname);
    FILE *fh = fopen(fpath, "r");
    free(fpath);
    if (fh == NULL) {
        perror("fopen");
        return 0;
    }

    char *line = NULL, *tline, *res = NULL;
    size_t len = 0, readed, param_len = strlen(param);

    while ((readed = getline(&line, &len, fh)) != -1) {
        line[readed-1] = '\0';
        tline = trim(line);
        if (strncmp(tline, param, param_len) == 0) {
            tline[strlen(tline)-1] = '\0'; // remove ";"
            res = malloc(strlen(tline) - param_len + 1);
            strcpy(res, trim(tline + param_len));
        }

        free(line);
        line = NULL;
        len = 0;
        if (res != NULL) break;
    }

    fclose(fh);

    return res;
}

int ls(struct config *cfg, struct entity *entities, void (*ls_cb)(struct config *cfg, struct entity *entity, char *name)) {
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
        ls_cb(cfg, &entities[len], de->d_name);
        len++;
    }

    closedir(dh);
    return len;
}

char rm(struct config *cfg, char *fname) {
    char *fpath = malloc( (strlen(cfg->nginx_dir) + strlen(fname) + 2) * sizeof(char) );
    sprintf(fpath, "%s/%s", cfg->nginx_dir, fname);

    char rv = remove(fpath) == -1 ? 0 : 1;
    free(fpath);

    return rv;
}

char add(struct config *cfg, char *fname, char *listen_param, char *proxy_pass_param) {
    char tpl[] = "server {\n listen %s;\n proxy_pass %s;\n}\n";
    char *ngnx = malloc((strlen(tpl) + strlen(listen_param) + strlen(proxy_pass_param)) * sizeof(char));
    sprintf(ngnx, tpl, listen_param, proxy_pass_param);

    char *fpath = malloc( (strlen(cfg->nginx_dir) + strlen(fname) + 2) * sizeof(char) );
    sprintf(fpath, "%s/%s", cfg->nginx_dir, fname);

    FILE *fh = fopen(fpath, "w");
    char rv = 1;
    if (fh != NULL) {
        fputs(ngnx, fh);
        fclose(fh);
    }
    else {
        rv = 0;
        perror("fopen");
    }

    free(ngnx);
    free(fpath);

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

int process_commands(char *(*ls_processor)(struct config *), char *(*add_processor)(struct config *, char *), char *(*rm_processor)(struct config *, char *)) {
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

    char cmd[BUFF_LEN], got_cmd;
    int client, readed, cmdlen;

    while ((client = accept(server, NULL, NULL)) != -1) {
        cmdlen = 0;

        // for timeout
        alarm(5);
        got_cmd = 0;

        while ((readed = recv(client, cmd + cmdlen, BUFF_LEN - cmdlen, 0)) > 0) {
            cmdlen += readed;
            if (cmd[cmdlen - 1] == '\n') {
                cmd[cmdlen - 1] = '\0';
                got_cmd = 1;
                break;
            }
        }

        alarm(0);
        char *res = NULL;

        if (got_cmd && strcmp(cmd, "ls") == 0) {
            res = ls_processor(&cfg);
        }
        else if ( got_cmd && strncmp(cmd, "add ", 4) == 0 ) {
            res = add_processor(&cfg, trim(cmd + 4));
        }
        else if ( got_cmd && strncmp(cmd, "rm ", 3) == 0 ) {
            res = rm_processor(&cfg, trim(cmd + 3));
        }
        else {
            strcpy(cmd, "ERROR: unsupported command\n");
            send(client, cmd, strlen(cmd), 0);
        }

        if (res) {
            send(client, res, strlen(res), 0);
            free(res);
        }
        close(client);
    }

    return 0;
}
