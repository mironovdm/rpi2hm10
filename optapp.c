#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
// #include "argparse.h"

void print_bytes(const void *ptr, int len) {
    while (len--) {
        unsigned char chr = *((unsigned char *)ptr);
        printf("%02x", (unsigned int)chr);
        ptr++;
    }
    puts("");
}

int main(int argc, char *argv[]) {
    uint16_t val = 0;
    union {
        unsigned char c[4];
        uint32_t i;
    } a = {0};
    
    print_bytes(&a, 4);
    int res = sscanf("11111", "%" SCNu16, (uint16_t *)&a.i);
    printf("sscanf returned: %d, value=%u, array=%08x\n", res, (unsigned)val, (unsigned)a.i);
    print_bytes(&a.c, 4);
    return 0;


    // int err;
    // puts(help_text);
    // return 0;
    
    // err = parse_args(argc, argv);

    // if (err) {
    //     perror("Invalid paramters\n");
    //     return 1;
    // }

    // puts("Params are ok");

    // return 0;
}