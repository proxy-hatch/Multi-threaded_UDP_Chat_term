//// This project is created to fulfill CMPT 300 Assignment 2
////
////
//// Created on: Jun 15, 2017
//// Last Modified: Jun ??, 2017
//// Author: Yu Xuan (Shawn) Wang
//// Email: yxwang@sfu.ca
//// Student #: 301227972

// code based on tutorial referenced by instructor:
// http://beej.us/guide/bgnet/examples/listener.c

// this program ALWAYS define the sender as client and the receiver as server
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/poll.h>       // poll() to check if there is data on stdin buffer
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h> // for INT_MAX
//#include <unistd.h> // for sleep()
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>  //pthread API does NOT set the global variable errno, but instead #defines it thread-local, this is just a fail-safe
#include <signal.h>     //catch SIGINT

#include "LIST.h"

#define NUMTHREADS 4
#define MAXMSGLENGTH 256

// job packages data struct
// contains the type of job, and data (send/received msg)
// used in list->node->data
typedef enum {
    SEND = 0,
    PRINT
} jobType;
typedef struct jobPckg {
    jobType type;
    char msg[MAXMSGLENGTH];
} jobPckg;

// used for opening socket for UDP_in/out, and defining socket_addr for UDP_out
typedef struct sockInfo {
    int sockFD;     // local socket #. -1: invalid
    struct addrinfo *destAddrInfo;
    int errCode;    // 0: no problem; 1: Failed to create and bind receiving socket; 2: Failed to create destination addrinfo struct; 3: unknown
} sockInfo;
// used for sendUDP & rcvUDP upon thread startup
// contains socket info and thread #
typedef struct UDPThreadInitInfo {
    int threadNum;     // local socket #. invalid: -1
    sockInfo *mySock;
} UDPThreadInitInfo;

// global variables
// ptr to the queue data structure that will be used to manage traffic
list *jobMgmtQueue;

// socket file descriptor and info obj. To be passed to the two UDP-related threads
sockInfo *mySock;
UDPThreadInitInfo *mySockPacked;

// these two semaphores are used to notify the threads printScreen() & endUDPDatagram() to wake up
static sem_t print_sem;
static sem_t send_sem;

// mutex for list access
pthread_mutex_t list_mutex;

// condition to signal program exit
pthread_cond_t terminateSIG;

// --------------- Helper funcs ---------------------------------
// best way to convert char[] to int: https://stackoverflow.com/a/22866001
int strtoi(const char *str) {
    char *endptr;
    errno = 0;
    long l = strtol(str, &endptr, 0);
    // we make the exception of allowing trailing \r \n here
    if (errno == ERANGE || (*endptr != '\0' && *endptr != '\n' && *endptr != '\r') || str == endptr) {
        return -1;
    }
    // Only needed if sizeof(int) < sizeof(long)
    if (l < INT_MIN || l > INT_MAX) {
        return -2;
    }
    return (int) l;
}

// check if stdin is empty
// return 1 if not empty, return 0 if empty
int stdinIsNotEmpty(){
    struct pollfd fds;
    fds.fd = 0;       // 0 for stdin
    fds.events = POLLIN;
    if (poll(&fds, 1, 0) != 1)
        // empty stdin buffer or error
        return 0;
    else return 1;
}

// best way to read from stdin: https://stackoverflow.com/a/9278353
// fills the passed in char[] with what's buffered at stdin
// stdin is drained after this call
// CAUTION: this func assumes the size pass in is correct. Ensure this is the case to prevent memory leak
// returns 0 on success, -1 on fail
// if stdin is empty, it waits for stdin (check if stdin is empty before use)
int getstdinStr(char *arr, int size) {
    char *p;
    //reset msg
    memset(arr, 0, sizeof(p) * size);

    // fgets return NULL on failure
    if (fgets(arr, sizeof(p) * size, stdin)) {
        return 0;
    } else
        return -1;
}

// get sockaddr of sender in received UDP msgs, converting to IPv4
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    } else {
        perror("get_in_addr() detected sa->sa_family is not AF_INET (IPv4)");
        return NULL;
    }
}

// build addrinfo linked list to choose from
// build by getaddrinfo()
// note: the user of this func will be responsible for freeing the linked list created
struct addrinfo* getaddrrinfoList(int portNum)
{
    struct addrinfo hints, *servinfo;
    int rv;
    char portNumChar[6];
    sprintf(portNumChar, "%d", portNum);    // since 65535 is max
    // This is the modern way of packing a struct: automatically with getaddrinfo()
    // addrinfo hint is used to specify parameters to setup socket address structure for subsequent use
    memset(&hints, 0, sizeof hints);
    hints.ai_flags = AI_PASSIVE; // use my IP
    hints.ai_family = AF_INET; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;    //any

