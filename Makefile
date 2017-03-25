CFLAGS=-std=c99 -O3 -g -pedantic-errors -Wall -pthread
LFLAGS=-Wall -lpthread
ARFLAGS=-rcs

SRC_DIR=src/
BUILD_DIR=build/
TESTS_DIR=tests/
EXAMPLES_DIR=examples/

CORE_OBJS=$(SRC_DIR)upool.o
TESTS_OBJS=$(TESTS_DIR)tests.o
EXAMPLE_OBJ=$(EXAMPLES_DIR)example.o
EXAMPLE_SIMPLE_OBJ=$(EXAMPLES_DIR)simple_example.o
EXAMPLE_OBJS=$(EXAMPLE_OBJ) $(EXAMPLE_SIMPLE_OBJ)

LIB_OUTPUT=$(BUILD_DIR)libupool.a
TESTS_OUTPUT=$(BUILD_DIR)tests
EXAMPLE_OUTPUT=$(BUILD_DIR)example
EXAMPLE_SIMPLE_OUTPUT=$(BUILD_DIR)simple_example

%.o: %.c %.h
	$(CC) -c $(CFLAGS) -o $@ $<

all: lib examples tests

lib: $(CORE_OBJS)
	$(AR) $(ARFLAGS) $(LIB_OUTPUT) $(CORE_OBJS)

examples: $(CORE_OBJS) $(EXAMPLE_OBJS)
	$(CC) $(LFLAGS) -o $(EXAMPLE_OUTPUT) $(CORE_OBJS) $(EXAMPLE_OBJ); \
	$(CC) $(LFLAGS) -o $(EXAMPLE_SIMPLE_OUTPUT) $(CORE_OBJS) $(EXAMPLE_SIMPLE_OBJ)

tests: $(CORE_OBJS) $(TESTS_OBJS)
	$(CC) $(LFLAGS) -o $(TESTS_OUTPUT) $(TESTS_OBJS)

clean:
	rm $(LIB_OUTPUT) \
        $(CORE_OBJS) \
        $(TESTS_OBJS) \
        $(TESTS_OUTPUT) \
        $(EXAMPLE_OBJS) \
        $(EXAMPLE_OUTPUT) \
        $(EXAMPLE_SIMPLE_OUTPUT)
