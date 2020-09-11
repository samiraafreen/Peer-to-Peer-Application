#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PAYLOAD_SIZE 300
//#define PACKETS_PER_CONN 12
#define PACKETS_PER_CONN 10

int DEBUG;
extern int PORT;

int nb_conn;
int conn_count;
int cur_conn_count;

typedef struct connection_s {
    struct sockaddr_in servaddr;
    int my_id;
    int sockfd;
    int window_size;
    int segment_id;
    int rcv_id;
    int go_back_n;
    int pending;
    int close_rcvd;
} connection_t;

typedef enum msg_type_s {
    HB,
    CONN_REQ,
    CONN_REQ_ACK,
    CLOSE_CONN,
    CLOSE_CONN_ACK,
    DATA,
    DATA_ACK,
} msg_type_t;

typedef struct msg_s {
    msg_type_t msg_type;
    int window_size;
    int sender_id; /* only for msg with actual data */
    int segment_id;
    int port;
    unsigned long hash;
    int len;
    char payload[PAYLOAD_SIZE];
} msg_t;

typedef struct client_s {
    pthread_mutex_t msg_access_lock;
    struct sockaddr_in cliaddr;
    connection_t *connection;
    int should_disconnect;
    int client_hash;
    int msg_count;
    int read_head;
    int write_head;
    int last_received;
    int total_size;
    msg_t **msgs;
} client_t;

client_t **clients;

typedef struct con_req_s {
    struct sockaddr_in cliaddr;
    int client_hash;
    int sender_id;
    int window_size;
    int port;
    struct con_req_s *next;
} con_req_t;

int mrt_open(int n, int port);
/* n is the maximum number of neighbouring nodes that 
the node who calls this will support */

int mrt_connect(char *ip, int port);
/* takes in the ip_address and port from the user and gives back the 
p2p_layer a peer_id which it can use to spawn a thread for that specific peer */

int mrt_accept1();
/*  will return the peer_id of the peer that we accepted the link of */

int mrt_accept_protected1(char *peer_ip, int peer_port);

int mrt_probe_conn_req();
/* looks at the list of connection requests and returns true if there are any pending con req */

int mrt_send(int peer_id, void *data, int size);
/* sends this entire data reliably */

int mrt_receive(int peer_id, void *data, int size);
/* writes data of size to p2p_msg */

int mrt_disconnect(int peer_id);
/* i am getting from this peer_id */

int mrt_close();
/* i disconnect from every peer i have/ leave the network*/