    //  getaddrinfo() is reentrant and allows programs to eliminate IPv4-versus-IPv6 dependencies.
    if ((rv = getaddrinfo(NULL, portNumChar, &hints, &servinfo)) != 0) {
        // gai_strerror() is used SPECIFICALLY for getaddrinfo() errors
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL;
    }
    return servinfo;
}

// set up UPD socket and bind it to the specified port #, return the socket descriptor
// return -1 upon failing
int setupHostUDPSocket(int portNum) {
    struct addrinfo *p, *servinfo;
    int sockfd = 0;

    servinfo=getaddrrinfoList(portNum);
    if(!servinfo)
    {
        fprintf(stderr, "getaddrinfo failed in setupHostUDPSocket(%d)\n", portNum);
        return -1;
    }
    // loop through all the Internet address results, make a socket, and bind to the first we can (should only be one?)
    for (p=servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("listener: bind");
            close(sockfd);
            continue;
        }
        break;
    }
    // free the linked list
    freeaddrinfo(servinfo);
    // Failed!
    if (!p) {
        return -1;
    } // succeeded!
    else
        return sockfd;
}

// set up the destination socket addr with the specified port #,
// Note: this func assumes the receiver is on the same host (same IP addr)
// returns the addrinfo struct ptr
// returns NULL upon failing
struct addrinfo *setupDestAddrInfo(int serverPortNum) {
    struct addrinfo *p1, *p2;

    p1=getaddrrinfoList(serverPortNum);
    if(!p1)
        return NULL;
    else
    {
        // we only need to keep one valid addrinfo block
        p2=p1->ai_next;
        while(p2)
        {
            free(p1);
            p1=p2;
            p2=p2->ai_next;
        }
        return p1;
    }
}

// ensure port #, open socket for UDP_in/out, and define socket_addr for UDP_out
sockInfo *setupSockInfo(int MYPORT, int SERVERPORT)
{
//    int MYPORT;      // the port THIS instance will be using
//    int SERVERPORT;     // the port the SERVER instance will be using. I.e., port we will attempt to send TO
    int sockFD;
    int i;
    char input[5];    // because 5 digits is the max for port #
    struct addrinfo *destAddrInfo;
    sockInfo *reObj=malloc(sizeof(sockInfo));
    // initialize to error state
    reObj->sockFD=-1;
    reObj->errCode=3;
    reObj->destAddrInfo=NULL;

    // ########## get port # to spawn UPD socket for receiving msgs
    printf("Attempting to setup Local Socket...\n");
    i=5;
    while (i--) {
        if (MYPORT < 1025 || MYPORT > 65535) {
            fprintf(stderr, "Invalid number as port. Try again\n");
        }// try to set up a socket and bind to this port
        else {
            if ((sockFD = setupHostUDPSocket(MYPORT))== -1) {
                fprintf(stderr, "Failed to create and bind receiving socket. Try a different Port #\n");
                reObj->errCode=1;
                reObj->sockFD=-1;
            }// success!
            else {
                reObj->sockFD=sockFD;
                break;
            }
        }
        // returns 0 upon success
        if (!getstdinStr(input, MAXMSGLENGTH))
            MYPORT = strtoi(input);
        // failed, set MYPORT to 0 and try again
        if (MYPORT < 0)
            MYPORT = 0;
    }

    // ########## get server socket addrinfo obj for outgoing UDP msgs
    printf("Attempting to setup Destination (Server) Socket info...\n");
    i = 5;
    while (i--) {
        if (SERVERPORT < 1025 || SERVERPORT > 65535) {
            fprintf(stderr, "Invalid number as destination port. Try again\n");
        }// try to set up a socket with this port
        else {
            if ((destAddrInfo= setupDestAddrInfo(SERVERPORT))==NULL) {
                fprintf(stderr, "Failed to create destination addrinfo struct. Try a different Port #\n");
                reObj->errCode=2;
                reObj->destAddrInfo=NULL;
            }// success!
            else {
                reObj->errCode=0;
                reObj->destAddrInfo=destAddrInfo;
                break;
            }
        }
        // returns 0 upon success
        if (!getstdinStr(input, MAXMSGLENGTH))
            SERVERPORT = strtoi(input);
        // failed, set SERVERPORT to 0 and try again
        if (SERVERPORT < 0)
            SERVERPORT = 0;
    }

    return reObj;
}

// destroy socket & the socket_addr object in a sockInfo obj
void sock_destroy(sockInfo *mySock){
    freeaddrinfo(mySock->destAddrInfo);
    close(mySock->sockFD);
}

// destroy a packed UDPThreadInitInfo_destroy object and everything within it
void UDPThreadInitInfo_destroy(UDPThreadInitInfo *info){
    sock_destroy(info->mySock);
    free(info);
}


