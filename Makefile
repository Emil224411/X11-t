CC = gcc
LIBS = -lm -lX11 -lGLEW -lGL -lGLU

CFLAGS   = -g -pedantic -Wall -O0
#CFLAGS   = -pedantic -Wall -Wno-deprecated-declarations -Os
LDFLAGS  = ${LIBS}

DEFINES = -DPROG_DIR=\"$(abspath .)/\"

SRC = main.c fluid.c util.c
OBJ = ${SRC:.c=.o}

all: out

.c.o:
	${CC} -c ${DEFINES} ${CFLAGS} $<

${OBJ}: fluid.h util.h

out: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f ${OBJ} out

.PHONY: all clean
