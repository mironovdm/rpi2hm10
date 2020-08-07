
all: argparse
	$(CC) -Wall --std=gnu11 \
	main.c \
	argparse.o \
	-I/usr/include/gio-unix-2.0 \
	`pkg-config --cflags glib-2.0` \
	`pkg-config --libs glib-2.0` \
	-lgio-2.0 \
	-lgobject-2.0 \
	-o rpi2hm10

argparse:
	$(CC) -Wall -c -o argparse.o argparse.c
	
clean:
	rm *.o rpi2hm10

# Help Target
help:
	@echo "The following are some of the valid targets for this Makefile:"
	@echo "... all (the default if no target is provided)"
	@echo "... clean"
