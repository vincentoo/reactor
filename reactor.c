#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#define MAX_EVENTS (1024)
#define BUF_LEN (4096)
#define SERV_PORT (9999)

typedef struct RE_EVENT_T{
    int     fd;
    int     events;
    void    *arg;
    int     status;         /*1: 在监听事件中，0：不在*/
    char    buf[BUF_LEN];
    int     buflen;
    long    last_active_time;
    
    void (*cb)(int fd, int events, void *arg);
}re_event_t;

int g_efd; /*epoll_create 返回的fd*/
re_event_t g_events[MAX_EVENTS+1]; /*+1 最后一个用于listen fd*/

void re_event_set(re_event_t *ev, int fd, void (*cb)(int fd, int events, void *arg), void *arg)
{
    ev->fd  = fd;
    ev->cb  = cb;
    ev->arg = arg;
    ev->status = 0;
    //ev->buflen = 0;
    //memset ((void *)ev->buf, 0, BUF_LEN);
    ev->last_active_time = time(NULL);
    return;
}

void re_recv_data(int fd, int events, void *arg);
void re_send_data(int fd, int evnets, void *arg);

void re_event_add(int efd, int events, re_event_t *ev)
{
    struct epoll_event epv = {0, {0}};
    int op;

    epv.data.ptr = ev;
    epv.events = ev->events = events;

    if (ev->status == 1) {
        op = EPOLL_CTL_MOD;
    } else {
        op = EPOLL_CTL_ADD;
        ev->status = 1;
    }

    if (epoll_ctl(efd, op, ev->fd, &epv) < 0) {
        printf("[%s %d] event add FAILED fd[%d] op[%d] evnets[%d]\n", __FUNCTION__, __LINE__, ev->fd, op, events);
    } else {
        printf("[%s %d] event add OK fd[%d] op[%d] evnets[%d]\n", __FUNCTION__, __LINE__, ev->fd, op, events);
    }
    
    return;
}

void re_event_del(int efd, re_event_t *ev)
{
    struct epoll_event epv = {0, {0}};

    if (ev->status != 1) {
        printf("[%s %d] status[%d]\n", __FUNCTION__, __LINE__, ev->status);
        return;
    }

    epv.data.ptr = ev;
    ev->status = 0;

    if (epoll_ctl(efd, EPOLL_CTL_DEL, ev->fd, &epv) < 0) {
        printf("[%s %d] event del FAILED fd[%d] op[%d]\n", __FUNCTION__, __LINE__, ev->fd, EPOLL_CTL_DEL);
    } else {
        printf("[%s %d] event del OK fd[%d] op[%d]\n", __FUNCTION__, __LINE__, ev->fd, EPOLL_CTL_DEL);
    }
    
    return;
}

void re_accept_conn(int lfd, int events, void *arg)
{
    struct sockaddr_in cin;
    socklen_t len = sizeof(cin);
    int cfd, i;

    if ((cfd = accept(lfd, (struct sockaddr *)&cin, &len)) == -1) {
        if (errno != EAGAIN && errno != EINTR) {
            /*暂时不做处理*/
        }
        
        printf("[%s %d] accept %s\n", __FUNCTION__, __LINE__, strerror(errno));
        return;
    }

    do {
        for (i = 0; i < MAX_EVENTS; i++) {
            if (g_events[i].status == 0)
                break;
        }

        if (i == MAX_EVENTS) {
            printf("[%s %d] max conn litmit[%d]\n", __FUNCTION__, __LINE__, MAX_EVENTS);
            break;
        }

        int flag = 0;
        if ((flag = fcntl(cfd, F_SETFL, O_NONBLOCK)) < 0) {
            printf("[%s %d] fcntl none blocking failed %s\n", __FUNCTION__, __LINE__, strerror(errno));
            break;
        }

        re_event_set(&g_events[i], cfd, re_recv_data, &g_events[i]);
        re_event_add(g_efd, EPOLLIN, &g_events[i]);
    }while(0);

    printf("new connect [%s:%d] [time:%ld], pos[%d]\n", inet_ntoa(cin.sin_addr), ntohs(cin.sin_port), g_events[i].last_active_time, i);

    return;
}

