/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Ragnar Pálsson, ragnarp12@ru.is
 *     Kristinn Vignisson, kristinnv12@ru.is
 *
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */

#include "csapp.h"

#define PROXYFILE "proxy.log"

// Create struct to hold info about our client.
// Contains file descriptor and client ip address.
// Using typedef so we can use client_info
// instead of struct client_info
struct client_info
{
    struct sockaddr_in clientaddr;
    int connfd;
};

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

void *thread(void *myclientp);
void deliver(void *myclientp);

/* Following functions are necessary so the program won't end.
 * Gracefully deliver to user his request or write EOF
 */
ssize_t Rio_readn_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);

// Add comments
void Rio_writen_w(int fd, void *usrbuf, size_t n);

// Thread safe open_clientfd
int open_clientfd_ts(char *hostname, int portno);

// Logger which output to a file what uri user is requesting
void logging( void *myclientp, char *uri, int size);

sem_t clientmutex;

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int port,
        //connfd,
        listenfd;

    socklen_t clientlen = sizeof(struct sockaddr_in);

    struct client_info *myclient;
    pthread_t tid;

    /* Check arguments */
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }
    //bls 953
    //920
    //943
    //
    //Get user port number
    port = atoi(argv[1]);

    printf("Proxy server starting at port %d\n", port);

    // Open listening file descriptor
    listenfd = open_listenfd(port);

    // Initialize Semaphore so we can lock the log for each thread
    sem_init(&clientmutex, 1, 1);

    //http://stackoverflow.com/questions/108183/how-to-prevent-sigpipes-or-handle-them-properly
    // Ignore broken pipe if we write to socket.
    signal(SIGPIPE, SIG_IGN);

    while (1)
    {
        myclient = (struct client_info *) Malloc (sizeof (struct client_info));
        myclient->connfd = Accept(listenfd, (SA *) & (myclient->clientaddr), &clientlen);

        ///printf("Houston, this is %d. We have a connection\n", myclient->connfd);


        // bls 955
        // http://stackoverflow.com/questions/6524433/passing-multiple-arguments-to-a-thread-in-c-pthread-create
        // http://www.just4tech.com/2013/09/pass-multiple-arguments-in-pthreadcreate.html
        //Pthread_create(&tid, NULL, thread, (void *) myclient);
        Pthread_create(&tid, NULL, thread, myclient);
    }

    Pthread_exit(NULL);

    exit(0);
}

/*
 * A simple thread function which calls deliver()
 * and cleans up afterwards.
 * To do: Needs checking
 */
void *thread(void *myclientp)
{
    struct client_info *myclient = (struct client_info *) myclientp;

    Pthread_detach(pthread_self());

    deliver(myclient);
    Close(myclient->connfd);

    Free(myclientp);
    myclientp = NULL;

    return NULL;
}

/*
 * This is the actual function which handles the I/O to user.
 */
void deliver(void *myclientp)
{
    int serverfd;
    int tmpport;

    char buf[MAXLINE],
         method[MAXLINE],
         uri[MAXLINE],
         version[MAXLINE],
         hostname[MAXLINE],
         path[MAXLINE];

    rio_t rio, rioserver;
    struct client_info *myclient = (struct client_info *) myclientp;

    Rio_readinitb(&rio, myclient->connfd);
    rio_readlineb(&rio, buf, MAXLINE);

    // Get user header and split it to variables
    sscanf(buf, "%s %s %s", method, uri, version);

    // Parse uri to following chars.q
    parse_uri(uri, hostname, path, &tmpport);

    // For debugging
    printf("Sending > uri: %s , hostname: %s:%d, path: %s\n", uri, hostname, tmpport, path);

    // Open fd for the server request
    // and initialize the reading from server.
    serverfd = open_clientfd(hostname, tmpport);
    Rio_readinitb(&rioserver, serverfd);


    /*
     * Add here the loop, request from client to server
     * and the response from server to client
     */


    // Loop through response
    // 973

    // MAXLINE is not suppose to be here
    logging(myclient, uri, MAXLINE);

    return;
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0)
    {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')
        *port = atoi(hostend + 1);

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL)
    {
        pathname[0] = '\0';
    }
    else
    {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}


void logging( void *myclientp, char *uri, int size)
{
    FILE *proxylog;
    char logs[MAXLINE];

    struct client_info *myclient = (struct client_info *)myclientp;

    // Lock the log so only one thread write to it each time
    P(&clientmutex);

    // Format the text for output
    format_log_entry(logs, &myclient->clientaddr, uri, size);


    // Open PROXYFILE with append sign
    proxylog = Fopen(PROXYFILE, "a");
    Fwrite (logs , 1, strlen(logs), proxylog);
    fflush(proxylog);
    Fclose(proxylog);

    // Unlock it so everyone has access to it
    V(&clientmutex);

    return;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d\n", time_str, a, b, c, d, uri, size);
}


ssize_t Rio_readn_w(int fd, void *usrbuf, size_t n)
{
    ssize_t tmp;

    if ((tmp = rio_readn(fd, usrbuf, n)) >= 0)
    {
        return tmp;
    }

    return 0; // EOF so we won't close
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t tmp;

    if ( (tmp = rio_readlineb(rp, usrbuf, maxlen)) >= 0)
    {
        return tmp;
    }

    return 0; // EOF so we won't close
}

void Rio_writen_w(int fd, void *usrbuf, size_t n)
{
    if (rio_writen(fd, usrbuf, n) != n)
    {
        printf("Error: rio_writer failed\n");
    }
}

/*
 * Definitely not done. Only copy/paste of csapp.c
 * open_clientfd() plus a Malloc.
 */
int open_clientfd_ts(char *hostname, int portno)
{
    int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;

    P(&clientmutex);
    hp = (struct hostent *) Malloc(sizeof(struct hostent));

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; /* check errno for cause of error */

    /* Fill in the server's IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL)
        return -2; /* check h_errno for cause of error */

    bzero((char *) &serveraddr, sizeof(serveraddr));

    serveraddr.sin_family = AF_INET;

    bcopy((char *)hp->h_addr_list[0],
          (char *)&serveraddr.sin_addr.s_addr, hp->h_length);

    serveraddr.sin_port = htons(portno);

    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    V(&clientmutex);
    return clientfd;
}