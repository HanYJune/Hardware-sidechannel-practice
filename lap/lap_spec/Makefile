NDK ?= ~/android-ndk-r27c
API ?= 28
CC  := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang

CFLAGS  := -std=c11 -O2 -fPIE -D__ANDROID_API__=$(API) -fno-strict-aliasing -I. -Iinclude
LDFLAGS := -pie
LIBS    := -llog    # __android_log_print 사용 시

# 필요한 .c 전부 나열 (경로는 프로젝트 구조에 맞게 조정)
SRC := speculation_test.c linked_list_utils.c eviction.c timer_tool.c
OBJ := $(SRC:.c=.o)

all: speculation_test

speculation_test: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) speculation_test
