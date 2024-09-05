CC = clang
CFLAGS = -Wall -g -fsanitize=address
FILES = main.c
EXE_NAME = ascii_tracer
LINT_OPTS = BasedOnStyle: LLVM, IndentWidth: 4
FREETYPE_FLAGS = `pkg-config --cflags --libs freetype2`

LDFLAGS = $(FREETYPE_FLAGS) 

all:
	$(CC) $(CFLAGS) $(FILES) -o $(EXE_NAME) $(LDFLAGS)

clean:
	rm $(EXE_NAME)

lint:
	clang-format -i -style='{$(LINT_OPTS)}' $(FILES)