void re_recv_data(int fd, int events, void * arg)
{
    re_event_t *ev = (re_event_t *)arg;
    int len;

    len = recv(fd, ev->buf, BUF_LEN, 0);
    re_event_del(g_efd, ev);

    if (len > 0) {
        ev->buflen = len;
        ev->buf[len] = '\0';

        printf("C[%d]: %s\n", fd, ev->buf);

        re_event_set(ev, fd, re_send_data, ev);
        re_event_add(g_efd, EPOLLOUT, ev);
    } else if (len == 0) {
        close(ev->fd);
        
        /* ev-g_events 地址相减得到偏移元素位置 */
        printf("[fd=%d] pos[%d], closed\n", fd, (int)(ev - g_events));
    } else {
        close(ev->fd);
        printf("recv[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
    }
    return;
}

void re_send_data(int fd, int evnets, void *arg)
{
    re_event_t *ev = (re_event_t *)arg;
    int len;

    len = send(fd, ev->buf, ev->buflen, 0);
    re_event_del(g_efd, ev);

    if (len > 0) {
        printf("send[fd=%d], len[%d] buf[%s]\n", fd, len, ev->buf);
        
        re_event_set(ev, fd, re_recv_data, ev);
        re_event_add(g_efd, EPOLLIN, ev);
    } else {
        close(ev->fd);

        printf("close[fd=%d], len[%d] buf[%s]\n", fd, len, ev->buf);
        printf("send[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
    }
    
    return;
}

void re_init_listen_socket(int efd, short port)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(lfd, F_SETFL, O_NONBLOCK);
    
    re_event_set(&g_events[MAX_EVENTS], lfd, re_accept_conn, &g_events[MAX_EVENTS]);
    re_event_add(efd, EPOLLIN, &g_events[MAX_EVENTS]);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);
    
    bind(lfd, (struct sockaddr *)&sin, sizeof(sin));
    listen(lfd, 20);
    
    return;
}

int main(int argc, char **argv)
{
    unsigned short port = SERV_PORT;
    
    if (argc == 2)
        port = atoi(argv[1]);
    
    g_efd = epoll_create(MAX_EVENTS + 1);

    if (g_efd <= 0)
        printf("create efd in %s err %s\n", __func__, strerror(errno));

    re_init_listen_socket(g_efd, port);

    struct epoll_event events[MAX_EVENTS + 1];

    printf("server running:port[%d]\n", port);
    
    int checkpos = 0, i;

    /* 
    ** 超时验证，每次测试100个链接，不测试listenfd 
    ** 当客户端60秒内没有和服务器通信，则关闭此客户端链接 
    */
    while(1) {
        long time_now = time(NULL);
        for (i = 0; i < 100; i++, checkpos++) {
            if (checkpos == MAX_EVENTS)
                checkpos = 0;

            if (g_events[checkpos].status != 1)
                continue;

            long duration = time_now - g_events[checkpos].last_active_time;
            if (duration >= 60) {
                close(g_events[checkpos].fd);

                printf("[fd=%d] timeout\n", g_events[checkpos].fd);
                re_event_del(g_efd, &g_events[checkpos]);
            }
        }

        int nfd = epoll_wait(g_efd, events, MAX_EVENTS+1, 1000);
        if (nfd < 0) {
            printf("epoll_wait error, exit\n");
            break;
        }

        for (i = 0; i < nfd; i++) {
            re_event_t *ev = (re_event_t *)events[i].data.ptr;

            if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) {
                ev->cb(ev->fd, events[i].events, ev->arg);
            }
            
            if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT)) {
                ev->cb(ev->fd, events[i].events, ev->arg);
            }
        }
    }

    /*TODO: 退出前释放所有资源 */
    return 0;
}


