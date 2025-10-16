CXX = g++
CC = gcc

CXXFLAGS = -O2 -Wall
CFLAGS = -O2 -Wall

MAIN_TARGET = rusage_analyzer

SRCS_CPP = $(wildcard *.cpp)
OBJS_CPP = $(SRCS_CPP:.cpp=.o)


DEMO_SRCS = $(wildcard demo/*.c)
DEMO_TARGETS = $(patsubst %.c,%,$(DEMO_SRCS))


all: $(MAIN_TARGET) $(DEMO_TARGETS)

$(MAIN_TARGET): $(OBJS_CPP)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

demo/%: demo/%.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(MAIN_TARGET) $(OBJS_CPP) $(DEMO_TARGETS)

.PHONY: all clean
