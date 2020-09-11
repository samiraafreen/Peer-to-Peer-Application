#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "mrt.h"

int PORT = -1;

con_req_t *con_req_head = NULL;
int sockfd;

int DEBUG=0;

int send_packet(char *ip, int port, int peer_id, int nb_packets){

	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));

	if (peer_id == -1){
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(port);
		servaddr.sin_addr.s_addr = inet_addr(ip);
	}else{

		client_t *client = clients[peer_id];
        //("ip: %s port: %d\n", (const char *)&client->connection->servaddr.sin_addr.s_addr, ntohs(client->connection->servaddr.sin_port));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(client->connection->servaddr.sin_port);
		servaddr.sin_addr.s_addr = inet_addr((const char *)&client->connection->servaddr.sin_addr.s_addr);
	}

	// packet
	msg_t packet;
	packet.msg_type = HB;

	int tries = nb_packets;
	while (tries){
		sendto(sockfd, (const char*)&packet, sizeof(msg_t),
			0, (const struct sockaddr *)&servaddr, sizeof(struct sockaddr));
		tries--;
	}
	return 1;
}

unsigned long _calculate_hash(void *data, int len) {
    char *str = (char *)data;
    unsigned long hash = 5381;
    int c;

    int cur = 0;
    while (cur < len) {
        c = *str++;
        cur++;
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
}

int _send_disconn_req(struct sockaddr *servaddr, int sender_id)
{
    if(DEBUG) printf("Sending disconnection request\n");

    /* form packet */
    msg_t msg;
    msg.msg_type = CLOSE_CONN;
    msg.sender_id = sender_id;

    sendto(sockfd, (const char *)&msg, sizeof(msg_t),
        0, (const struct sockaddr *)servaddr,
            sizeof(struct sockaddr));

    return 1;
}

int _send_disconn_req_ack(struct sockaddr *servaddr, int sender_id)
{
    if(DEBUG) printf("Sending disconnection request ack\n");
    fflush(stdout);

    /* form packet */
    msg_t msg;
    msg.msg_type = CLOSE_CONN_ACK;
    msg.sender_id = sender_id;

    sendto(sockfd, (const char *)&msg, sizeof(msg_t),
        0, (const struct sockaddr *)servaddr,
            sizeof(struct sockaddr));

    return 1;
}

int _send_conn_req(struct sockaddr *servaddr, int port, int client_hash, int sender_id, int window_size)
{
    if(DEBUG) printf("Sending connection request from port: %d\n", port);

    /* form packet */
    msg_t msg;
    msg.msg_type = CONN_REQ;
    msg.sender_id = sender_id;
    msg.port = port;
    msg.segment_id = client_hash;
    msg.window_size = window_size;

    sendto(sockfd, (const char *)&msg, sizeof(msg_t),
        0, (const struct sockaddr *)servaddr,
            sizeof(struct sockaddr));

    return 1;
}

int _send_conn_req_ack(struct sockaddr *cliaddr, int sender_id)
{

    /* form packet */
    msg_t msg;
    msg.msg_type = CONN_REQ_ACK;
    msg.sender_id = sender_id;
    msg.window_size = PACKETS_PER_CONN;

    sendto(sockfd, (const char *)&msg, sizeof(msg_t),
        0, (const struct sockaddr *)cliaddr,
            sizeof(struct sockaddr));

    return 1;
}

int _send_data_ack(struct sockaddr *cliaddr, int sender_id,
                   int window_size, int segment_id, int go_back_n)
{

    /* form packet */
    msg_t msg;
    msg.msg_type = DATA_ACK;
    msg.sender_id = sender_id;
    msg.window_size = window_size;
    msg.segment_id = segment_id;
    msg.len = go_back_n;

    sendto(sockfd, (const char *)&msg, sizeof(msg_t),
        0, (const struct sockaddr *)cliaddr,
            sizeof(struct sockaddr));

    return 1;
}

int _save_message(int client_id, msg_t *msg) {
    assert(clients[client_id]->msg_count < PACKETS_PER_CONN);

    if(DEBUG) {
        printf("Saving msg from %d msg count: %d segment %d\n", client_id, clients[client_id]->msg_count, msg->segment_id);
        printf("len : %d\n", msg->len);
    }

    memcpy(clients[client_id]->msgs[clients[client_id]->write_head], msg, sizeof(msg_t));
    clients[client_id]->write_head =
        (clients[client_id]->write_head + 1) % PACKETS_PER_CONN;
    clients[client_id]->last_received++;
    clients[client_id]->msg_count++;

    return 1;
}

pthread_mutex_t con_req_access_lock = PTHREAD_MUTEX_INITIALIZER;

/* This part deals with the msg_handler */
typedef struct msg_handler_arg_s {
    pthread_t *thread_id;
    int cond;
} msg_handler_arg_t;

pthread_t hb_thread;
msg_handler_arg_t *hb_arg;

void *hb_handler(void *hb_arg)
{
    if(DEBUG)
        printf("HB thread spawned\n");

	struct timespec ts, ts1;
    ts.tv_sec = 0;
    ts.tv_nsec = 3000 * 1000;

    int i;

	while(((msg_handler_arg_t *)hb_arg)->cond){
		nanosleep(&ts, &ts1);
		for (i = 0; i < conn_count; i++){
			if (clients[i] != NULL) {
				send_packet(NULL, -1, i, 10);
			}
		}
	}
    pthread_exit(NULL);
}

pthread_t msg_handler_thread;
msg_handler_arg_t *msg_handler_arg;

void *msg_handler(void *arg)
{
    if(DEBUG)
        printf("Worker thread spawned\n");

    int len, n, i;
    struct sockaddr_in cliaddr;
    len = sizeof(cliaddr);  //len is value/result

    msg_t buffer;

    int maxfd;
    fd_set rset;
    struct timeval tv = {3, 0};
    while(((msg_handler_arg_t *)arg)->cond) {
        FD_ZERO(&rset);
        FD_SET(sockfd, &rset);
        maxfd = sockfd + 1;

        int res = select(maxfd, &rset, NULL, NULL, &tv);

        if(res > 0) {
            if(FD_ISSET(sockfd, &rset)) {
                n = recvfrom(sockfd, (char *)&buffer, sizeof(buffer),
                        MSG_WAITALL, (struct sockaddr *)&cliaddr,
                        (socklen_t *)&len);

                assert(len == sizeof(struct sockaddr_in));
                if(n > 0) {
                    if (buffer.msg_type == CONN_REQ) {
                        if (conn_count < nb_conn) {
                            if(DEBUG)
                                printf("Connection request received %d\n", ntohs(cliaddr.sin_port));

                            /* putting it in list for accept() to accpet it */
                            pthread_mutex_lock(&con_req_access_lock);

                            int found = 0, sender_id = -1;
                            /* Check if this connection was already received */
                            for (i = 0; i < conn_count; i++) {
                                if (clients[i] != NULL) {
                                    if (clients[i]->client_hash == buffer.segment_id) {
                                        found = 1;
                                        sender_id = i;
                                        break;
                                    }
                                }
                            }

                            if(!found) {
                                con_req_t *con_req = malloc(sizeof(con_req_t));
                                con_req->next = NULL;
                                memcpy(&(con_req->cliaddr), &cliaddr, sizeof(struct sockaddr_in));
                                con_req->client_hash = buffer.segment_id;
                                con_req->port = buffer.port;
                                con_req->sender_id = buffer.sender_id;
                                con_req->window_size = buffer.window_size;

                                con_req_t *cur = con_req_head, *last = NULL;

                                /* traverse to the end of the list */
                                while (cur) {
                                    last = cur;
                                    cur = cur->next;
                                }

                                if (last == NULL) {
                                    con_req_head = con_req;
                                } else {
                                    last->next = con_req;
                                }
                            } else {
                                _send_conn_req_ack((struct sockaddr *)&cliaddr, sender_id);
                            }

                            pthread_mutex_unlock(&con_req_access_lock);
                        } else {

                            _send_conn_req_ack((struct sockaddr *)&cliaddr, -2);
                        }
                    } else if (buffer.msg_type == CONN_REQ_ACK) {
                        if (buffer.sender_id == -2) {
                            /* receiver is full */
                            //TODO
                            return NULL;
                        }
                        if(DEBUG)
                            printf("Connection request ack received: %d\n", buffer.sender_id);

                        if(buffer.sender_id >= conn_count) {

                            connection_t *connection = malloc(sizeof(connection_t));
                            connection->sockfd = sockfd;
                            connection->my_id = buffer.sender_id;
                            connection->window_size = buffer.window_size;
                            connection->segment_id = 0;
                            connection->rcv_id = 0;
                            connection->go_back_n = 0;
                            connection->pending = 0;
                            connection->close_rcvd = 0;
                            memcpy(&(connection->servaddr), &cliaddr, sizeof(struct sockaddr_in));

                            /* putting it in list for accept() to accpet it */
                            pthread_mutex_lock(&con_req_access_lock);

                            int conn_id = conn_count;
                            client_t *client = clients[conn_id];
                            /* We accept the connection */
                            memcpy(&(client->cliaddr), &cliaddr, sizeof(struct sockaddr_in));

                            client->msgs = malloc(PACKETS_PER_CONN * sizeof(msg_t *));
                            int i;
                            for(i = 0; i < PACKETS_PER_CONN; i++) {
                                clients[conn_id]->msgs[i] = malloc(sizeof(msg_t));
                            }
                            clients[conn_id]->msg_count = 0;
                            clients[conn_id]->read_head = 0;
                            clients[conn_id]->write_head = 0;
                            srand(time(0));
                            clients[conn_id]->client_hash = rand();
                            clients[conn_id]->last_received = -1;
                            clients[conn_id]->total_size = 0;

                            clients[conn_id]->connection = connection;

                            cur_conn_count++;
                            conn_count++;
                            
                            pthread_mutex_unlock(&con_req_access_lock);
                        }

                    } else if (buffer.msg_type == CLOSE_CONN) {
                        /* close_conn received close connection */
                        if(DEBUG)
                            printf("Connection CLOSE request received\n");

                        int sender_id = buffer.sender_id;
                        client_t *client = clients[sender_id];
                        _send_disconn_req_ack((struct sockaddr *)&cliaddr, clients[sender_id]->connection->my_id);

                        while (client->connection->pending != 0) {}


                        if(client != NULL && client->connection->close_rcvd == 0) {
                            client->connection->close_rcvd = 1;
                            pthread_mutex_lock(&con_req_access_lock);
                            
                            printf("FINAL Total received from %d: %d bytes\n", sender_id, clients[sender_id]->total_size);
                            //clients[sender_id] = NULL;
                            //free(client);
                            cur_conn_count--;
                            assert(cur_conn_count > -1);
                            
                            pthread_mutex_unlock(&con_req_access_lock);
                        }
                        

                    } else if (buffer.msg_type == CLOSE_CONN_ACK) {
                        if(DEBUG)
                            printf("Connection CLOSE ACK received\n");
                        int sender_id = buffer.sender_id;
                        pthread_mutex_lock(&con_req_access_lock);
                        if(clients[sender_id]->connection != NULL) {
                            clients[sender_id]->connection->close_rcvd = 1;
                            //free(clients[sender_id]->connection);
                            //clients[sender_id]->connection = NULL;
                        }
                        pthread_mutex_unlock(&con_req_access_lock);

                    } else if (buffer.msg_type == DATA) {
                        int sender_id = buffer.sender_id;
                        int window_size = -1;
                        int segment_id = buffer.segment_id;
                        int go_back_n = 0;
                        if(DEBUG)
                            printf("data received sender_id : %d segment_id %d \n", sender_id, segment_id);

                        unsigned long hash = _calculate_hash(buffer.payload, buffer.len);

                        pthread_mutex_lock(&clients[sender_id]->msg_access_lock);
                        if (clients[sender_id]->last_received + 1 == segment_id && hash == buffer.hash) {
                            /* Save this message */
                            _save_message(sender_id, &buffer);
                        } else if (segment_id < clients[sender_id]->last_received) {
                            segment_id = buffer.segment_id;
                        } else {
                            segment_id = clients[sender_id]->last_received;
                            go_back_n = 1;
                        }

                        if(DEBUG)
                            printf("data ack segment_id : %d last_received %d go_back_n %d\n", segment_id, clients[sender_id]->last_received, go_back_n);

                        window_size = PACKETS_PER_CONN - clients[sender_id]->msg_count;
                        _send_data_ack((struct sockaddr *)&cliaddr, sender_id,
                                       window_size, segment_id, go_back_n);

                        pthread_mutex_unlock(&clients[sender_id]->msg_access_lock);

                    } else if (buffer.msg_type == DATA_ACK) {
                        if(DEBUG)
                            printf("DATA ack received: sender: %d  segment %d len %d window %d\n",
                                    buffer.sender_id, buffer.segment_id, buffer.len, buffer.window_size);

                        int sender_id = buffer.sender_id;
                        if (buffer.segment_id + 1 >=
                            clients[sender_id]->connection->rcv_id)
                        {
                            clients[sender_id]->connection->window_size =
                                buffer.window_size;
                            clients[sender_id]->connection->rcv_id =
                                buffer.segment_id + 1;
                        }

                        if (buffer.len) {
                            if(DEBUG) printf("Go-back-n\n");

                            clients[sender_id]->connection->go_back_n = 1;
                        }
                    } else if (buffer.msg_type == HB) {
                         if(DEBUG)
                            printf("HB received\n");
                    }
                }
                else {

                    printf("=====---- %d -----====\n", n);
                }
            }
        }
    }

    pthread_exit(NULL);
}
/* --------------------------------------- */

int
mrt_open(int conn, int port)
{
    int i;
    nb_conn = conn;
    conn_count = 0;
    cur_conn_count = 0;
    struct sockaddr_in servaddr;
    PORT = port;

    // Creating socket file descriptor
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    if(DEBUG) {
        printf("MRT open PORT: %d port: %d, sockfd: %d\n", PORT, port, sockfd);
    }

    memset(&servaddr, 0, sizeof(servaddr));

    // Filling server information
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    //inet_aton(ip, &servaddr.sin_addr);
    servaddr.sin_port = htons(PORT);

    // Bind the socket with the server address
    if ( bind(sockfd, (const struct sockaddr *)&servaddr,
            sizeof(servaddr)) < 0 )
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    /* Create clients array */
    clients = malloc(nb_conn * sizeof(client_t *));
    for(i = 0; i < nb_conn; i++) {
        clients[i] = NULL;
    }

    msg_handler_arg = malloc(sizeof(msg_handler_arg_t));
    msg_handler_arg->thread_id = &msg_handler_thread;
    msg_handler_arg->cond = 1;

	/* create msg handler thread */
    if(DEBUG)
        printf("Open\n");

	pthread_create(&msg_handler_thread, NULL,
            msg_handler, (void *)msg_handler_arg);

    hb_arg = malloc(sizeof(msg_handler_arg_t));
    hb_arg->thread_id = &hb_thread;
    hb_arg->cond = 1;

    pthread_create(&hb_thread, NULL,
            hb_handler, (void *)hb_arg);


    return 1;
}

int
mrt_connect(char *peer_ip, int peer_port)
{
    send_packet(peer_ip, peer_port, -1, 10);

    srand(time(0));
    int client_hash = rand();
    int conn_id;
    struct sockaddr_in servaddr;

    if(DEBUG) printf("Sending connection request to port: %d\n", peer_port);

    memset(&servaddr, 0, sizeof(servaddr));

    // Filling server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(peer_port);
    //inet_aton(ip, &servaddr.sin_addr);
    //servaddr.sin_addr.s_addr = inet_addr(ip);
    servaddr.sin_addr.s_addr = inet_addr(peer_ip);

    pthread_mutex_lock(&con_req_access_lock);

    conn_id = conn_count;
    client_t *client = malloc(sizeof(client_t));
    client->should_disconnect = 1;
    pthread_mutex_init(&client->msg_access_lock, NULL);
    client->connection = NULL;
    clients[conn_id] = client;

    pthread_mutex_unlock(&con_req_access_lock);

    _send_conn_req((struct sockaddr *)&servaddr, PORT, client_hash, conn_id, PACKETS_PER_CONN);

    struct timespec ts, ts1;
    ts.tv_sec = 0;
    ts.tv_nsec = 3 * 1000;
    while(client->connection == NULL) {
        nanosleep(&ts, &ts1);
        _send_conn_req((struct sockaddr *)&servaddr, PORT, client_hash, conn_id, PACKETS_PER_CONN);
    }

    return conn_id;
}

int
mrt_accept1()
{
    if(conn_count >= nb_conn) {
        /* We cannot accept any more connection */
        return -1;
    }

    /* We block until we accept a connection */
    while(con_req_head == NULL){}

    connection_t *connection = malloc(sizeof(connection_t));
    connection->sockfd = sockfd;

    /* putting it in list for accept() to accpet it */
    pthread_mutex_lock(&con_req_access_lock);

    con_req_t *cur = con_req_head;
    con_req_head = cur->next;

    client_t *client = malloc(sizeof(client_t));
    client->should_disconnect = 0;
    pthread_mutex_init(&client->msg_access_lock, NULL);
    clients[conn_count] = client;

    /* We accept the connection */
    connection->my_id = cur->sender_id;
    connection->window_size = cur->window_size;
    connection->segment_id = 0;
    connection->rcv_id = 0;
    connection->go_back_n = 0;
    connection->pending = 0;
    connection->close_rcvd = 0;
    memcpy(&(connection->servaddr), &(cur->cliaddr), sizeof(struct sockaddr_in));
    memcpy(&(clients[conn_count]->cliaddr), &(cur->cliaddr), sizeof(struct sockaddr_in));

    clients[conn_count]->msgs = malloc(PACKETS_PER_CONN * sizeof(msg_t *));
    int i;
    for(i = 0; i < PACKETS_PER_CONN; i++) {
        clients[conn_count]->msgs[i] = malloc(sizeof(msg_t));
    }
    clients[conn_count]->msg_count = 0;
    clients[conn_count]->read_head = 0;
    clients[conn_count]->write_head = 0;
    clients[conn_count]->client_hash = cur->client_hash;
    clients[conn_count]->last_received = -1;
    clients[conn_count]->total_size = 0;
    clients[conn_count]->connection = connection;

    if(DEBUG) printf("Connection accepted\n");
    _send_conn_req_ack((struct sockaddr *)&(clients[conn_count]->cliaddr), conn_count);

    conn_count++;
    cur_conn_count++;

    pthread_mutex_unlock(&con_req_access_lock);

    free(cur);

    return conn_count - 1;
}

int mrt_accept_protected1(char *peer_ip, int peer_port){
	send_packet(peer_ip, peer_port, -1, 10);
	return mrt_accept1();
}

int
mrt_receive(int peer_id, void *data, int size)
{
    client_t *client = clients[peer_id];
    if(DEBUG)
        printf("receive\n");

    int so_far = 0;
    int len;
    int idx = 0;
    int last_segment_id = -1;

    if (client != NULL) {
        while(so_far < size) {
            last_segment_id = -1;
            pthread_mutex_lock(&client->msg_access_lock);

            while(client->msg_count != 0 && so_far < size) {

                idx = client->read_head;
                len = client->msgs[idx]->len;
                memcpy(((char*)data)+so_far, client->msgs[idx]->payload, len);
                so_far += len;

                if(DEBUG) printf("=== So far : %d len: %d idx: %d msg_count: %d segment_id: %d ===\n", so_far, len, idx, client->msg_count, client->msgs[idx]->segment_id);

                client->msg_count--;
                last_segment_id = client->msgs[idx]->segment_id;
                client->read_head =
                    (client->read_head + 1) % PACKETS_PER_CONN;

                int window_size = PACKETS_PER_CONN - client->msg_count;
                _send_data_ack((struct sockaddr *)&client->cliaddr, client->connection->my_id,
                               window_size, last_segment_id, 0);

                client->total_size += len;
                client->connection->pending++;
            }

            pthread_mutex_unlock(&client->msg_access_lock);
        }
    }


    if(DEBUG)
        printf("size: %d\n", size);

    assert(size == so_far);

    client->connection->pending = 0;

    return 0;
}

int
mrt_probe_conn_req()
{
    int ret = 0;
    /* putting it in list for accept() to accpet it */
    pthread_mutex_lock(&con_req_access_lock);
    if (con_req_head) {
        ret = 1;
    }
    pthread_mutex_unlock(&con_req_access_lock);

    return ret;
}

int _min(int a, int b) {return (a > b) ? b : a;}

int
mrt_send(int peer_id, void *data, int size)
{
    client_t *client = clients[peer_id];
    if(DEBUG)
        printf("Sending Data please wait until you receive completion notice\n");

    /* Prepare send buffers */
    int sent_segment_id = client->connection->segment_id;
    int len = size, i;
    int nb_buffers = 0;
    if(len % PAYLOAD_SIZE != 0) {
        nb_buffers = (len / PAYLOAD_SIZE) + 1;
    } else {
        nb_buffers = (len / PAYLOAD_SIZE);
    }

    if(len == 0 || len < PAYLOAD_SIZE ) {nb_buffers = 1;}

    msg_t **buffers = malloc(nb_buffers * sizeof(msg_t*));

    if(DEBUG)
        printf("rcv_segment_id %d, connection_segment_id %d nb_buffer: %d "
           " window_size %d len %d\n", 
            client->connection->rcv_id, client->connection->segment_id,  nb_buffers, client->connection->window_size,
            len);

    for (i = 0; i < nb_buffers; i++) {
        buffers[i] = malloc(sizeof(msg_t));
        buffers[i]->msg_type = DATA;
        buffers[i]->sender_id = client->connection->my_id;
        buffers[i]->segment_id = sent_segment_id++;
        /* copy actual data */
        if (len < PAYLOAD_SIZE) {
            memcpy(&(buffers[i]->payload), ((char *)data) + (PAYLOAD_SIZE * i), len);
            buffers[i]->len = len;
            buffers[i]->hash = _calculate_hash(((char *)data) + (PAYLOAD_SIZE * i), buffers[i]->len);

        } else {
            memcpy(&(buffers[i]->payload), ((char *)data) + (PAYLOAD_SIZE * i),
                    PAYLOAD_SIZE);
            len -= PAYLOAD_SIZE;
            buffers[i]->len = PAYLOAD_SIZE;
            buffers[i]->hash = _calculate_hash(((char *)data) + (PAYLOAD_SIZE * i), buffers[i]->len);
        }
    }

    /* Send data */
    struct timespec ts, ts1;
    ts.tv_sec = 0;
    ts.tv_nsec = 3 * 1000;

    while (sent_segment_id - client->connection->rcv_id != 0) {
        client->connection->go_back_n = 0;

        int idx = client->connection->rcv_id - client->connection->segment_id;
        int to_send = sent_segment_id - idx;

        int sent = 0;
        if(DEBUG)
            printf("trying to send rcv_segment_id %d, connection_segment_id %d nb_buffer: %d window %d\n",
                client->connection->rcv_id, client->connection->segment_id,  nb_buffers, client->connection->window_size);
        int smaller = _min(to_send, client->connection->window_size);

        while (smaller > 0) {
            if(DEBUG)
                printf("buffers[%d] len: %d\n", idx, buffers[idx]->len);
            /* Send data */
            sendto(sockfd, (const char *)buffers[idx], sizeof(msg_t),
                    0, (const struct sockaddr *)&(client->connection->servaddr),
                        sizeof(struct sockaddr));

            smaller -= 1;
            idx += 1;
            sent++;
            client->connection->pending++;
        }

        int try = 0;
        while ((!client->connection->go_back_n) &&
               ((sent_segment_id - client->connection->rcv_id) != 0) &&
               (client->connection->rcv_id - client->connection->segment_id < sent)) {
            nanosleep(&ts, &ts1);
            try++;
            if(try > 100) {
                break;
            }

        }
    }

    for (i = 0; i < nb_buffers; i++) {
        free(buffers[i]);
    }
    free(buffers);

    client->connection->segment_id = sent_segment_id;

    client->connection->pending = 0;

    if(DEBUG)
        printf("Send complete\n");

    return 1;
}

int
mrt_disconnect(int peer_id)
{
    client_t *peer = clients[peer_id];

    if(!peer->should_disconnect) return 1;

    connection_t *connection = peer->connection;

    if(DEBUG)
        printf("Disconnection close sent\n");

    _send_disconn_req((struct sockaddr *)&connection->servaddr, connection->my_id);

    struct timespec ts, ts1;
    ts.tv_sec = 0;
    ts.tv_nsec = 30 * 1000;

    int try = 0;
    while(1) {
        nanosleep(&ts, &ts1);
        pthread_mutex_lock(&con_req_access_lock);
        if(!peer->connection->close_rcvd) {
            _send_disconn_req((struct sockaddr *)&connection->servaddr, connection->my_id);
            try++;
            if (try > 2000) {
                /*if(peer->connection != NULL) {
                    free(peer->connection);
                    peer->connection = NULL;
                }*/
                pthread_mutex_unlock(&con_req_access_lock);
                break;
            }
        } else {
            pthread_mutex_unlock(&con_req_access_lock);
            break;
        }
        pthread_mutex_unlock(&con_req_access_lock);
    }

    return 1;
}

int
mrt_close()
{
    int i, j;
    if(DEBUG)
        printf("close\n");

    msg_handler_arg->cond = 0;
	pthread_join(msg_handler_thread, NULL);
    free(msg_handler_arg);

    hb_arg->cond = 0;
	pthread_join(hb_thread, NULL);
    free(hb_arg);

    /* make sure we complete everything */

    /* free clients */
    for(i = 0; i < nb_conn; i++) {
        if(clients[i] != NULL) {
            for(j = 0; j < PACKETS_PER_CONN; j++) {
                free(clients[i]->msgs[j]);
            }
            free(clients[i]->msgs);
            free(clients[i]->connection);
            free(clients[i]);
        }
    }
    free(clients);

    pthread_exit(NULL);
    return 1;
}
