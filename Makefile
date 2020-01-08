
# Configuration variables
CC = gcc
CFLAGS  = -fPIC -fopenmp -Wall -O3
LDFLAGS = -shared -ldl -fPIC
#To puild PGOMP with PAPI BUILD_PAPI must be Yes
BUILD_PAPI = Yes 
RM = rm -f
IFLAGS=
ifeq ($(BUILD_PAPI), Yes )
        CFLAGS+=-DBUILD_PAPI
        IFLAGS += -I/Tools/papi-4.2.0/src/ /Tools/papi-4.2.0/src/libpapi.so
endif

# Target library name and version
TARGET = libpgomp
VERSION = 0.1

OBJECTS = pgomp.o

all: $(TARGET).so.$(VERSION) test

$(TARGET).so.$(VERSION): $(OBJECTS)
	$(CC) $(LDFLAGS) -Wl,-soname,$(TARGET).so -o $(TARGET).so.$(VERSION) -ldl $(OBJECTS) $(IFLAGS)

test: test.o
	$(CC) -o $@ $^ -lgomp 

clean:
	$(RM) $(TARGET).so.$(VERSION) $(OBJECTS) test test.o

pgomp.o: config.h

#
# Useless stuff: played with -Wl,--export-dynamic on the test
# program to see if dladdr() would work, but it didn't for me (JEC)
#
