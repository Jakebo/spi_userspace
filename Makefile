CC     := ${CROSS_COMPILE}gcc
TARGET := spi_adc
SRC    := ${TARGET}.c

all: ${TARGET}

${TARGET}: ${SRC}
	${CC} -o ${TARGET} ${INC} ${SRC}

clean:
	@rm -rf ${TARGET}
