/* compile with PRINT_MISSING_PACKETS to activate this function in play_frame */
/*
%%%%%%%% play_frame_custom %%%%%%%%%%%%%%%%%%%%%%%%%
gcc -DPRINT_MISSING_PACKETS=1 -Wshadow -Wpedantic -Wall -Wextra -Wstrict-overflow -fno-strict-aliasing -o vitextclient vitextclient.c ../lib/packet_buffer.o ../lib/configure_sockets.c ../lib/play_frame.c 
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

gcc -DPRINT_MISSING_PACKETS=1 -Wshadow -Wpedantic -Wall -Wextra -Wstrict-overflow -fno-strict-aliasing -o vitextclient_template vitextclient_template.c ../lib/packet_buffer.o ../lib/configure_sockets.c ../lib/play_frame.o
*/

//#pragma scalar_storage_order big-endian
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <math.h>


#include "../lib/packet_buffer.h"
#include "../lib/configure_sockets.h"
#include "../lib/rtp.h"
#include "../lib/vtx_rtp.h"

#include "../lib/play_frame.h"
// #include "../lib/play_frame_custom.h"

#define MAX_PACKET_SIZE 1500
#define MAX_VITEXT_PAYLOAD_SIZE 1452

#define WAIT_FIRST 0
#define FIRST_PACKET 1
#define PLAY_BUFF 2
#define PLAY_EMPTY_BUFF 3


