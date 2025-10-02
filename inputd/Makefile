CC = gcc

OBJDIR = obj

LIBS = 

CFLAGS   = -g -Wall -O0
#CFLAGS   = -Wall -Wno-deprecated-declarations -Ofast
LDFLAGS  = ${LIBS}

SRC = main.c
OBJ = ${SRC:%.c=obj/%.o}

all: out

obj/%.o: %.c
	${CC} ${DEFINES} ${CFLAGS} -o $@ -c $<

${OBJ}: 

out: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f ${OBJ} out

.PHONY: all clean
