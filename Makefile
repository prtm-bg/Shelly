CC=gcc
CFLAGS=-Wall -g -Iinclude

SRCS=$(wildcard src/*.c) $(wildcard src/builtins/*.c)
OBJS=$(SRCS:.c=.o)
TARGET=shelly

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
