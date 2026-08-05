CC=gcc
CFLAGS=-Wall -g

SRCS=shelly.c lexer.c parser.c executor.c
OBJS=$(SRCS:.c=.o)
TARGET=mysh

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
