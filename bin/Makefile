PREFIX  ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS += -std=c11 -Os -g -pedantic -Wall -I../lib 
LDLIBS = -lcolumnix
LDFLAGS = 

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
BIN = $(basename $(SRC))

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $< $(LDLIBS)

# $@指代当前目标，就是Make命令当前构建的那个目标
# $< 指代第一个前置条件。比如，规则为 t: p1 p2，那么$< 就指代p1。

%.o : %.c
	${CC} ${CFLAGS} -c -o $@ $<

all: $(BIN)
	@echo "source code: "$(SRC)
	@echo "object files: "$(OBJ)
	@echo "binary files: "$(BIN)

clean:
	$(RM) $(BIN) $(OBJ)

install: $(BIN)
	install -m 755 $(BIN) $(BINDIR)

uninstall:
	$(RM) $(addprefix $(BINDIR)/,$(BIN))

.PHONY: clean install uninstall all
