#include "syshead.h"
#include "utils.h"
#include "socket.h"
#include "inet.h"
#include "wait.h"

static int sock_amount = 0;
static LIST_HEAD(sockets);

extern struct net_family inet;

static struct net_family *families[128] = {
    [AF_INET] = &inet,
};

static struct socket *alloc_socket(pid_t pid)
{
    // TODO: Figure out a way to not shadow kernel file descriptors.
    // Now, we'll just expect the fds for a process to never exceed this.
    static int fd = 4097;
    struct socket *sock = malloc(sizeof (struct socket));
    list_init(&sock->list);

    sock->pid = pid;
    sock->fd = fd++;
    sock->state = SS_UNCONNECTED;
    wait_init(&sock->sleep);
    sock_amount++;
    
    return sock;
}

static int free_socket(struct socket *sock)
{
    if (sock->ops) {
        sock->ops->free(sock);
    }
    
    sock_amount--;
    return 0;
}

void free_sockets() {
    struct list_head *item, *tmp;
    struct socket *sock;

    list_for_each_safe(item, tmp, &sockets) {
        sock = list_entry(item, struct socket, list);
        list_del(item);
        sock->ops->free(sock);
        free(sock);
    }
}

static struct socket *get_socket(pid_t pid, int fd)
{
    struct list_head *item;
    struct socket *sock = NULL;

    list_for_each(item, &sockets) {
        sock = list_entry(item, struct socket, list);
        if (sock->pid == pid && sock->fd == fd) return sock;
    }
    
    return NULL;
}

struct socket *socket_lookup(uint16_t sport, uint16_t dport)
{
    struct list_head *item;
    struct socket *sock = NULL;
    struct sock *sk = NULL;

    list_for_each(item, &sockets) {
        sk = list_entry(item, struct socket, list)->sk;

        if (sk != NULL && sk->sport == sport && sk->dport == dport) return sock;
    }
    
    return NULL;
}

int _socket(pid_t pid, int domain, int type, int protocol)
{
    struct socket *sock;
    struct net_family *family;

    if ((sock = alloc_socket(pid)) == NULL) {
        print_error("Could not alloc socket\n");
        return -1;
    }

    sock->type = type;

    printf("pid %d\n", pid);
    printf("domain %x\n", domain);
    printf("type %x\n", type);
    printf("protocol %x\n", protocol);

    family = families[domain];

    if (!family) {
        print_err("Domain not supported: %d\n", domain);
        goto abort_socket;
    }
    
    if (family->create(sock, protocol) != 0) {
        print_err("Creating domain failed\n");
        goto abort_socket;
    }

    list_add_tail(&sock->list, &sockets);

    return sock->fd;

abort_socket:
    free_socket(sock);
    return -1;
}

int _connect(pid_t pid, int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    struct socket *sock;

    if ((sock = get_socket(pid, sockfd)) == NULL) {
        print_error("Could not find socket for connection\n");
        return -1;
    }

    return sock->ops->connect(sock, addr, addrlen, 0);
}

int _write(pid_t pid, int sockfd, const void *buf, const unsigned int count)
{
    struct socket *sock;

    if ((sock = get_socket(pid, sockfd)) == NULL) {
        print_error("Could not find socket for connection\n");
        return -1;
    }

    return sock->ops->write(sock, buf, count);
}

int _read(pid_t pid, int sockfd, void *buf, const unsigned int count)
{
    struct socket *sock;

    if ((sock = get_socket(pid, sockfd)) == NULL) {
        print_error("Could not find socket for connection\n");
        return -1;
    }

    return sock->ops->read(sock, buf, count);
}

int _close(pid_t pid, int sockfd)
{
    struct socket *sock;

    if ((sock = get_socket(pid, sockfd)) == NULL) {
        print_error("Could not find socket for connection\n");
        return -1;
    }

    return sock->ops->free(sock);
}
