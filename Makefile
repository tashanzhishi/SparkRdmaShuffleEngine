#STATIC := libspark_rdma.a
SHARE   := ./lib/libspark_rdma.so

CC      := gcc
AR      := ar
RANLIB  := ranlib
LDFLAGS := 
LIBS    := -lpthread $(shell pkg-config --libs glib-2.0) $(shell pkg-config --libs gthread-2.0)
CFLAGS  := -Wall $(shell pkg-config --cflags glib-2.0) $(shell pkg-config --cflags gthread-2.0)
SHARE_FLAGS := -fPIC -shared -o

SRC_DIR := ./src
OBJ_DIR := ./obj
LIB_DIR := ./lib
SOURCES  := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst %.c, $(OBJ_DIR)/%.o, $(notdir $(SOURCES)))

#$(warning $(SOURCES))
#$(warning $(OBJS))


all : $(SHARE)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -fpic -c $< -o $@ $(LDFLAGS) $(LIBS)

#$(STATIC) : $(OBJS)
#	$(AR) cru $(STATIC) $(OBJS)
#	$(RANLIB) $(TARGET)

$(SHARE) : $(OBJS)
	gcc -shared -o $@ $(OBJS)
#	$(CC) $(CFLAGS) $(SHARE_FLAGS) $@ $(OBJS) $(LDFLAGS) $(LIBS)

.PHONY : clean

clean :
	rm -f $(OBJS) $(SHARE) $(STATIC)
