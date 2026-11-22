#ifndef CONFIG_H
#define CONFIG_H

#include "mac_table.h"

struct config
{
    int aging;         
    int max_macs;       
    int max_perport; 
    int rate;           
    int port_wait;      
    int trunk;         
    int vlan[max_ports + 1];
};

void config_defaults(struct config *c);
int config_load(const char *path, struct config *c, int num_ports);

#endif
