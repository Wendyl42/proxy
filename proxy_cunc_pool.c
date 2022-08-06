#include <stdio.h>

#include "csapp.h"
#include "sbuf.h"
/* Recommended max cache and object sizes */
#define MAX_OBJECT_SIZE 102400
/* Size of thread pool and sbuf */
#define NTHREADS 8
#define SBUFSIZE 32

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *request_line_f = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key = "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

sbuf_t sbuf; /* Shared buffer of connfd */

void *thread(void *vargp);
void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_msg(char *http_msg, char *hostname, char *path, int port,
                    rio_t *client_rio);
int connect_endServer(char *hostname, int port);

int main(int argc, char **argv) {
    int listenfd, connfd;
    pthread_t tid;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage :%s <port> \n", argv[0]);
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);

    listenfd = Open_listenfd(argv[1]);

    sbuf_init(&sbuf, SBUFSIZE);
    for (int i = 0; i < NTHREADS; ++i) Pthread_create(&tid, NULL, thread, NULL);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        /* print accepted message */
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port,
                    MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);

        /* insert connfd into sbuf, so other thread can fetch the job */
        sbuf_insert(&sbuf, connfd);
    }
    Close(listenfd);
    sbuf_deinit(&sbuf);
    return 0;
}

void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    int connfd = sbuf_remove(&sbuf); /* not from arg, but from sbuf */
    doit(connfd);
    Close(connfd);
}

/* get request msg, transfer to server, get respond msg, and transfer to client
 */
void doit(int connfd) {
    int end_serverfd; /* the end server file descriptor */

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_msg[MAXLINE];
    /* store the request line arguments */
    char hostname[MAXLINE], path[MAXLINE];
    int port;

    rio_t rio,      /* this for clientfd */
        server_rio; /* and this for server fd */

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    /* read the request line from msg */
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        clienterror(connfd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }
    /* parse the uri to get hostname, file path, port */
    parse_uri(uri, hostname, path, &port);

    /* make our HTTP/1.0 request msg, including request line and headers */
    build_http_msg(endserver_http_msg, hostname, path, port, &rio);

    /* connect to the endserver */
    end_serverfd = connect_endServer(hostname, port);
    if (end_serverfd < 0) {
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&server_rio, end_serverfd);
    /* send our HTTP/1.0 request msg to endserver */
    Rio_writen(end_serverfd, endserver_http_msg, strlen(endserver_http_msg));

    /* receive msg from endserver and send it to the client */
    size_t n;
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        printf("proxy received %d bytes,then send\n", n);
        Rio_writen(connfd, buf, n);
    }
    /* do not forget this */
    Close(end_serverfd);
}

void build_http_msg(char *http_msg, char *hostname, char *path, int port,
                    rio_t *client_rio) {
    char buf[MAXLINE], request_line[MAXLINE], other_hdr[MAXLINE],
        host_hdr[MAXLINE];

    /* request line */
    sprintf(request_line, request_line_f, path);
    /* request headers */
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        /* EOF */
        if (strcmp(buf, endof_hdr) == 0) break;

        /* host header */
        if (!strncasecmp(buf, host_key, strlen(host_key))) {
            strcpy(host_hdr, buf);
            continue;
        }

        /* other header except host/user_agent/conn/proxy/eof */
        if (!strncasecmp(buf, connection_key, strlen(connection_key)) &&
            !strncasecmp(buf, proxy_connection_key,
                         strlen(proxy_connection_key)) &&
            !strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
            strcat(other_hdr, buf);
        }
    }
    /* if client msg didn't have host header, we have to make our own */
    if (strlen(host_hdr) == 0) {
        sprintf(host_hdr, host_hdr_format, hostname);
    }
    /* concat request line and headers into msg */
    sprintf(http_msg, "%s%s%s%s%s%s%s", request_line, host_hdr, conn_hdr,
            prox_hdr, user_agent_hdr, other_hdr, endof_hdr);
    return;
}

/* Connect to the end server */
inline int connect_endServer(char *hostname, int port) {
    char portStr[100];
    sprintf(portStr, "%d", port); /* just cast int to str */
    return Open_clientfd(hostname, portStr);
}

void parse_uri(char *uri, char *hostname, char *path, int *port) {
    *port = 80;
    char *pos = strstr(uri, "//");

    pos = pos != NULL ? pos + 2 : uri;

    char *pos2 = strstr(pos, ":");
    if (pos2 != NULL) {
        *pos2 = '\0';
        sscanf(pos, "%s", hostname);
        sscanf(pos2 + 1, "%d%s", port, path);
        *pos2 = ':'; /* change it back, since the uri cannot be modified */
    } else {
        pos2 = strstr(pos, "/");
        if (pos2 != NULL) {
            *pos2 = '\0';
            sscanf(pos, "%s", hostname);
            *pos2 = '/'; /* change it back */
            sscanf(pos2, "%s", path);
        } else {
            sscanf(pos, "%s", hostname);
            sscanf("/", "%s", path);
        }
    }
    return;
}

/* returns an error message to the client, code from book CSAPP */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf,
            "<body bgcolor="
            "ffffff"
            ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}