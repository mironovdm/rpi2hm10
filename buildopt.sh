#!/bin/bash

# Exit on first error
set -e

clang -c \
    argparse.c \
    optapp.c
clang argparse.o optapp.o -o optapp
