#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include "../lib/packet_buffer.h"
#include <string.h>

// Función para mostrar el frame en la terminal.
void mostrar_frame(uint8_t* frame_data, /*size_t frame_size,*/ uint16_t video_width, uint16_t video_height) {
    // Obtiene el tamaño de la terminal.
    struct winsize terminal_size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal_size);
    uint16_t display_width = video_width+1;
    uint16_t display_height = video_height;
    uint8_t* cropped_frame_data = malloc(display_width * display_height); // Guardamos en un buffer el frame recortado
    
    // Ajustamos el tamaño si supera el del terminal
    if (video_width > terminal_size.ws_col) {
        display_width = terminal_size.ws_col;
    }

    if (video_height > terminal_size.ws_row) {
        display_height = terminal_size.ws_row;
    }

    // Copiamos los frames del original en el frame recortado para adaptar el video
    // en caso de que el terminal sea mas pequeño que el video.
    for (int y = 0; y < display_height; y++) {
        for (int x = 0; x < display_width; x++) {
            cropped_frame_data[y * display_width + x] = frame_data[y * (video_width+1) + x];
        }
    }

    // Códigos de control para limpiar la pantalla y mover el cursor a la esquina superior izquierda.
    const char* CURSOR_TOP_LEFT = "\033[H\033[J";

    // Limpia la pantalla y mueve el cursor.
    write(STDOUT_FILENO, CURSOR_TOP_LEFT, strlen(CURSOR_TOP_LEFT));

    // Muestra el frame recortado.
    write(STDOUT_FILENO, cropped_frame_data, display_width * display_height-1);
    if (display_height<terminal_size.ws_row){
        printf("\n");
    }
    // Liberamos la memoria adquirida anteriormente
    free(cropped_frame_data);
}

int frame_from_data(void* buffer, uint16_t* seq, uint8_t** frame_data, size_t* frame_size, bool* is_frame_complete, uint16_t* video_width, uint16_t* video_height, bool verbose, FILE* log_file) {
    // Verifica la validez de los argumentos.
   if (buffer == NULL || seq == NULL || frame_data == NULL || frame_size == NULL || is_frame_complete == NULL || video_width == NULL || video_height == NULL) {
        return -1;
    }

    *is_frame_complete = true;
    uint32_t timestamp, offset, accoffset = 0;
    uint16_t data_length;
    bool is_last_packet;
    size_t total_frame_size = 0;
    uint16_t current_seq = *seq;

    void* packet_data;
    do {
        packet_data = pbuf_retrieve(buffer, current_seq, &timestamp, &is_last_packet, &offset, video_width, video_height, &data_length);
        if (packet_data == NULL) {
            *is_frame_complete = false;
            break;
        }
        if(accoffset != offset){
            *is_frame_complete = false;
            break;
        }
        accoffset+=data_length;
        total_frame_size += data_length;
        current_seq++;
    } while (!is_last_packet);

    if (!*is_frame_complete) {
        *seq = current_seq;
        return -1;
    }

    *frame_data = malloc(total_frame_size);
    if (*frame_data == NULL) {
        return -1;
    }

    *frame_size = total_frame_size;
    current_seq = *seq;
    size_t copied_data = 0;
    do {
        packet_data = pbuf_retrieve(buffer, current_seq++, &timestamp, &is_last_packet, &offset, video_width, video_height, &data_length);
        // Registro en el fichero de log
        if (verbose && log_file) {
            fprintf(log_file, is_last_packet ? "#" : "=");
        }
        memcpy(*frame_data + copied_data, packet_data, data_length);
        copied_data += data_length;
    } while (!is_last_packet);
    *seq = current_seq-1;
    return 0;
}

int play_frame_custom(void* buffer, uint16_t* seq, bool verbose, FILE* log_file) {
    uint8_t* frame_data = NULL;
    size_t frame_size = 0;
    uint16_t video_width = 0, video_height = 0;
    bool is_frame_complete = false;

    int status = frame_from_data(buffer, seq, &frame_data, &frame_size, &is_frame_complete, &video_width, &video_height, verbose, log_file);
    if (status != 0 || !is_frame_complete) {
        free(frame_data);
        return -1;
    }

    mostrar_frame(frame_data,/* frame_size,*/ video_width, video_height);

    free(frame_data);
    return 0;
}
