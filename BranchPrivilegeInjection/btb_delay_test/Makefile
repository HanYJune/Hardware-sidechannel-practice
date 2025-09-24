# Makefile

CC      := cc
CFLAGS  := -O0 -g -Wall -Wextra -fno-pie -no-pie
TARGET  := btb_delay
SRCS    := btb_delay.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) $(TARGET)
