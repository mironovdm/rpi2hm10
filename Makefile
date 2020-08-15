CFLAGS = -Wall --std=gnu11

BINARY = rpi2hm10
SRCDIR = src

INCLUDE_GLIB = `pkg-config --cflags glib-2.0 gio-unix-2.0`
LIB_GLIB = `pkg-config --libs glib-2.0`
LIB_GIO = `pkg-config --libs gio-2.0`


.PHONY: all clean

all: $(BINARY)

$(BINARY): $(SRCDIR)/main.o $(SRCDIR)/argparse.o
	$(CC) $(CFLAGS) $^ $(LIB_GLIB) $(LIB_GIO) -o $(BINARY)

$(SRCDIR)/main.o: $(SRCDIR)/main.c
	$(CC) $(CFLAGS) $(INCLUDE_GLIB) -c -o $@ $<

argparse.o: argparse.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(SRCDIR)/*.o rpi2hm10

# Help Target
help:
	@echo "The following are some of the valid targets for this Makefile:"
	@echo "... all (the default if no target is provided)"
	@echo "... clean"
