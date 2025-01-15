CC = gcc
CFLAGS = -Iinclude -Wall -Wextra 
FT_CFLAGS = $(shell pkg-config --cflags freetype2)
LDFLAGS = -shared -fPIC $(shell pkg-config --libs freetype2)
SRC = src/giko.c
OBJ = $(SRC:.c=.o)

ifeq ($(shell uname), Darwin)
    LIB_EXT = .dylib
else
    LIB_EXT = .so
endif

BUILD_DIR = build
STATIC_TARGET = $(BUILD_DIR)/libgiko.a
SHARED_TARGET = $(BUILD_DIR)/libgiko$(LIB_EXT)
LINT_OPTS = BasedOnStyle: LLVM, IndentWidth: 4
EXE_SRC = src/cli.c
EXE_NAME = giko-trace

all: $(SHARED_TARGET) $(STATIC_TARGET)

# Build shared library
$(SHARED_TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Build static library
$(STATIC_TARGET): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(FT_CFLAGS) -c $< -o $@

giko-trace:
	$(CC) -fsanitize=address -g -Iinclude -L$(BUILD_DIR) -lgiko $(EXE_SRC) -o $(EXE_NAME)

clean:
	rm -f $(OBJ) $(SHARED_TARGET) $(STATIC_TARGET)

