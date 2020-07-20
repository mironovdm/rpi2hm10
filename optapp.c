#include <stdlib.h>
#include <stdio.h>
#include "argparse.h"

extern struct options opts;

int main(int argc, char *argv[]) {
    int err;
    
    err = parse_args(argc, argv);

    if (err) {
        perror("Invalid paramters\n");
        return 1;
    }

    puts("Params are ok");

    return 0;
}