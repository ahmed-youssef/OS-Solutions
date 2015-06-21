CFLAGS = -c -Wall
CFLAGS+= -g
LDFLAGS = 

SOURCES= malloc_api.c Test.c

OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=my_malloc

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	gcc $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	gcc $(CFLAGS) $< -o $@

clean:
	rm -rf *.o *~ 
