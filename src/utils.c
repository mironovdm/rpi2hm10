#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

bool is_valid_mac_str(const char *mac_str)
{
    int matches;
    unsigned char mac = 0;

    if (strlen(mac_str) < MAC_ADDR_STR_LEN)
        return false;

    matches = sscanf(
        mac_str,
        "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
        &mac, &mac, &mac, &mac, &mac, &mac
    );

    if (matches == EOF || matches < MAC_ADDR_STR_LEN)
        return false;
    
    return true;
}

const unsigned char *mac_str_to_bytes(const char *mac_str)
{
    int matches;
    const unsigned char *mac = malloc(sizeof(unsigned char) * MAC_ADDR_BYTES);
    if (!mac)
        return NULL;

    matches = sscanf(
        mac_str,
        "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
        mac, mac+1, mac+2, mac+3, mac+4, mac+5
    );

    return mac;
}

const char *mac_to_str(const unsigned char *mac)
{
    const char *mac_str = malloc(MAC_ADDR_STR_LEN + 1);
    
    if (!mac_str)
        return NULL;

    sprintf(
        mac_str,
        "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
}

unsigned char *split_object_path()
{

}