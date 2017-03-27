/*
 * HashTable.c
 *
 *  Created on: May 18, 2016
 *      Author: yurujie
 */

#include "HashTable.h"
#include "RdmaCommon.h"


static inline void* getNext(void *hashNode, int nodeTypeSize);
static inline void** getNextPtr(void *hashNode, int nodeTypeSize);
//static int initNodePool(hash_table *hashTable);
//static void destroyNodePool(hash_table *hashTable);
//static int appendNodePool(hash_table *hashTable);
//static void* getNodeFromPool(hash_table *hashTable);
//static void returnNodeToPool(hash_table *hashTable, void *node);


int initHashTable(hash_table *hashTable, int nodeTypeSize, int tableSize) {

	memset(hashTable, 0, sizeof(hash_table));
	hashTable->table = malloc(nodeTypeSize * tableSize);
	hashTable->tableSize = tableSize;
	hashTable->nodeTypeSize = nodeTypeSize;

	memset(hashTable->table, 0, nodeTypeSize * tableSize);

	int i = 0;
	for (i = 0; i < tableSize; i++) {
		*((uint64_t *)(hashTable->table + nodeTypeSize * i)) = -1;
	}

	pthread_mutex_init(&hashTable->lock, 0);

	// return initNodePool(hashTable);
	return 0;
}

void destroyHashTable(hash_table *hashTable) {

	// destroyNodePool(hashTable);

	if (hashTable->table) {
		free(hashTable->table);
		hashTable->table = NULL;
		hashTable->tableSize = 0;
	}

	hashTable->freeList = NULL;
	hashTable->freeLen = 0;

	pthread_mutex_destroy(&hashTable->lock);
}

int put(hash_table *hashTable, uint32_t (*hash_code)(uint64_t key), uint64_t key, void *value, int vLen) {

	uint32_t hash;
	void *node, *tmp, *pre = NULL;
	int first = 0;

	hash = hash_code(key);
	hash %= hashTable->tableSize;
	node = hashTable->table + hashTable->nodeTypeSize * hash;

	rdma_debug("put: key = %d, hash = %d, node = %016p\n", key, hash, node);

	while (node) {
		pre = node;
		rdma_debug("node = %016p\n", node);
		if (*((uint64_t *)node) == -1
				|| *((uint64_t *)node) == key) {
			first = 1;
			break;
		}
		node = getNext(node, hashTable->nodeTypeSize);
	}

	if (first) {
		rdma_debug("1: pre = %016p\n", pre);
		*((uint64_t *)pre) = key;
		memcpy(pre + sizeof(uint64_t), value, vLen);
		//rdma_debug("pre = 0x%016x\n", pre);
	}
	else {
		rdma_debug("2: pre = %016p\n", pre);
		//tmp = getNodeFromPool(hashTable);
		tmp = malloc(hashTable->nodeTypeSize);
		*(uint64_t *)tmp = key;
		memcpy(tmp + sizeof(uint64_t), value, vLen);
		*getNextPtr(tmp, hashTable->nodeTypeSize) = NULL;
		pthread_mutex_lock(&hashTable->lock);
		*getNextPtr(pre, hashTable->nodeTypeSize) = tmp;
		pthread_mutex_unlock(&hashTable->lock);
	}

	return 0;
}

int get(hash_table *hashTable, uint32_t (*hash_code)(uint64_t key), uint64_t key, void *value, int vLen) {

	uint32_t hash;
	void *node;

	hash = hash_code(key);
	if (hashTable->tableSize <= 0) {
		rdma_debug("hash table not initialized.\n");
		return 0;
	}
	hash %= hashTable->tableSize;
	node = hashTable->table + hashTable->nodeTypeSize * hash;

	rdma_debug("get: key = %d, hash = %d, node = %016p\n", key, hash, node);

	while (node) {
		if (*((uint64_t *)node) == key) {
			rdma_debug("node = %016p\n", node);
			memcpy(value, node + sizeof(uint64_t), vLen);
			break;
		}
		node = getNext(node, hashTable->nodeTypeSize);
	}

	if (!node) {
		return 0;
	}

	return 1;
}

int delete(hash_table *hashTable, uint32_t (*hash_code)(uint64_t key), uint64_t key) {

	uint32_t hash;
	void *node;

	hash = hash_code(key);
	hash %= hashTable->tableSize;
	node = hashTable->table + hashTable->nodeTypeSize * hash;

	return 0;
}

static inline void* getNext(void *hashNode, int nodeTypeSize) {

	rdma_debug("%016p\n", hashNode + nodeTypeSize - sizeof(void *));
	return *(void **)(hashNode + nodeTypeSize - sizeof(void *));
}

static inline void** getNextPtr(void *hashNode, int nodeTypeSize) {

	return (void**)(hashNode + nodeTypeSize - sizeof(void *));
}

/*
static int initNodePool(hash_table *hashTable) {

	return appendNodePool(hashTable);
}*/

/*
static void destroyNodePool(hash_table *hashTable) {

	pthread_mutex_lock(&hashTable->lock);
	if (hashTable->nodePool) {
		free(hashTable->nodePool);
		hashTable->nodePool = NULL;
		hashTable->poolSize = 0;
	}
	pthread_mutex_unlock(&hashTable->lock);
}*/

/*
static int appendNodePool(hash_table *hashTable) {

	int i;

	pthread_mutex_lock(&hashTable->lock);
	if (!hashTable->nodePool) {
		hashTable->nodePool = malloc(hashTable->nodeTypeSize * NODE_POOL_APPEND_SIZE);
	}
	else {
		hashTable->nodePool = realloc(hashTable->nodePool,
				hashTable->nodeTypeSize * (hashTable->poolSize + NODE_POOL_APPEND_SIZE));
	}

	if (!hashTable->nodePool) {
		pthread_mutex_unlock(&hashTable->lock);
		return -1;
	}

	for (i = hashTable->poolSize; i < hashTable->poolSize + NODE_POOL_APPEND_SIZE - 1; i++) {
		*getNextPtr(hashTable->nodePool + i * hashTable->nodeTypeSize, hashTable->nodeTypeSize)
				= hashTable->nodePool + (i + 1) * hashTable->nodeTypeSize;
	}

	*getNextPtr(hashTable->nodePool + i * hashTable->nodeTypeSize, hashTable->nodeTypeSize) = hashTable->freeList;
	hashTable->freeList = hashTable->nodePool + hashTable->poolSize * hashTable->nodeTypeSize;
	pthread_mutex_unlock(&hashTable->lock);

	hashTable->poolSize += NODE_POOL_APPEND_SIZE;
	hashTable->freeLen += NODE_POOL_APPEND_SIZE;

	return 0;
}*/

/*
static void* getNodeFromPool(hash_table *hashTable) {

	void *node;

	if (!hashTable->freeList) {
		appendNodePool(hashTable);
	}

	pthread_mutex_lock(&hashTable->lock);
	node = hashTable->freeList;
	hashTable->freeList = getNext(node, hashTable->nodeTypeSize);
	*getNextPtr(node, hashTable->nodeTypeSize) = NULL;
	hashTable->freeLen --;
	pthread_mutex_unlock(&hashTable->lock);

	return node;
}*/

/*
static void returnNodeToPool(hash_table *hashTable, void *node) {

	pthread_mutex_lock(&hashTable->lock);
	*getNextPtr(node, hashTable->nodeTypeSize) = hashTable->freeList;
	hashTable->freeList = node;
	hashTable->freeLen ++;
	pthread_mutex_unlock(&hashTable->lock);
}
*/
