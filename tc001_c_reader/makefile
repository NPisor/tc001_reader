# Makefile in tc001_c_reader/

CC      := gcc
SRC     := reader.c image_writer.c
TARGET  := reader

# CURDIR is auto-set by Make to the absolute path of this directory
CFLAGS  := -std=c11 -Wall -Wextra -O2 \
           -I"$(CURDIR)/include"
LDFLAGS := -L"$(CURDIR)/lib" -lusb-1.0

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)
