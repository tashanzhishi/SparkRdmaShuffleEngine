STATIC  := ./lib/libspark_rdma.a

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


#all : $(SHARE)
all : $(STATIC)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
#	$(CC) $(CFLAGS) -fpic -c $< -o $@ $(LDFLAGS) $(LIBS)
	$(CC) $(CFLAGS) -c $< -o $@ $(LDFLAGS) $(LIBS)

$(STATIC) : $(OBJS)
	$(AR) cru $(STATIC) $(OBJS)
	$(RANLIB) $(STATIC)

#$(SHARE) : $(OBJS)
#	gcc -shared -o $@ $(OBJS)
#	$(CC) $(CFLAGS) $(SHARE_FLAGS) $@ $(OBJS) $(LDFLAGS) $(LIBS)

install:
#	cp $(SHARE) /usr/lib/
	cp $(STATIC) /usr/lib/

uninstall:
#	rm -f /usr/lib/libspark_rdma.so
	rm -f /usr/lib/libspark_rdma.a

.PHONY : clean

clean :
	rm -f $(OBJS) $(SHARE) $(STATIC)
