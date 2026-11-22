#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

void config_defaults(struct config *c)
{
    c->aging = 30;
    c->max_macs = 1024;
    c->max_perport = 16;
    c->rate = 0;
    c->port_wait = 5;
    c->trunk = 0;
    for (int i = 0; i <= max_ports; i++)
        c->vlan[i] = 1;
}

int config_load(const char *path, struct config *c, int num_ports)
{
    FILE *f;
    char line[256];
    char key[32];
    int a, b, n, ok;
    int lineno = 0;

    f = fopen(path, "r");
    if (f == NULL)
    {
        perror(path);
        return -1;
    }

    while (fgets(line, sizeof(line), f))
    {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';

        n = sscanf(line, "%31s %d %d", key, &a, &b);
        if (n <= 0)
            continue;

        ok = 1;
        if (strcmp(key, "aging") == 0 && n == 2 && a > 0)
            c->aging = a;
        else if (strcmp(key, "max_macs") == 0 && n == 2 && a > 0)
            c->max_macs = a;
        else if (strcmp(key, "max_per_port") == 0 && n == 2 && a > 0)
            c->max_perport = a;
        else if (strcmp(key, "rate") == 0 && n == 2 && a >= 0)
            c->rate = a;
        else if (strcmp(key, "port_wait") == 0 && n == 2 && a >= 0)
            c->port_wait = a;
        else if (strcmp(key, "trunk") == 0 && n == 2 && a >= 0 && a <= num_ports)
            c->trunk = a;
        else if (strcmp(key, "vlan") == 0 && n == 3 && a >= 1 && a <= num_ports && b >= 1 && b <= 4094)
            c->vlan[a] = b;
        else
            ok = 0;

        if (!ok)
        {
            fprintf(stderr, "%s line %d: bad setting\n", path, lineno);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}