// request packages to be put onto the list
void jobEnqueue(jobType type, char *str, list *aList) {
    struct jobPckg *newJob = malloc(sizeof(jobPckg));
    strcpy(newJob->msg, str);
    newJob->type = type;
    // hold key to list and prepend
    pthread_mutex_lock(&list_mutex);
    ListPrepend(aList, newJob);
    pthread_mutex_lock(&list_mutex);
}

// -------------------------------- threads ------------------------------------------------------------
// it is essential to have threads return void*: https://stackoverflow.com/a/10457390
void *recordKBInput(void *t) {
    int myID = (int) t;
    char msg[MAXMSGLENGTH];

    while(myID){
        if (stdinIsNotEmpty()) {
            if (!getstdinStr(msg, MAXMSGLENGTH)) {
                if (strlen(msg)) {
                    // create send & print jobPckg
                    jobEnqueue(PRINT,msg,jobMgmtQueue);
                    jobEnqueue(SEND,msg,jobMgmtQueue);
                    // wake up threads printScreen & sendUDPDatagram if they are currently blocked (print_sem == -1 || send_sem==-1)
                    sem_post(&print_sem);
                    sem_post(&send_sem);
                }
            }
        }
    }
    pthread_exit(NULL);
}

void *rcvUDPDatagram(void *t) {
    int myID =( (UDPThreadInitInfo *) t)->threadNum;
}

void *printScreen(void *t) {
    int myID = (int) t;
}

void *sendUDPDatagram(void *t) {
    int myID =( (UDPThreadInitInfo *) t)->threadNum;
}

int main(int argc, char *argv[]) {
    int i;  // loop index
    int t1 = 1;
    int t2 = 2;
    int t3 = 1;
    int t4 = 4;
    int rv; //return value


    mySockPacked=malloc(sizeof(UDPThreadInitInfo));

    pthread_attr_t attr;
    pthread_t threads[NUMTHREADS];

    // ensure port #, open socket for UDP_in/out, and define socket_addr for UDP_out
    // not specified: pass in 0 as false value and valid port # will be prompted for user input
    if(argc>2)
        mySock = setupSockInfo(strtoi(argv[1]), strtoi(argv[2]));
    else if(argc>1)
        mySock=setupSockInfo(strtoi(argv[1]),0);
    else
        mySock=setupSockInfo(0,0);

    // terminate program if socket was unable to be set up successfully
    if(mySock->errCode)
    {
        fprintf(stderr,"Failed to create socket or socket info for server! Goodbye : %d\n",mySock->errCode);
        return 1;
    }

    // list initialization
    jobMgmtQueue = ListCreate();

    // semaphores initialization
    if (sem_init(&print_sem, 0, 0) == -1)
        // note: perror() is similar to printf("%s",strerror(errno)): https://stackoverflow.com/12102357
        perror("sem_init for print_sem failed\n");
    if (sem_init(&send_sem, 0, 0) == -1)
        perror("sem_init for send_sem failed\n");

    if (rv = pthread_mutex_init(&list_mutex, NULL) != 0)
        fprintf(stderr,"mutex_init for list_mutex failed: %s\n", strerror(rv));

    // condition initialization
    if (rv = pthread_cond_init(&terminateSIG, NULL) != 0)
        fprintf(stderr,"cond_init for terminateSIG failed: %s\n", strerror(rv));

    // for portability explicitly create threads in a joinable state
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&threads[0], &attr, recordKBInput, (void *) t1);
    pthread_create(&threads[1], &attr, printScreen, (void *) t2);
    // pack mySock and pass to the UDP threads
    mySockPacked->mySock=mySock;
    mySockPacked->threadNum=t3;
    pthread_create(&threads[2], &attr, rcvUDPDatagram, (void *) &mySockPacked);
    mySockPacked->threadNum=t4;
    pthread_create(&threads[3], &attr, sendUDPDatagram, (void *) &mySockPacked);


    // wait for ctrl+c || ctrl+d


//    pthread_exit(NULL);   //main() will block and be kept alive to support the threads it created until they are done
    // use pthread_join() instead, as mutex will be destroyed by main() in the end, and resulting other threads to fail
    for (i = 0; i < NUMTHREADS; i++)
        pthread_join(threads[i], NULL);


    // cleanup
    UDPThreadInitInfo_destroy(mySockPacked);
    if (rv = pthread_mutex_destroy(&list_mutex) != 0)
        fprintf(stderr,"sem_destroy for print_sem failed: %s\n", strerror(rv));
    if (rv = pthread_cond_destroy(&terminateSIG) != 0)
        fprintf(stderr,"cond_destroy for terminateSIG failed: %s\n", strerror(rv));
    if (sem_destroy(&print_sem) == -1)
        perror("sem_destroy for print_sem failed\n");
    if (sem_destroy(&send_sem) == -1)
        perror("sem_destroy for send_sem failed\n");


    return 0;
}