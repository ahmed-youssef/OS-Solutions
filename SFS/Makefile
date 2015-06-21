CFLAGS = -c -Wall `pkg-config fuse --cflags --libs`
#CFLAGS+= -g
LDFLAGS = `pkg-config fuse --cflags --libs`

SOURCES= disk_emu.c sfs_api.c fuse_wrappers.c
#SOURCES= disk_emu.c sfs_api.c sfs_test.c

OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=sfs

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	gcc $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	gcc $(CFLAGS) $< -o $@

clean:
	rm -rf *.o *~ 
