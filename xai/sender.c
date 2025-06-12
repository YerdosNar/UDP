#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "packet.h"

#define TIMEOUT_INTERVAL 3

int waiting_ack = 0;
int sockfd;
struct sockaddr_in receiver_addr;
socklen_t addr_len = sizeof(receiver_addr);
Packet last_packet;
FILE *log_fp;

// Print progress bar for file transfer
void print_progress_bar(int sent_bytes, int total_bytes) {
    const int bar_width = 50;
    float percentage = (float)sent_bytes / total_bytes;
    int pos = (int)(bar_width * percentage);

    printf("\r[");
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) printf("#");
        else printf("-");
    }
    printf("] %3d%%", (int)(percentage * 100));
    fflush(stdout);
}

// Log events to file
void log_event(const char *event, Packet *pkt) {
    time_t now = time(NULL);
    fprintf(log_fp, "[%ld] %s - type: %d, seqNum: %d, ackNum: %d, len: %d\n",
            now, event, pkt->type, pkt->seqNum, pkt->ackNum, pkt->length);
    fflush(log_fp);
}

// Handle timeout for retransmission
void handle_timeout(int sig) {
    if (waiting_ack) {
        fprintf(log_fp, "Timeout occurred. Retransmitting packet %d...\n", last_packet.seqNum);
        fflush(log_fp);
        if (sendto(sockfd, &last_packet, sizeof(last_packet), 0, (struct sockaddr *)&receiver_addr, addr_len) < 0) {
            perror("Retransmission failed");
        } else {
            log_event("RETRANSMIT", &last_packet);
        }
        alarm(TIMEOUT_INTERVAL);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 7) {
        fprintf(stderr, "Usage: %s <sender_port> <receiver_ip> <receiver_port> <timeout> <filename> <prob>\n", argv[0]);
        exit(1);
    }

    int sender_port = atoi(argv[1]);
    char *receiver_ip = argv[2];
    int receiver_port = atoi(argv[3]);
    int timeout = atoi(argv[4]);
    char *filename = argv[5];
    float ack_drop_prob = atof(argv[6]);
    int total_sent = 0;
    int f_size;

    signal(SIGALRM, handle_timeout);
    srand(time(NULL));

    // Open log file
    log_fp = fopen("sender_udp_logs", "a");
    if (!log_fp) {
        perror("Failed to open log file");
        exit(1);
    }

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        fclose(log_fp);
        exit(1);
    }

    // Set up sender address
    struct sockaddr_in sender_addr;
    memset(&sender_addr, 0, sizeof(sender_addr));
    sender_addr.sin_family = AF_INET;
    sender_addr.sin_addr.s_addr = INADDR_ANY;
    sender_addr.sin_port = htons(sender_port);

    if (bind(sockfd, (struct sockaddr *)&sender_addr, sizeof(sender_addr)) < 0) {
        perror("Bind failed");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }

    // Set up receiver address
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(receiver_port);
    if (inet_pton(AF_INET, receiver_ip, &receiver_addr.sin_addr) <= 0) {
        perror("Invalid receiver IP");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }

    // Send greeting packet
    Packet greet_pkt;
    memset(&greet_pkt, 0, sizeof(greet_pkt));
    greet_pkt.type = TYPE_DATA;
    greet_pkt.seqNum = 0;
    greet_pkt.length = strlen("Greeting");
    strcpy(greet_pkt.data, "Greeting");
    if (sendto(sockfd, &greet_pkt, sizeof(greet_pkt), 0, (struct sockaddr *)&receiver_addr, addr_len) < 0) {
        perror("Failed to send greeting");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    log_event("SEND GREETING", &greet_pkt);

    // Receive OK acknowledgment
    Packet ack_pkt;
    memset(&ack_pkt, 0, sizeof(ack_pkt));
    if (recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&receiver_addr, &addr_len) < 0) {
        perror("Failed to receive OK");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    if (ack_pkt.type != TYPE_ACK || strcmp(ack_pkt.data, "OK") != 0) {
        fprintf(stderr, "Unexpected response. Aborting.\n");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    log_event("RECV OK", &ack_pkt);

    // Send filename
    Packet fname_pkt;
    memset(&fname_pkt, 0, sizeof(fname_pkt));
    fname_pkt.type = TYPE_DATA;
    fname_pkt.seqNum = 1;
    fname_pkt.length = strlen(filename);
    strncpy(fname_pkt.data, filename, MAX_DATA_SIZE);
    if (sendto(sockfd, &fname_pkt, sizeof(fname_pkt), 0, (struct sockaddr *)&receiver_addr, addr_len) < 0) {
        perror("Failed to send filename");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    log_event("SEND FILENAME", &fname_pkt);

    // Open input file and send file size
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open input file");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    fseek(fp, 0L, SEEK_END);
    f_size = ftell(fp);
    rewind(fp);

    Packet size_pkt;
    memset(&size_pkt, 0, sizeof(size_pkt));
    size_pkt.type = TYPE_DATA;
    size_pkt.seqNum = 2;
    size_pkt.length = sizeof(int);
    memcpy(size_pkt.data, &f_size, sizeof(int));
    if (sendto(sockfd, &size_pkt, sizeof(size_pkt), 0, (struct sockaddr *)&receiver_addr, addr_len) < 0) {
        perror("Failed to send file size");
        fclose(fp);
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    log_event("SEND FILE SIZE", &size_pkt);

    // Main data transfer loop
    int seq = 3;
    while (1) {
        Packet pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = TYPE_DATA;
        pkt.seqNum = seq;
        pkt.length = fread(pkt.data, 1, MAX_DATA_SIZE, fp);
        last_packet = pkt;

        if (sendto(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&receiver_addr, addr_len) < 0) {
            perror("Failed to send data packet");
            fclose(fp);
            fclose(log_fp);
            close(sockfd);
            exit(1);
        }
        log_event("SEND DATA", &pkt);
        waiting_ack = 1;
        alarm(timeout);

        while (waiting_ack) {
            Packet ack;
            memset(&ack, 0, sizeof(ack));
            ssize_t recv_len = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&receiver_addr, &addr_len);
            if (recv_len > 0 && ack.type == TYPE_ACK && ack.ackNum == seq) {
                if (drop(ack_drop_prob)) {
                    fprintf(log_fp, "ACK %d dropped intentionally\n", ack.ackNum);
                    fflush(log_fp);
                    continue;
                }
                log_event("RECV ACK", &ack);
                alarm(0);
                waiting_ack = 0;
            }
        }
        total_sent += pkt.length;
        print_progress_bar(total_sent, f_size);

        if (pkt.length < MAX_DATA_SIZE) break;
        seq++;
    }

    // Send EOT packet
    Packet eot;
    memset(&eot, 0, sizeof(eot));
    eot.type = TYPE_EOT;
    eot.seqNum = seq;
    if (sendto(sockfd, &eot, sizeof(eot), 0, (struct sockaddr *)&receiver_addr, addr_len) < 0) {
        perror("Failed to send EOT");
    } else {
        log_event("SEND EOT", &eot);
    }

    // Cleanup
    fclose(fp);
    fclose(log_fp);
    close(sockfd);
    printf("\nFile transfer complete.\n");

    return 0;
}