void get_arguments(int argc, char *argv[], struct in_addr *remote_ip, bool *is_multicast, int *port, uint32_t *ssrc, uint32_t *buffering_ms, bool *verbose, char *log_filename) {
    #define HELP printf("\nvitextclient [-h] [-b buffering] [-l log_filename] [-p port] [-s ssrc]  IP_ADDRESS\n" \
    "This implementation sends RTCP bye when the transmission ends. It also stops if it receives and RTCP bye from the client.\n\n" \
    "[-h] to show this help\n" \
    "[-b buffering] to set the buffering in ms (default 100)\n" \
    "[-p port] to set the port\n" \
    "[-l log_filename] to set the log filename\n" \
    "[-s ssrc] to set the ssrc of the client (only relevant if the client sends any RTCP message)\n" \
    "\nIP_ADDRESS is the multicast address in which it listens\n" \
    );

    // default values
    *remote_ip = (struct in_addr) {0};
    *is_multicast = false;
    *port = 5004;
    *log_filename = '\0';
    *buffering_ms = 100;
    *ssrc = 1;
    *verbose = false;

    int opt;
    while ((opt = getopt(argc, argv, "hvp:s:l:b:")) != -1) {
        switch (opt) {
            case 'h':
                HELP;
                break;
            case 'v':
                *verbose = true;
                break;
            case 'p':
                *port = (int) strtol(optarg, NULL, 10);
                if (*port < 1) {
                    fprintf(stderr, "Port must be a positive number\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 's':
                *ssrc = (uint32_t) strtol(optarg, NULL, 10);
                break;

            case 'b':
                *buffering_ms = (uint32_t) strtol(optarg, NULL, 10);
                if (*buffering_ms < 1) {
                    fprintf(stderr, "Buffering must be a positive number\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'l':
                if (strlen(optarg) <=255 )
                {
                    strcpy(log_filename, optarg);
                }
                else {
                    fprintf(stderr, "Log filename too long, must be up to 255\n");
                    exit(EXIT_FAILURE);
                }
                *verbose = true;
                break;
            case '?':

            default:
                HELP;
                exit(EXIT_FAILURE);
        }
    }
    if (optind < argc) {
        int res = inet_pton(AF_INET, argv[optind], remote_ip );
        if (res < 1) {
            printf("\nInternet address string not recognized\n");
            exit(EXIT_FAILURE);
        }
        if (IN_CLASSD(ntohl(remote_ip ->s_addr))) {
            *is_multicast = true;
        }
        else {
            *is_multicast = false;
        }
    }
    // ensure an address has been read in remote_ip

    if (remote_ip ->s_addr == 0) {
        HELP;

        fprintf(stderr, "\n\nNo remote address given\n\n");
        exit(EXIT_FAILURE);
    }
}

void free_resources (void *buffer, FILE *log_file, bool verbose){
    pbuf_destroy(buffer);
    if (verbose) {
      fclose(log_file);
    }
    // for (int i=0;i<num_resources;i++)
    //     free(dynamic_resources[i]);
    // free(dynamic_resources);
}

/* Read es la cantidad de bytes de la cabecera RTP+frame*/
uint16_t packet_to_data(uint32_t* packet_buffer, int read, uint8_t* data_buffer){
    uint16_t data_bytes = read-20; // 12 de rtp y 8 de vtxt
    uint16_t data_entries = data_bytes/4; // Número de uint32 que hay, redondea hacia abajo.
    if (data_entries*4!=data_bytes) // Si la división no es entera añade 1 (=3.4 --> 4)
        data_entries++;
    for (int i=0;i<data_bytes;i++){ // Esta línea se encarga de extraer los bytes individuales de una secuencia de datos de 32 bits (cada uint32_t en packet_buffer) y colocarlos en un arreglo de bytes (data_buffer).
        //printf("Saving in data position %d packet buffer position %d with %d shift\n",i,5+i/4,24-8*(i%4));
        data_buffer[i] = (uint8_t) (ntohl(packet_buffer[5+i/4])>>(24-8*(i%4)));
    }
    return data_bytes;
}


int main(int argc, char *argv[]) {
    // default values are assigned in get_arguments
    //u_int32_t a = 3;

    // printf("Starting program: %u\n",(a<<31)>>31);

    struct in_addr remote_ip;
    bool is_multicast;
    int port;
    char log_filename[256];
    uint32_t buffering_ms;
    bool verbose;
    uint32_t ssrc;

    // uint8_t **dyn_res = (uint8_t **) calloc(20,sizeof (uint8_t *));
    // if (dyn_res == NULL){
    //     printf("Error allocating memory, error: %s", strerror(errno));
    //     exit(EXIT_FAILURE);
    // }
    // int num_res = 0;

    get_arguments(argc, argv, &remote_ip, &is_multicast, &port, &ssrc, &buffering_ms, &verbose, log_filename);

    // config_sockets from configure_sockets.h
    int socket_RTP, socket_RTCP;
    struct sockaddr_in remote_RTP, remote_RTCP;

    configure_sockets(&socket_RTP, &socket_RTCP, remote_ip, is_multicast, port, &remote_RTP, &remote_RTCP);


    /*************************/
    /* gets signal_fd and blocks signals, so that they will be processed inside the select
    Install it before buffer creation, so that it always enters select and exits through the appropriate code section freeing the memory */
    sigset_t sigInfo;
    sigemptyset(&sigInfo);
    sigaddset(&sigInfo, SIGINT);
    sigaddset(&sigInfo, SIGALRM);
    sigaddset(&sigInfo, SIGTERM);

    int signal_fd = signalfd(-1, &sigInfo, 0);
    if (signal_fd < 0) {
        printf("Error getting file descriptor for signals, error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // block SIGINT signal with sigprocmask
    if (sigprocmask(SIG_BLOCK, &sigInfo, NULL) < 0) {
        printf("Error installing signal, error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /**************************/

    // Create the log file
    FILE *log_file;
    uint32_t num_packets = 0;

    // traza[0] = '\0';
    if (verbose) {
        log_file = fopen(log_filename, "w");
        if (log_file == NULL) {
            fprintf(stderr, "Error creating the log file\n");
            exit(EXIT_FAILURE);
        }

        /* Put log_file in fully buffered mode, so that it writes to file only when it has a lot of data, or at the end */
        if (setvbuf(log_file, NULL, _IOFBF, 0) < 0) {
            perror("setvbuf");
            exit(EXIT_FAILURE);
        }

        fprintf(log_file, "Remote IP: %s, port %d, ssrc %d, buffering %dms, log_filename %s\n", inet_ntoa(remote_ip), port, ssrc, buffering_ms, log_filename);
        }


    // Create buffer,
    // Asumimos 30 fps
    size_t buffer_size;
    buffer_size = (size_t) (((double)(buffering_ms))/1000 * 30 * 25); //[Tiempo de buffering] * [FPS] * [Tamaño máximo de frame = 32768/máximo payload de paquete = 1452] (22.5 aprox)
                                                                        /* Por un lado:
                                                                             - Número de frames (1 sec) = Tiempo de buffering (sec) * FPS (frames/second)
                                                                             - Máximo paquetes rtp por frame = Tamaño máx frame / Máximo payload de paquete
                                                                               Resultado = Número de frames que quieres en el buffer*/

    void *buffer = pbuf_create(buffer_size,MAX_PACKET_SIZE);
    if(buffer == NULL){
        printf("Error allocating buffer, error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }


    unsigned int state = WAIT_FIRST;
    fd_set lectura;
    int res, read;
    struct timeval timer,momento_timer,momento_actual,momento_primer_frame;
    struct timeval inicio_reproduccion, fin_reproduccion, duracion_teoria, duracion_real;
    struct timeval *select_timer;
    uint32_t buffer_paquetes[375]; // Numero de uint32 que caben en el buffer

    uint8_t buffer_data[1480]; // 1500 - 20 bytes de ip


    rtp_hdr_t header_rtp;
    rtp_vtx_frame_header_t header_vtx;
    rtcp_t header_rtcp;
    int insert = 0, frames_played = 0, frames_missed = 0, late_packets = 0;
    bool first_packet_arrived = false,first_frame_played = false;// ,have_complete_frame = false;

    uint32_t lowest_ts = 0, first_ts_played = 0,dummy_ts = 0; //,last_ts_checked = 0;
    uint16_t min_seq_n_buffered = 0, max_seq_n_buffered = 0,lowest_seq_n = 0,last_seq_n_played = 0, temp;

    // printf("El tamaño es: %li",sizeof(mreq_ipv4));
    
    // uint32_t dummy_timestamp;
    // bool is_last_packet;
    // uint32_t dummy_offset,expected_offset = 0; // vtx_rtp format
    // uint16_t dummy_video_width;
    // uint16_t dummy_video_height;
    // uint16_t buffered_data_length;
    // void *retrieval;
    //int k =0;
    // Create loop that receives packets and inserts them in the buffer, read from the buffer and plays them
    while(true){
        
        /*--------------------------------PREPARACIÓN DEL SELECT---------------------------------------------*/
        switch (state){
            case WAIT_FIRST:
                // printf("ESTADO: WAIT_FIRST_1\n");
                //Añado descriptores a lista de lectura
                FD_ZERO(&lectura);
                FD_SET(signal_fd,&lectura);
                FD_SET(socket_RTP,&lectura);
                select_timer = NULL;

                break;
            case PLAY_BUFF:
                // printf("ESTADO: PLAY_BUFF_1\n");
                FD_ZERO(&lectura);
                FD_SET(signal_fd,&lectura);
                FD_SET(socket_RTP,&lectura);
                //TEMPORIZADORES

                if (select_timer == NULL){  /*Cuando llega el primer paquete estamos en tiempo de buffering y esperamos
                                              el tiempo que queda hasta el momento en que se acaba el tiempo de buffering
                                             (calculado abajo)*/

                    // Compruebo el momento actual
                    if (gettimeofday (&momento_actual, NULL) <0){  // Momento_actual = coge el tiempo del sistema
                        printf("Error getting current time: %s",strerror(errno));
                        free_resources(buffer,log_file,verbose);
                        exit(EXIT_FAILURE);
                    }
                    //Fijo el momento donde dejo de esperar para llenar buffer en buffering_ms después del momento actual

                    //Guardo el tiempo de buffering en timer
                    timer.tv_sec = ((double)(buffering_ms))/1000; //Guardo los segundos
                    timer.tv_usec =  (buffering_ms-timer.tv_sec*1000)*1000; //Guardo los microsegundos

                    // printf("Timer debería ser de %lds y %luus\n", timer.tv_sec, timer.tv_usec);

                    //Sumo el tiempo de buffering (en timer) al momento actual y lo guardo en momento_timer
                    timeradd(&momento_actual,&timer,&momento_timer);

                    momento_primer_frame = momento_timer; //El momento en el que salta el primer timer es cuando se reproduce el primer frame, los demás se reproducirán en función a este momento

                }
                    //Imprimo el momento en que debería saltar el timer (esto es para debug durante el desarrollo)
                    // printf ("Momento donde debe saltar el timer: %lus y %luus\n",momento_timer.tfv_sec,momento_timer.tv_usec);

                //Hay que buscar qué paquete queremos resproducir -> Menor timestamp no reproducida

                //Si aún no hemos reproducido nada buscamos el paquete con menor timestamp de todos los del buffer (n_seq entre mínimo y máximo guardado)
                if (!first_frame_played)
                    last_seq_n_played = min_seq_n_buffered;

                //Buscamos el paquete con menor timestamp con n_seq entre el último reproducido y el máximo en el buffer
                res = get_lowest_timestamp(buffer,&lowest_ts,&lowest_seq_n,last_seq_n_played,max_seq_n_buffered);
                
                // Si no tenemos mas paquetes en el buffer
                if (res < 0){
                    state = PLAY_EMPTY_BUFF;
                    // configuramos el timer de 5 segundos
                    select_timer->tv_sec = 5;
                    select_timer->tv_usec = 0;
                    break;
                    }


                if (first_frame_played){ //Si ya hemos empezado a reproducir se calcula el momento de la siguiente reproducción en función de la diferencia de timestamps entre el frame a reproducir y el primero
                    // printf("Timer (no sale 1) de %lus y %luus\n", timer.tv_sec, timer.tv_usec);porq
                    dummy_ts = lowest_ts - first_ts_played;
                    timer.tv_sec = (long)(((double) dummy_ts)/90000);
                    timer.tv_usec = (long) (((((double) dummy_ts)/90000)-timer.tv_sec)*1000000);
                    timeradd(&timer,&momento_primer_frame,&momento_timer);
                }

                if (gettimeofday (&momento_actual, NULL) <0){
                        printf("Error getting current time: %s",strerror(errno));
                        free_resources(buffer,log_file,verbose);
                        exit(EXIT_FAILURE);
                }

                timersub(&momento_timer,&momento_actual,&timer);    //Obtengo el tiempo actual y lo resto del momento de timer, ese es el timepo que queda por esperar
                if(timer.tv_sec < 0 || timer.tv_usec <0){   //Si por retardos se ha pasado ya el momento ponemos el timer a 0 para que el select salte de inmediato
                    timer.tv_sec = 0;
                    timer.tv_usec = 0;
                    if (verbose) {
                        fprintf(log_file, "N");
                    }
                }

                select_timer = &timer;  //Elegimos el timer configurado para usarse en el select
                // printf("Timer de %lus y %luus\n", timer.tv_sec, timer.tv_usec);

                break;
            default:
                printf("Unknown state: %d",state);
                free_resources(buffer,log_file,verbose);
                exit(EXIT_FAILURE);
        }

        //SELECT
        res = select(FD_SETSIZE, &lectura, NULL, NULL, select_timer); // &TIMER

        //ANÁLISIS SELECT
        if (res<0){
            printf("Error in select instruction: %s",strerror(errno));
            free_resources(buffer,log_file,verbose);
            exit(EXIT_FAILURE);
        }
        //HA SALTADO EL TIMER
        if (res == 0){
            switch (state){
                case PLAY_BUFF:
                    //Si el frame que quiero reproducir no es el inmediatamente siguiente es que me ha faltad algún paquete
                    // if(lowest_seq_n != last_seq_n_played){
                    //     if (verbose) {
                    //         fprintf(log_file, "[%d]",last_seq_n_played);
                    //     }
                    // }
                    //Intento reproducir el frame
                    temp = lowest_seq_n;
                    res = play_frame(buffer,&lowest_seq_n,verbose, log_file);
                    // res = play_frame(buffer,&lowest_seq_n,verbose, log_file);
                    if (res == 0){
                        frames_played += 1;
                    } else{
                        frames_missed += 1;
                        if (verbose) {
                            if(lowest_seq_n==temp)
                                fprintf(log_file, "[%d]",lowest_seq_n-1);
                            else
                                fprintf(log_file, "[%d]",lowest_seq_n);
                        }
                    }
                    last_seq_n_played = lowest_seq_n + 1;
                    if (!first_frame_played){
                        first_ts_played = lowest_ts;
                        first_frame_played = true;
                        gettimeofday(&inicio_reproduccion, NULL);
                    }
                    break;
                case PLAY_EMPTY_BUFF:
                //uint32_t finalTime = header_rtp.ts-lowest_ts;
                        if (verbose){
                            gettimeofday(&fin_reproduccion, NULL);
                            timersub(&fin_reproduccion, &inicio_reproduccion, &duracion_real);
                            uint32_t tiempo_teorico = dummy_ts;
                            duracion_teoria.tv_sec = (long)(((double) tiempo_teorico)/90000);
                            duracion_teoria.tv_usec = (long) (((((double) tiempo_teorico)/90000)-timer.tv_sec)*1000000);
                            // duracion_teoria.tv_sec = momento_timer.tv_sec;
                            // duracion_teoria.tv_usec = momento_timer.tv_usec;
                            fprintf(log_file, "\n----------- Estadísticas de Reproducción ------------------\n");
                            fprintf(log_file, "Tiempo de reproducción teórico: %ld.%06ld segundos\n", duracion_teoria.tv_sec, duracion_teoria.tv_usec);
                            fprintf(log_file, "Tiempo de reproducción real (incluyendo pausas intermedias y 5s al final): %ld.%06ld segundos\n", duracion_real.tv_sec, duracion_real.tv_usec);
                            fprintf(log_file, "Número total de paquetes: %d\n", num_packets);
                            fprintf(log_file, "Número paquetes recibidos tarde: %d\n", late_packets);
                            fprintf(log_file, "Número de frames reproducidos: %d\n", frames_played);
                            fprintf(log_file, "Número de frames no reproducidos: %d\n", frames_missed);
                            fprintf(log_file, "Tiempo de buffering: %dms\n", buffering_ms);
                            fprintf(log_file, "--------------------------------------------------------------\n");

                            printf("\nNo more packets in buffer, closing application...\n");
                        }
                        free_resources(buffer,log_file,verbose);
                        exit(EXIT_SUCCESS);
                default:
                    break;
            }
        }
        else{
            //HA LLEGADO UN PAQUETE
            if (FD_ISSET(socket_RTP,&lectura)==1){
                read = recvfrom(socket_RTP, buffer_paquetes, 368*sizeof(uint32_t), 0, NULL, 0); // Va metiendo los packetes al buffer_paquetes

                // Si estamos esperando los cinco segundos y viene un paquete reseteamos el timer.
                

                num_packets++;

                if (read<0) {
                    fprintf(stderr,"Error reading from socket, closing...");
                    exit(EXIT_FAILURE);
                }
                
                //Extracción de los datos de la cabecera RTP
                //printf("Packet size: %u\n",read);
                header_rtp.version = ntohs(buffer_paquetes[0]<<16>>16)>>14; // 16 de bits
                header_rtp.p = ntohs(buffer_paquetes[0]<<16>>16)<<2>>15;
                header_rtp.x = ntohs(buffer_paquetes[0]<<16>>16)<<3>>15;
                header_rtp.cc = ntohs(buffer_paquetes[0]<<16>>16)<<4>>12;
                header_rtp.m = ntohs(buffer_paquetes[0]<<16>>16)<<8>>15;

                /* Si el marcador es 1 (último paquete)*/
                if (verbose) {
                    if (header_rtp.m) {
                    fprintf(log_file, "+");
                } else {
                    fprintf(log_file, "-");
                }
                }

                header_rtp.pt = ntohs(buffer_paquetes[0]<<16>>16)<<9>>9;

                header_rtp.seq = ntohs(buffer_paquetes[0]>>16);

                // Coge el valor de secuencia más bajo de todos los paquetes que le hayan llegado
                if (!first_packet_arrived || header_rtp.seq<min_seq_n_buffered)
                    min_seq_n_buffered = header_rtp.seq;
                if (!first_packet_arrived || header_rtp.seq>max_seq_n_buffered)
                    max_seq_n_buffered = header_rtp.seq;

                // if (header_rtp.seq<last_seq_n_played)
                // {
                //    if (verbose) {
                //         fprintf(log_file, "L");
                //     }
                // }


                header_rtp.ts = ntohl(buffer_paquetes[1]); // 32 bits

                header_rtp.ssrc = ntohl(buffer_paquetes[2]);

                if(first_frame_played){
                    if (gettimeofday (&momento_actual, NULL) <0){
                            printf("Error getting current time: %s",strerror(errno));
                            free_resources(buffer,log_file,verbose);
                            exit(EXIT_FAILURE);
                    }

                    dummy_ts = header_rtp.ts - first_ts_played;
                    timer.tv_sec = (long)(((double) dummy_ts)/90000);
                    timer.tv_usec = (long) (((((double) dummy_ts)/90000)-timer.tv_sec)*1000000);
                    timeradd(&timer,&momento_primer_frame,&momento_timer);
                    if(timercmp(&momento_timer,&momento_actual,<)){
                        printf("a\n");
                        late_packets += 1;
                        if (verbose) {
                            fprintf(log_file, "L");
                        }
                        continue;
                    }
                }


                //Extracción de cabeceras Vitext
                header_vtx.off = ntohl(buffer_paquetes[3]);
                header_vtx.width = ntohs(buffer_paquetes[4]>>16);
                header_vtx.height = ntohs(buffer_paquetes[4]<<16>>16);

                //Conversión de los datos a formato adecuado
                uint16_t data_length = packet_to_data(buffer_paquetes,read,buffer_data);
                // uint32_t *data = (buffer_paquetes + 5); // Asumiendo que los datos comienzan después de las cabeceras RTP y Vitext
                //uint16_t data_length = read - (5 * sizeof(uint32_t));
                // uint8_t *data = buffer_data;
                // printf("Data length: %u\n",data_length+1);

                // printf("%u\n%u\n%u\n%u\n%u\n%u\n%u\n%u\n%u\n",header_rtp.version,header_rtp.p,header_rtp.x,header_rtp.cc,header_rtp.m,header_rtp.pt,header_rtp.seq,header_rtp.ts,header_rtp.ssrc);

                // // Insertar en el buffer
                insert = pbuf_insert(buffer,             // Puntero al búfer
                                    (uint32_t) header_rtp.seq,     // Número de secuencia del paquete
                                    (uint32_t) header_rtp.ts,      // Marca de tiempo RTP
                                    (bool) header_rtp.m,       // Indicador de último paquete
                                    header_vtx.off,     // Desplazamiento del frame Vitext
                                    header_vtx.width,   // Ancho del video
                                    header_vtx.height,  // Altura del video
                                    buffer_data,//(uint8_t *) data,               // Datos de frame??
                                    data_length);
                //Si se insertó mal en el buffer acabo
                first_packet_arrived = true;
                if (insert<0){
                    printf("Error in packet insertion: %s",strerror(errno));
                    free_resources(buffer,log_file,verbose);
                    exit(EXIT_FAILURE);
                }
                //Si era mi primer paquete
                state = PLAY_BUFF;
                // printf("%ux%u\n", header_vtx.width, header_vtx.height);
                // printf("%c\n",buffer_paquetes[5+(24)/4]>>24);
                // for (int i = 0; i<data_length;i++){
                //     printf("%c",*(buffer_data+i));
                // }
            }
            //HAN PULSADO CTRL+C
            if (FD_ISSET(signal_fd,&lectura)==1){
                if(verbose){
                  printf("Signal received, closing...\n");
                  gettimeofday(&fin_reproduccion, NULL);
                  timersub(&fin_reproduccion, &inicio_reproduccion, &duracion_real);
                  uint32_t tiempo_teorico = dummy_ts;
                  duracion_teoria.tv_sec = (long)(((double) tiempo_teorico)/90000);
                  duracion_teoria.tv_usec = (long) (((((double) tiempo_teorico)/90000)-timer.tv_sec)*1000000);
                  // duracion_teoria.tv_sec = momento_timer.tv_sec;
                  // duracion_teoria.tv_usec = momento_timer.tv_usec;
                  fprintf(log_file, "\n----------- Estadísticas de Reproducción ------------------\n");
                  fprintf(log_file, "Tiempo de reproducción teórico: %ld.%06ld segundos\n", duracion_teoria.tv_sec, duracion_teoria.tv_usec);
                  fprintf(log_file, "Tiempo de reproducción real (incluyendo pausas intermedias): %ld.%06ld segundos\n", duracion_real.tv_sec, duracion_real.tv_usec);
                  fprintf(log_file, "Número total de paquetes: %d\n", num_packets);
                  fprintf(log_file, "Número paquetes recibidos tarde: %d\n", late_packets);
                  fprintf(log_file, "Número de frames reproducidos: %d\n", frames_played);
                  fprintf(log_file, "Número de frames no reproducidos: %d\n", frames_missed);
                  fprintf(log_file, "Tiempo de buffering: %dms\n", buffering_ms);
                  fprintf(log_file, "--------------------------------------------------------------\n");

                  printf("Ctrl+C pressed, clossing application...\n");

                }

                // RTCP BYE
                memset(&header_rtcp, 0, sizeof(rtcp_t));

                // Configuracion de la cabecera RTCP
                header_rtcp.common.version = 2; 
                header_rtcp.common.p = 1;
                header_rtcp.common.count = 1;  
                header_rtcp.common.pt = 203;  // Mensaje BYE

                // Le pasamos el SSRC por el terminal
                header_rtcp.r.bye.src[0] = ntohl(ssrc); 

                // Enviamos el paquete BYE al grupo multicast
                if (sendto(socket_RTCP, &header_rtcp, sizeof(rtcp_t), 0, (struct sockaddr*)&remote_RTCP, sizeof(remote_RTCP)) == -1) {
                    perror("Error al enviar el paquete BYE");
                }

                free_resources(buffer,log_file,verbose);
                exit(EXIT_SUCCESS);
            }
        }



    }
}