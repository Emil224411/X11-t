CC = gcc
LIBS = -lm -lX11 -lGL -lGLU

#CFLAGS   = -g -pedantic -Wall -O0
CFLAGS   = -pedantic -Wall -Wno-deprecated-declarations -Os
LDFLAGS  = ${LIBS}


SRC = main.c fluid.c
OBJ = ${SRC:.c=.o}

all: out

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: fluid.h

out: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f ${OBJ} out

.PHONY: all clean
