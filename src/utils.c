#include <stdbool.h>
#include <string.h>

#include "utils.h"

bool is_valid_mac(const char *mac)
{
    if (strlen(mac) < MAC_ADDR_STR_LEN)
        return false;
    
    return true;
}

char *mac_to_dev_path(const char *mac)
{
    unsigned char mac[6] = {0};
    int parsed = sscanf(
        "AA:BB:cc:ee:ff:0d",
        "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
        mac, mac+1, mac+2, mac+3, mac+4, mac+5
    );

    printf("found matches: %d, MAC: %02x:%02x:%02x:%02x:%02x:%02x \n", parsed, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}