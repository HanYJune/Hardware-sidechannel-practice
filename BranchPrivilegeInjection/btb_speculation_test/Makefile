CC       := gcc
CFLAGS   := -g -O0 -fno-pie
LDFLAGS  := -no-pie
CPPFLAGS := -I../kmod_ap

TARGET := btb_speculation_test
SRCS   := btb_speculation_test.c
OBJS   := $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

