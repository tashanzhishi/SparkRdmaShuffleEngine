SHARE  := ./lib/libSparkRdma.so

CC      := gcc
AR      := ar
RANLIB  := ranlib
LDFLAGS :=
LIBS    := -lpthread -libverbs $(shell pkg-config --libs glib-2.0) $(shell pkg-config --libs gthread-2.0)
CFLAGS  := -Wall $(shell pkg-config --cflags glib-2.0) $(shell pkg-config --cflags gthread-2.0)

SRC_DIR := ./src
OBJ_DIR := ./obj
LIB_DIR := ./lib
SOURCES  := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst %.c, $(OBJ_DIR)/%.o, $(notdir $(SOURCES)))

#$(warning $(SOURCES))
#$(warning $(OBJS))


all : $(SHARE)

$(SHARE) : $(SOURCES)
        $(CC) $(CFLAGS) $(SOURCES) -fPIC -shared -o $(SHARE) $(LIBS)

install:
        cp $(SHARE) /usr/lib/

uninstall:
        rm -f /usr/lib/libsparkrdma.so

.PHONY : clean

clean :
        rm -f $(OBJS) $(SHARE)