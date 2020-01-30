#ifndef _RH_HASHVIEW_
#define _RH_HASHVIEW_

#include <iostream>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <random>

/**
 * Common code for both MFC and X11 versions
 */

#define BITS_IN_BYTE 8

std::vector<bool> bytesToBools(const uint8_t* arr, size_t byteSize) {
    std::vector<bool> result(8*byteSize,false);
    for(int i = byteSize - 1; i >= 0; --i) {
        for(int bit = 0; bit < BITS_IN_BYTE; ++bit) {
            result[i * BITS_IN_BYTE + bit] = ((arr[i] >> bit) & 1);
        }
    }
    return result;
}

int getBitSeqFromBooleanArray(int bitIndex, int bitLength, const std::vector<bool>& bb) {
    int a = 0, m = 1;

    int i = bitIndex+bitLength-1;
    while(i>=bitIndex) {
        a += bb[i]?m:0;
        m <<= 1;
        i--;
    }

    return a;
}
#endif /* _RH_HASHVIEW_ */