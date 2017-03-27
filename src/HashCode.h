/*
 * HashCode.h
 *
 *  Created on: May 18, 2016
 *      Author: yurujie
 */

#ifndef HASHCODE_H_
#define HASHCODE_H_

#include "stdint.h"

uint32_t strHash(char *str);
uint32_t ipHash(uint64_t addr);
uint32_t ptrHash(uint64_t ptr);

#endif /* HASHCODE_H_ */
