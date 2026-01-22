#include "utils.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "yyt_error.h"


#define MAX_CONNECTIONS 1000

int rank_fd[MAX_CONNECTIONS];
int listen_fd, connect_fd;

void socket_init(int group_size, int rank, uint32_t ip, uint16_t port)
{
    printf("target address: %x:%d\n", ip, port);
    int ret;
    
    struct sockaddr_in rank0_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = { .s_addr = htonl(ip) },
        .sin_zero = {},
    };

    if(rank == 0) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        MYCHECK(listen_fd < 0, "Error on socket()");

        int opt = 1;
        ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));
        MYCHECK(ret < 0, "Error on setsockopt()");

        for(int r = 0; r < group_size; r++) rank_fd[r] = -1;

        ret = bind(listen_fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        MYCHECK(ret < 0, "Error on bind()");
        ret = listen(listen_fd, MAX_CONNECTIONS);
        MYCHECK(ret < 0, "Error on listen()");
        // gather
        for(int r = 1; r < group_size; r++) {
            int conn_fd = accept(listen_fd, NULL, NULL);
            MYCHECK(conn_fd < 0, "Error on accept()");
            int client_rank;
            ret = read(conn_fd, &client_rank, sizeof(client_rank));
            MYCHECK(!(0<client_rank && client_rank<group_size), "Rank error");
            MYCHECK(rank_fd[client_rank] != -1, "Rank error");
            MYCHECK(ret < sizeof(client_rank), "Partial read()");

            rank_fd[client_rank] = conn_fd;
        }        
    }
    else {
        connect_fd = socket(AF_INET, SOCK_STREAM, 0);
        MYCHECK(connect_fd < 0, "Error on socket()");
        ret = connect(connect_fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        MYCHECK(ret < 0, "Error on connect()");
        ret = write(connect_fd, &rank, sizeof(rank));
        MYCHECK(ret < sizeof(rank), "Partial write()");
    }
}

int main(int argc, char **argv)
{
    int group_size = atoi(argv[1]);
    int rank = atoi(argv[2]);
    uint32_t ip = htonl(inet_addr(argv[3]));// string to int32 IP (little endian)
    uint16_t port = atoi(argv[4]);
    socket_init(group_size, rank, ip, port);
    return 0;
}