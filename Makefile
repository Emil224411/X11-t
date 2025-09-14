CC = gcc
LIBS = -lX11 -lm

CFLAGS   = -g -pedantic -Wall -O0 ${INCS} ${CPPFLAGS}
#CFLAGS   = -pedantic -Wall -Wno-deprecated-declarations -Os ${INCS} ${CPPFLAGS}
LDFLAGS  = ${LIBS}


SRC = main.c
OBJ = ${SRC:.c=.o}

all: out

.c.o:
	${CC} -c ${CFLAGS} $<

#${OBJ}: 

out: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f ${OBJ} out

.PHONY: all clean
