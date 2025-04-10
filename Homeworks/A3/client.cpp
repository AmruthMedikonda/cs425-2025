#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/time.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define CLIENT_PORT 54321
#define TIMEOUT_SEC 5

int main() {
    // Create raw socket for TCP protocol
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Enable custom IP header construction
    int one = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt() failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt() failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Server address configuration
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    /* ========== STEP 1: Send SYN Packet (SEQ=200) ========== */
    char syn_packet[sizeof(struct iphdr) + sizeof(struct tcphdr)];
    memset(syn_packet, 0, sizeof(syn_packet));

    // IP Header
    struct iphdr *ip = (struct iphdr *)syn_packet;
    ip->ihl = 5;
    ip->version = 4;
    ip->tos = 0;
    ip->tot_len = htons(sizeof(syn_packet));
    ip->id = htons(12345);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IPPROTO_TCP;
    ip->saddr = inet_addr("127.0.0.1");
    ip->daddr = server_addr.sin_addr.s_addr;

    // TCP Header
    struct tcphdr *tcp = (struct tcphdr *)(syn_packet + sizeof(struct iphdr));
    tcp->source = htons(CLIENT_PORT);
    tcp->dest = htons(SERVER_PORT);
    tcp->seq = htonl(200);  // Initial sequence number
    tcp->ack_seq = 0;
    tcp->doff = 5;
    tcp->syn = 1;  // SYN flag set
    tcp->window = htons(8192);
    tcp->check = 0;  // Let kernel compute checksum

    // Send SYN packet
    if (sendto(sock, syn_packet, sizeof(syn_packet), 0, 
              (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("SYN packet sending failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    std::cout << "[+] Sent SYN packet with SEQ: 200" << std::endl;

    /* ========== STEP 2: Receive SYN-ACK Packet ========== */
    char buffer[65536];
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);
    bool syn_ack_received = false;
    time_t start_time = time(NULL);

    while (!syn_ack_received) {
        // Check timeout
        if (time(NULL) - start_time > TIMEOUT_SEC) {
            std::cerr << "[-] Connection timed out. Server may be offline." << std::endl;
            close(sock);
            exit(EXIT_FAILURE);
        }

        int data_size = recvfrom(sock, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&source_addr, &addr_len);
        
        if (data_size < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // Timeout handled by manual check
            }
            perror("recvfrom() failed");
            continue;
        }

        // Parse IP and TCP headers
        struct iphdr *recv_ip = (struct iphdr *)buffer;
        struct tcphdr *recv_tcp = (struct tcphdr *)(buffer + (recv_ip->ihl * 4));

        // Validate SYN-ACK response
        if (ntohs(recv_tcp->source) == SERVER_PORT &&
            ntohs(recv_tcp->dest) == CLIENT_PORT &&
            recv_tcp->syn == 1 && 
            recv_tcp->ack == 1 &&
            ntohl(recv_tcp->seq) == 400 && 
            ntohl(recv_tcp->ack_seq) == 201) {
            
            std::cout << "[+] Received SYN-ACK with SEQ: 400 and ACK: 201" << std::endl;
            syn_ack_received = true;
        }
    }

    /* ========== STEP 3: Send ACK Packet (SEQ=600, ACK=401) ========== */
    char ack_packet[sizeof(struct iphdr) + sizeof(struct tcphdr)];
    memset(ack_packet, 0, sizeof(ack_packet));

    // IP Header
    struct iphdr *ack_ip = (struct iphdr *)ack_packet;
    ack_ip->ihl = 5;
    ack_ip->version = 4;
    ack_ip->tot_len = htons(sizeof(ack_packet));
    ack_ip->id = htons(54321);
    ack_ip->ttl = 64;
    ack_ip->protocol = IPPROTO_TCP;
    ack_ip->saddr = inet_addr("127.0.0.1");
    ack_ip->daddr = server_addr.sin_addr.s_addr;

    // TCP Header
    struct tcphdr *ack_tcp = (struct tcphdr *)(ack_packet + sizeof(struct iphdr));
    ack_tcp->source = htons(CLIENT_PORT);
    ack_tcp->dest = htons(SERVER_PORT);
    ack_tcp->seq = htonl(600);  // Final sequence number
    ack_tcp->ack_seq = htonl(401);  // Acknowledge server's SEQ+1
    ack_tcp->doff = 5;
    ack_tcp->ack = 1;  // ACK flag set
    ack_tcp->window = htons(8192);
    ack_tcp->check = 0;

    // Send ACK packet
    if (sendto(sock, ack_packet, sizeof(ack_packet), 0,
              (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("ACK packet sending failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    std::cout << "[+] Sent ACK packet with SEQ: 600 and ACK: 401" << std::endl;
    std::cout << "[+] Three-way handshake completed successfully!" << std::endl;

    close(sock);
    return 0;
}
