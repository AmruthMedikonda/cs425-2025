#include <iostream>      
#include <cstring>       
#include <unistd.h>      
#include <arpa/inet.h>   
#include <netinet/ip.h>  
#include <netinet/tcp.h> 
#include <sys/socket.h>  
#include <errno.h>       

#define SERVER_IP "127.0.0.1"  // Server's IP address
#define SERVER_PORT 12345      // Server's port number (given in server.cpp)
#define CLIENT_PORT 54321      // Source port for our packets (from server.cpp)

int main() {
    // Create a raw socket for sending custom TCP packets
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt() failed");
        exit(EXIT_FAILURE);
    }


    // Enable IP header inclusion - allows us to provide our own IP headers
    // This is required for raw sockets to specify our own IP header
    int one = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt() failed");
        exit(EXIT_FAILURE);
    }

    // Set up the server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));       // Clear the structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Create and send SYN packet to initiate TCP handshake
    
    // Allocate memory for the packet (IP header + TCP header)
    char syn_packet[sizeof(struct iphdr) + sizeof(struct tcphdr)];
    memset(syn_packet, 0, sizeof(syn_packet)); // Clear the packet buffer

    // Set up IP header
    struct iphdr *ip = (struct iphdr *)syn_packet;
    ip->ihl = 5;                              // IP header length (5 * 4 = 20 bytes)
    ip->version = 4;                          // IPv4
    ip->tos = 0;                              // Type of service (normal)
    ip->tot_len = htons(sizeof(syn_packet));  // Total packet length (convert to network byte order)
    ip->id = htons(12345);                    // Identification
    ip->frag_off = 0;                         // Fragment offset
    ip->ttl = 64;                             // Time to live
    ip->protocol = IPPROTO_TCP;               // Protocol (TCP)
    ip->saddr = inet_addr("127.0.0.1");       // Source IP (client)
    ip->daddr = server_addr.sin_addr.s_addr;  // Destination IP (server)

    // Set up TCP header
    struct tcphdr *tcp = (struct tcphdr *)(syn_packet + sizeof(struct iphdr));
    tcp->source = htons(CLIENT_PORT);         // Source port (convert to network byte order)
    tcp->dest = htons(SERVER_PORT);           // Destination port (convert to network byte order)
    tcp->seq = htonl(200);                    // Sequence number 200 (as expected by server)
    tcp->ack_seq = 0;                         // Acknowledgment number (0 for SYN)
    tcp->doff = 5;                            // TCP header length (5 * 4 = 20 bytes)
    tcp->syn = 1;                             // SYN flag set to 1 (initiating connection)
    tcp->ack = 0;                             // ACK flag set to 0 (no acknowledgment yet)
    tcp->window = htons(8192);                // Window size (convert to network byte order)
    tcp->check = 0;                           // Checksum (will be computed by kernel)

    // Send SYN packet to the server
    if (sendto(sock, syn_packet, sizeof(syn_packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("SYN packet sending failed");
        exit(EXIT_FAILURE);
    }
    std::cout << "[+] Sent SYN packet with SEQ: 200" << std::endl;

    bool syn_ack_received = false;
    time_t start_time = time(NULL); // Track start time
    
    while (!syn_ack_received) {
        // Check timeout
        if (time(NULL) - start_time > TIMEOUT_SEC) {
            std::cerr << "[-] Server not responding. It may be offline." << std::endl;
            close(sock);
            exit(EXIT_FAILURE);
        }

    //Receive SYN-ACK packet from server
    
    char buffer[65536];                       // Buffer to store incoming packets
    struct sockaddr_in source_addr;           // Source address of incoming packets
    socklen_t addr_len = sizeof(source_addr);
    bool syn_ack_received = false;            // Flag to track SYN-ACK reception
    
    while (!syn_ack_received) {
        // Receive packet from the network
        int data_size = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&source_addr, &addr_len);
        if (data_size < 0) {
            perror("Packet reception failed");
            continue;
        }

        int data_size = recvfrom(sock, buffer, sizeof(buffer), 0, 
                               (struct sockaddr *)&source_addr, &addr_len);
        
        if (data_size < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::cerr << "[-] Connection timed out. Server may be offline." << std::endl;
                close(sock);
                exit(EXIT_FAILURE);
            }
            perror("recvfrom() failed");
            continue;
        }

        // Process IP and TCP headers from the received packet
        struct iphdr *recv_ip = (struct iphdr *)buffer;
        struct tcphdr *recv_tcp = (struct tcphdr *)(buffer + (recv_ip->ihl * 4));
        
        // Check if this is the SYN-ACK response we're expecting from the server
        // 1. Check if the packet is addressed to our port and from the server port
        // 2. Check if both SYN and ACK flags are set
        // 3. Check if the sequence number is 400 as specified in the server code
        if (ntohs(recv_tcp->dest) == CLIENT_PORT && ntohs(recv_tcp->source) == SERVER_PORT &&
            recv_tcp->syn == 1 && recv_tcp->ack == 1 && ntohl(recv_tcp->seq) == 400) {
            
            std::cout << "[+] Received SYN-ACK with SEQ: " << ntohl(recv_tcp->seq) 
                      << " and ACK: " << ntohl(recv_tcp->ack_seq) << std::endl;
            
            
            //Send ACK packet to complete handshake
            
            // Allocate memory for the ACK packet
            char ack_packet[sizeof(struct iphdr) + sizeof(struct tcphdr)];
            memset(ack_packet, 0, sizeof(ack_packet));
            
            // Set up IP header for ACK packet
            struct iphdr *ack_ip = (struct iphdr *)ack_packet;
            ack_ip->ihl = 5;                              // IP header length
            ack_ip->version = 4;                          // IPv4
            ack_ip->tos = 0;                              // Type of service
            ack_ip->tot_len = htons(sizeof(ack_packet));  // Total packet length
            ack_ip->id = htons(54321);                    // Identification
            ack_ip->frag_off = 0;                         // Fragment offset
            ack_ip->ttl = 64;                             // Time to live
            ack_ip->protocol = IPPROTO_TCP;               // Protocol (TCP)
            ack_ip->saddr = inet_addr("127.0.0.1");       // Source IP
            ack_ip->daddr = server_addr.sin_addr.s_addr;  // Destination IP
            
            // Set up TCP header for ACK packet
            struct tcphdr *ack_tcp = (struct tcphdr *)(ack_packet + sizeof(struct iphdr));
            ack_tcp->source = htons(CLIENT_PORT);         // Source port
            ack_tcp->dest = htons(SERVER_PORT);           // Destination port
            ack_tcp->seq = htonl(600);                    // Sequence number 600 (as expected by server)
            ack_tcp->ack_seq = htonl(ntohl(recv_tcp->seq) + 1); // ACK = server's SEQ + 1 (401)
            ack_tcp->doff = 5;                            // TCP header length
            ack_tcp->syn = 0;                             // SYN flag set to 0 
            ack_tcp->ack = 1;                             // ACK flag set to 1 (acknowledging)
            ack_tcp->window = htons(8192);                // Window size
            ack_tcp->check = 0;                           // Checksum
            
            // Send ACK packet to the server
            if (sendto(sock, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                perror("ACK packet sending failed");
                exit(EXIT_FAILURE);
            }
            std::cout << "[+] Sent ACK packet with SEQ: 600 and ACK: " << ntohl(ack_tcp->ack_seq) << std::endl;
            std::cout << "[+] Three-way handshake completed successfully!" << std::endl;
            
            syn_ack_received = true; // Marking SYN-ACK as received to exit loop
        }
    }
    
    close(sock);
    
    return 0;
}