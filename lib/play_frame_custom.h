#ifndef PLAY_FRAME_CUSTOM_H
#define PLAY_FRAME_CUSTOM_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

void mostrar_frame(uint8_t* frame_data,  uint16_t video_width, uint16_t video_height);

int frame_from_data(void* buffer, uint16_t* seq, uint8_t** frame_data, size_t* frame_size, bool* is_frame_complete, uint16_t* video_width, uint16_t* video_height, bool verbose, FILE* log_file);

int play_frame_custom(void* buffer, uint16_t* seq, bool verbose, FILE* log_file);

#endif 
