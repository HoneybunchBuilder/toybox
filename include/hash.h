#pragma once

#include "stdint.h"

/*
    Adapted from http://www.cse.yorku.ca/~oz/hash.html:
    This algorithm was created for sdbm (a public-domain reimplementation of
    ndbm) database library. it was found to do well in scrambling bits, causing
    better distribution of the keys and fewer splits. it also happens to be a
    good general hashing function with good distribution. the actual function is
    hash(i) = hash(i - 1) * 65599 + str[i]; what is included below is the faster
    version used in gawk. [there is even a faster, duff-device version] the
    magic constant 65599 was picked out of thin air while experimenting with
    different constants, and turns out to be a prime. this is one of the
    algorithms used in berkeley db (see sleepycat) and elsewhere.
*/
uint64_t tb_sdbm(uint64_t hash, const uint8_t *data, uint64_t len);
