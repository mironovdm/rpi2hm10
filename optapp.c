#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include "argparse.h"
#include <getopt.h>

#define OPT_END (-1)

static struct option long_opts[] = {
	{"foo", 0, NULL, 'f'},
    {"bar", true, NULL, 'b'}
};

void test_parse_short_args(int argc, char *argv[])
{
    char option;

    while((option = getopt(argc, argv, "a:b:")) != -1) {
        printf("opt=%c, optarg=%s", option, optarg);
    }
}

void test_parse_long_args(int argc, char *argv[])
{
    char option;
    int option_index = -1;

    while ((option = getopt_long(argc, argv, "fb:", long_opts, &option_index)) != OPT_END) {
        printf("opt=%c, optarg=%s, option_index=%d\n", option, optarg, option_index);
    }
    puts("");
}

int main(int argc, char *argv[]) {
    if (parse_args(argc, argv) < 0) {
        return 1;
    }

    printf("Device path: %s\n", opts.dev_path);
    printf("Char path: %s\n", opts.chr_path);
    printf("TCP host: %s\n", opts.host);
    printf("TCP port: %u\n", opts.port);

    return 0;
}