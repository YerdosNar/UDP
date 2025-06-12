#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h> // For fcntl
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "packet.h"

int sockfd;
struct sockaddr_in receiver_addr;
socklen_t addrlen = sizeof(receiver_addr);
Packet last_packet;
FILE *log_fp;
volatile sig_atomic_t timeout_occurred = 0;

void print_progress_bar(int sent_bytes, int total_bytes) {
    if (total_bytes == 0) return;
    const int bar_width = 50;
    float percentage = (float)sent_bytes / total_bytes;
    int pos = (int)(bar_width * percentage);

    printf("\r[");
    for(int i = 0; i < bar_width; ++i) {
        if(i < pos) printf("#");
        else printf("-");
    }
    printf("] %3d%%", (int)(percentage * 100));
    fflush(stdout);
}

void log_event(const char* event, Packet *pkt) {
    time_t now = time(NULL);
    fprintf(log_fp, "[%ld] %s - type: %d, seqNum: %d, ackNum: %d, len: %d\n",
            now, event, pkt->type, pkt->seqNum, pkt->ackNum, pkt->length);
    fflush(log_fp);
}

void handle_timeout(int sig) {
    timeout_occurred = 1;
}

int main(int argc, char *argv[]) {
    if(argc != 7) {
        fprintf(stderr, "Usage: %s <sender_port> <receiver_ip> <receiver_port> <timeout> <filename> <prob>\n", argv[0]);
        exit(1);
    }

    int sender_port = atoi(argv[1]);
    char *receiver_ip = argv[2];
    int receiver_port = atoi(argv[3]);
    int timeout = atoi(argv[4]);
    char *filename = argv[5];
    double ack_drop_prob = atof(argv[6]);

    int total_sent = 0, f_size;

    signal(SIGALRM, handle_timeout);
    srand(time(NULL));

    log_fp = fopen("udp_sender_logs.txt", "a");
    if(!log_fp) {
        perror("Failed to open log file");
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Set socket to non-blocking
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in sender_addr;
    memset(&sender_addr, 0, sizeof(sender_addr));
    sender_addr.sin_family = AF_INET;
    sender_addr.sin_addr.s_addr = INADDR_ANY;
    sender_addr.sin_port = htons(sender_port);

    if(bind(sockfd, (struct sockaddr *)&sender_addr, sizeof(sender_addr)) < 0) {
        perror("bind failed");
        exit(1);
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(receiver_port);

    if(inet_pton(AF_INET, receiver_ip, &receiver_addr.sin_addr) <= 0) {
        perror("Invalid receiver IP");
        exit(1);
    }

    Packet greet_pkt = {0};
    greet_pkt.type = TYPE_DATA;
    greet_pkt.seqNum = 0;
    strcpy(greet_pkt.data, "Greeting");
    sendto(sockfd, &greet_pkt, sizeof(greet_pkt), 0, (struct sockaddr *)&receiver_addr, addrlen);

    Packet ack_pkt = {0};
    while(recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&receiver_addr, &addrlen) <= 0);

    if(ack_pkt.type != TYPE_ACK || strcmp(ack_pkt.data, "OK") != 0) {
        fprintf(stderr, "Unexpected response. Aborting.\n");
        exit(1);
    }

    Packet fname_pkt = {0};
    fname_pkt.type = TYPE_DATA;
    fname_pkt.seqNum = 1;
    strncpy(fname_pkt.data, filename, MAX_DATA_SIZE);
    sendto(sockfd, &fname_pkt, sizeof(fname_pkt), 0, (struct sockaddr *)&receiver_addr, addrlen);

    FILE *fp = fopen(filename, "rb");
    if(!fp) {
        perror("fopen failed");
        exit(1);
    }
    fseek(fp, 0L, SEEK_END);
    f_size = ftell(fp);
    rewind(fp);
    sendto(sockfd, &f_size, sizeof(int), 0, (struct sockaddr *)&receiver_addr, addrlen);

    int seq = 3;
    int waiting_ack;
    while(1) {
        char buffer[MAX_DATA_SIZE];
        int bytes_read = fread(buffer, 1, MAX_DATA_SIZE, fp);

        last_packet.type = TYPE_DATA;
        last_packet.seqNum = seq;
        last_packet.length = bytes_read;
        memcpy(last_packet.data, buffer, bytes_read);

        sendto(sockfd, &last_packet, sizeof(last_packet), 0, (struct sockaddr *)&receiver_addr, addrlen);
        log_event("SEND DATA", &last_packet);

        alarm(timeout);
        waiting_ack = 1;

        while(waiting_ack) {
            if(timeout_occurred) {
                log_event("TIMEOUT", &last_packet);
                sendto(sockfd, &last_packet, sizeof(last_packet), 0, (struct sockaddr *)&receiver_addr, addrlen);
                log_event("RETRANSMIT", &last_packet);
                timeout_occurred = 0;
                alarm(timeout);
            }

            Packet ack;
            ssize_t recv_len = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&receiver_addr, &addrlen);

            if(recv_len > 0) {
                if(ack.type == TYPE_ACK && ack.ackNum == seq) {
                    log_event("RECV ACK", &ack);
                    alarm(0); // Cancel the alarm
                    waiting_ack = 0;
                    total_sent += last_packet.length;
                    print_progress_bar(total_sent, f_size);
                }
            }
        }

        if(bytes_read < MAX_DATA_SIZE) break;
        seq++;
    }

    Packet eot = { .type = TYPE_EOT, .seqNum = seq };
    sendto(sockfd, &eot, sizeof(eot), 0, (struct sockaddr *)&receiver_addr, addrlen);
    log_event("SEND EOT", &eot);

    fclose(fp);
    fclose(log_fp);
    close(sockfd);

    printf("\nFile sent successfully.\n");

    return 0;
}
