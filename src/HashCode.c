/*
 * HashCode.c
 *
 *  Created on: May 18, 2016
 *      Author: yurujie
 */

#include "HashCode.h"

uint32_t strHash(char *str) {

	if (!str) {
		return 0;
	}

	register uint32_t hash = 5381;
    while (*str)
    {
        hash = hash * 33 ^ (uint32_t)*str;
        str++;
    }

    return hash;
}

uint32_t ipHash(uint64_t addr) {

	return (addr >> 24) | ((addr >> 16) & 0x0FF);
}

uint32_t ptrHash(uint64_t ptr) {

	return (uint32_t)(ptr & 0x0000FFFF);
}
