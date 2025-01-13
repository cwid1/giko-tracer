CC = gcc
CFLAGS = -Iinclude -Wall -Wextra $(shell pkg-config --cflags freetype2)
LDFLAGS = -shared -fPIC $(shell pkg-config --libs freetype2)
SRC = src/giko.c
OBJ = $(SRC:.c=.o)

ifeq ($(shell uname), Darwin)
    LIB_EXT = .dylib
else
    LIB_EXT = .so
endif

STATIC_TARGET = build/libgiko.a
SHARED_TARGET = build/libgiko$(LIB_EXT)
EXE_NAME = giko_cli
LINT_OPTS = BasedOnStyle: LLVM, IndentWidth: 4

all: $(SHARED_TARGET) $(STATIC_TARGET)

# Build shared library
$(SHARED_TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Build static library
$(STATIC_TARGET): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(SHARED_TARGET) $(STATIC_TARGET)

