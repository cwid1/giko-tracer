CC = gcc
CFLAGS = -Wall
FILES = main.c
EXE_NAME = giko_tracer
LINT_OPTS = BasedOnStyle: LLVM, IndentWidth: 4
FREETYPE_FLAGS = $(shell pkg-config --cflags --libs freetype2)

LDFLAGS = $(FREETYPE_FLAGS) 

all:
	$(CC) $(CFLAGS) $(FILES) -o $(EXE_NAME) $(LDFLAGS)

asan:
	$(CC) $(CFLAGS) -fsanitize=address -g $(FILES) -o $(EXE_NAME) $(LDFLAGS)

prof:
	$(CC) $(CFLAGS) -pg $(FILES) -o $(EXE_NAME) $(LDFLAGS)

clean:
	rm $(EXE_NAME)

lint:
	clang-format -i -style='{$(LINT_OPTS)}' $(FILES)

