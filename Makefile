CC = gcc

OBJDIR = obj

FREETYPELIBS = -lfontconfig -lXft
FREETYPEINC = /usr/include/freetype2

LIBS = -lm -lX11 -lGLEW -lGL -lGLU ${FREETYPELIBS}
INCS = -I${FREETYPEINC}

#CFLAGS   = -g -pedantic -Wall -O0 ${INCS}
CFLAGS   = -pedantic -Wall -Wno-deprecated-declarations -Ofast
LDFLAGS  = ${LIBS}

DEFINES = -DSHADER_DIR=\"$(abspath .)/shaders/\"

SRC = main.c util.c gpu.c cpu.c
OBJ = ${SRC:%.c=obj/%.o}

all: out

obj/%.o: %.c
	${CC} ${DEFINES} ${CFLAGS} -o $@ -c $<

${OBJ}: util.h gpu.h cpu.h

out: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f ${OBJ} out

.PHONY: all clean
