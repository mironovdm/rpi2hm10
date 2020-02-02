CC=gcc

# lgobject-2.0 - g_object_unref()
# -lgio-2.0 - All IO calls: connect, disconnect and other

all:
	$(CC) -Wall main.c \
	-I/usr/include/gio-unix-2.0 \
	`pkg-config --cflags --libs glib-2.0` \
	-lgio-2.0 \
	-lgobject-2.0 \
	-o rpi2hm10
	
clean:
	rm *.o a.out

# Help Target
help:
	@echo "The following are some of the valid targets for this Makefile:"
	@echo "... all (the default if no target is provided)"
	@echo "... clean"
