ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

# Yeah, there's probably a better way than this -- I'll gladly accept a pull request, thank you!
BIN1=gust_pak
SRC1=${BIN1}.c util.c parson.c
OBJ1=${SRC1:.c=.o}
DEP1=${SRC1:.c=.d}

BIN2=gust_elixir
SRC2=${BIN2}.c util.c parson.c miniz_tinfl.c miniz_tdef.c
OBJ2=${SRC2:.c=.o}
DEP2=${SRC2:.c=.d}

BIN3=gust_g1t
SRC3=${BIN3}.c util.c parson.c
OBJ3=${SRC3:.c=.o}
DEP3=${SRC3:.c=.d}

BIN4=gust_enc
SRC4=${BIN4}.c util.c parson.c
OBJ4=${SRC4:.c=.o}
DEP4=${SRC4:.c=.d}

BIN5=gust_ebm
SRC5=${BIN5}.c util.c parson.c
OBJ5=${SRC5:.c=.o}
DEP5=${SRC5:.c=.d}

BIN=${BIN1}${EXE} ${BIN2}${EXE} ${BIN3}${EXE} ${BIN4}${EXE} ${BIN5}${EXE}
OBJ=${OBJ1} ${OBJ2} ${OBJ3} ${OBJ4} ${OBJ5}
DEP=${DEP1} ${DEP2} ${DEP3} ${DEP4} ${DEP5}

# -Wno-sequence-point because *dst++ = dst[-d]; is only ambiguous for people who don't know how CPUs work.
CFLAGS=-std=c99 -pipe -fvisibility=hidden -Wall -Wextra -Werror -Wno-sequence-point -Wno-unknown-pragmas -UNDEBUG -D_GNU_SOURCE -O2
ifeq ($(OS),Windows_NT)
LDFLAGS=-s -municode
else
LDFLAGS=-s -lm
endif

.PHONY: all clean

all: ${BIN}

clean:
	@${RM} ${BIN} ${OBJ} ${DEP}

${BIN1}${EXE}: ${OBJ1}
	@echo [L] $@
	@${CC} ${LDFLAGS} -o $@ $^

${BIN2}${EXE}: ${OBJ2}
	@echo [L] $@
	@${CC} ${LDFLAGS} -o $@ $^

${BIN3}${EXE}: ${OBJ3}
	@echo [L] $@
	@${CC} ${LDFLAGS} -o $@ $^

${BIN4}${EXE}: ${OBJ4}
	@echo [L] $@
	@${CC} ${LDFLAGS} -o $@ $^

${BIN5}${EXE}: ${OBJ5}
	@echo [L] $@
	@${CC} ${LDFLAGS} -o $@ $^

%.o: %.c
	@echo [C] $<
	@${CC} ${CFLAGS} -MMD -c -o $@ $<

-include ${DEP}
