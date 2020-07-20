#!/bin/bash

# Exit on first error
set -e

gcc -c \
    argparse.c \
    optapp.c
gcc argparse.o optapp.o -o optapp
