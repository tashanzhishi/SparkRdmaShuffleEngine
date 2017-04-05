CC = gcc
CFLAGS = -g -Wall
LIBS = -lpthread -libverbs

objects = rdma_log.o rdma_buffer_pool.o rdma_utils.o test_rdma_buffer_pool.o

a.out: $(objects)
	cc $(CFLAGS) -o a.out $(objects) $(LIBS)

$(objects): rdma_log.h
rdma_buffer_pool.o rdma_utils.o test_rdma_buffer_pool.o: rdma_buffer_pool.h
rdma_utils.o test_rdma_buffer_pool.o: rdma_utils.h

.PHONY: clean
clean:
	rm -f a.out $(objects)
