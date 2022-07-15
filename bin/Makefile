PREFIX  ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS += -std=c11 -Os -g -pedantic -Wall -I../lib 
LDLIBS = -lcolumnix

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
BIN = $(basename $(SRC))

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $@.o $(LDLIBS)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(BIN) $(OBJ)

install: $(BIN)
	install -m 755 $(BIN) $(BINDIR)

uninstall:
	$(RM) $(addprefix $(BINDIR)/,$(BIN))

.PHONY: clean install uninstall
