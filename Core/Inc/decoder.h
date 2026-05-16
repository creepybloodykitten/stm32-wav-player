#ifndef DECODER_H
#define DECODER_H

#include "fatfs.h"
#include <stdint.h>

typedef struct {
    uint32_t sample_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    uint32_t total_samples; 
		char title[80];  
    char artist[80];
} AudioInfo;


typedef struct Decoder {
    const char* name; 
    int (*can_handle)(const char* filename);
    int (*open)(FIL* file, AudioInfo* info);
    uint32_t (*decode)(void* pcm_buffer, uint32_t bytes_to_decode);
    void (*close)(void);
} Decoder;

#endif // DECODER_H