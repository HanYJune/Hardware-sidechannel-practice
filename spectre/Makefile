# Makefile

CC      := cc
CFLAGS  := -O0 -g -Wall -Wextra 
TARGET  := spectrev1
SRCS    := spectrev1.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) $(TARGET)
