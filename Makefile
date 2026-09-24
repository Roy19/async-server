CC ?= cc
CPPFLAGS += -Isrc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LDFLAGS ?=

TARGET := server
SOURCES := $(wildcard src/*.c)
OBJECTS := $(SOURCES:.c=.o)

.PHONY: all clean debug sanitize test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

debug: CFLAGS := -O0 -g3 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
debug: clean all

sanitize: CFLAGS := -O1 -g3 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -fsanitize=address,undefined
sanitize: LDFLAGS := -fsanitize=address,undefined
sanitize: clean all

test: $(TARGET)
	python3 tests/test_echo_server.py --server ./$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJECTS) $(OBJECTS:.o=.d)

-include $(OBJECTS:.o=.d)
