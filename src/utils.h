#ifndef __UTILS_H
#define __UTILS_H

#define MAC_ADDR_STR_LEN 17
#define MAC_ADDR_BYTES 6


bool is_valid_mac_str(const char *mac_str);

/**
 * Convert MAC string to 6 bytes array.
 * This functions assums MAC string is valid, see 'is_valid_mac_str()'.
 */
const unsigned char *mac_str_to_bytes(const char *mac_str);

/**
 * Convert MAC addr bytes to string.
 * mac should be an array of 6 bytes.
 */
const char *mac_to_str(const unsigned char mac[]);


#endif
