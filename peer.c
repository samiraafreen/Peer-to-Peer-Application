#include <stdio.h>
#include <string.h> //strlen
#include <stdlib.h>
#include <errno.h>
#include <unistd.h> //close
#include <sys/types.h>
#include <sys/time.h> //FD_SET, FD_ISSET, FD_ZERO macros
#include <assert.h>

#include "mrt.h"

int generate(char a, void *buf, int count)
{
    char *data = (char *)buf;
    int i;
    char c = a;
    for (i = 0; i < count; i++){
        if (i % 15 == 0) {
            c = a;
        }
        data[i] = c;
        c += 1;
    }
    return 1;
}

int check(char a, void *buf, int count)
{
    char *data = (char *)buf;
    int i;
    char c = a;
    for (i = 0; i < count; i++){
        if (i % 15 == 0) {
            c = a;
        }
        assert(data[i] == c);
        c += 1;
    }
    return 1;
}

int
main(int argc, char **argv)
{
    int connections = atoi(argv[1]);
    int option = atoi(argv[2]);
    int my_port = atoi(argv[3]);

    int peer_port;
    char *peer_ip;
    peer_ip = argv[4];
    peer_port = atoi(argv[5]);

    int size = 10024;

    mrt_open(connections, my_port);

    if(option == 1) {
        int peer = mrt_connect(peer_ip, peer_port);

        printf("Connection Established\n");

        char *data = malloc(sizeof(char) * size);

        mrt_receive(peer, (void *)data, size);
        check('a', data, size);
        printf("Data received and verified\n");


        mrt_send(peer, data, size);
        printf("Data sent\n");

        mrt_disconnect(peer);
    } else {
        int peer = mrt_accept_protected1(peer_ip, peer_port);

        printf("Connection Established\n");

        char *data = malloc(sizeof(char) * size);
        generate('a', (void *)data, size);

        mrt_send(peer, data, size);
        printf("Data sent\n");


        mrt_receive(peer, (void *)data, size);
        check('a', data, size);
        printf("Data received and verified\n");

        while(cur_conn_count != 0) {
        }
    }

    printf("Finishing\n");
    mrt_close();

    return 0;
}
