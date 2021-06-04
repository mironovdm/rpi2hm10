CFLAGS = -Wall --std=gnu11 -pedantic -g -O2

TARGET = rpi2hm10
SRCDIR = src
BUILDDIR = build

INCLUDE_GLIB = `pkg-config --cflags glib-2.0 gio-unix-2.0`
LIB_GLIB = `pkg-config --libs glib-2.0`
LIB_GIO = `pkg-config --libs gio-2.0`
LIBS = -lpthread

sources := $(wildcard $(SRCDIR)/*.c)
objects := $(patsubst $(SRCDIR)/%.o,$(BUILDDIR)/%.o,$(sources:%.c=%.o))

$(info # objects = ${objects})
$(info # sources = ${sources})

.PHONY: call all required_dirs clean

all: required_dirs $(TARGET)

call: clean all

$(TARGET): $(objects)
	$(CC) $(CFLAGS) $^ $(LIB_GLIB) $(LIB_GIO) $(LIBS) -o $(TARGET)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDE_GLIB) -c -o $@ $<


required_dirs:
	mkdir -p build

clean:
	rm -f $(SRCDIR)/*.o ./*.o rpi2hm10 build/*.*

# Help Target
help:
	@echo "The following are some of the valid targets for this Makefile:"
	@echo "... all (the default if no target is provided)"
	@echo "... clean"
