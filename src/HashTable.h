/*
 * HashTable.h
 *
 *  Created on: May 18, 2016
 *      Author: yurujie
 */

#ifndef HASHTABLE_H_
#define HASHTABLE_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


#define NODE_POOL_APPEND_SIZE	64


#define HASH_TABLE_TYPE_DEF(vType, name) \
	typedef struct name##_hash_node { \
		uint64_t				key; \
		vType					value; \
		struct name##_hash_node	*next; \
	}name##_hash_node; \
	typedef struct name##_hash_table { \
		name##_hash_node		*table; \
		name##_hash_node		*nodePool; \
		name##_hash_node		*freeList; \
		int						tableSize; \
		int						poolSize; \
		int						freeLen; \
		int						nodeTypeSize; \
		pthread_mutex_t 		lock; \
	}name##_hash_table;

#define HASH_NODE_TYPE(name)	name##_hash_node
#define HASH_TABLE_TYPE(name)	name##_hash_table


typedef struct hash_table {
	void	*table;
	void	*nodePool;
	void	*freeList;
	int		tableSize;
	int		poolSize;
	int		freeLen;
	int		nodeTypeSize;
	pthread_mutex_t lock;
}hash_table;


int initHashTable(hash_table *hashTable, int nodeTypeSize, int tableSize);
void destroyHashTable(hash_table *hashTable);
int put(hash_table *hashTable, uint32_t (*hash_code)(uint64_t key), uint64_t key, void *value, int vLen);
int get(hash_table *hashTable, uint32_t (*hash_code)(uint64_t key), uint64_t key, void *value, int vLen);



#endif /* HASHTABLE_H_ */
