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
// addrinfo & AI_PASSIVE was not able to be recognized despite '#include <netdb.h>' when compiling with '-std=CXX' flag
// Adding this is one liner was the solution as explained here: https://stackoverflow.com/a/37545256
#define _POSIX_C_SOURCE 200112L

//// DEBUG macro is used to turn on various debugging features
//// Disable at the release version
//#define DEBUG

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
#include <sys/types.h>
#include <ctype.h>      // isspace()


#include "LIST.h"

#define NUMTHREADS 4
#define MAXMSGLENGTH 256

// job packages data struct
// contains the type of job, and data (send/received msg)
// used in list->node->data
typedef enum {
    WROTE = 0,     // printing job created by recordKBInput(), print "He/She:" header in msg
    RCVED,     // printing job created by rcvUDPDatagram(), print "Me:" header in msg
    DEFAULT    // filler jobType used for sending jobs
} jobType;
typedef struct jobPckg {
    jobType type;
    char msg[MAXMSGLENGTH];
} jobPckg;

// used for opening socket for UDP_in/out, and defining socketaddr_in for UDP_out
// used for sendUDP & rcvUDP upon thread startup
typedef struct sockInfo {
    int sockFD;     // local socket #. -1: invalid
    struct addrinfo *destAddrInfo;
//    int errCode;    // 0: no problem; 1: Failed to create and bind receiving socket; 2: Failed to create destination addrinfo struct; 3: unknown
} sockInfo;


// global variables
// ptrs to the queue data structure that will be used to manage traffic
static list *printJobMgmtQueue;
static list *sendJobMgmtQueue;

// socket file descriptor and info obj. To be passed to the two UDP-related threads
static sockInfo *sockInfoPassToThread;

// these two semaphores are used to notify the threads printScreen() & endUDPDatagram() to wake up
static sem_t print_sem;
static sem_t send_sem;

// mutex for list access
static pthread_mutex_t printList_mutex;
static pthread_mutex_t sendList_mutex;

// condition to signal program exit
// 1: signal sent; 0: signal not discovered
static int terminateSIG_bool;

// --------------- Helper funcs ---------------------------------
// Designed for ONLY POSITIVE int
// best way to convert char[] to int: https://stackoverflow.com/a/22866001
// return int upon success. Return negative int upon failure
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

// return 0 upon success; return 1 upon failure
int convertCharPortsToInt(char *MYPORTchar, char *REMOTEPORTchar, int *MYPORT, int *REMOTEPORT) {
    // empty addr-> return
    if (!MYPORT || !REMOTEPORT)
        return 1;
    int rv;
    int badParam_bool = 0;
    if ((rv = strtoi(MYPORTchar)) < 0) {
        fprintf(stderr, "Failed to convert MYPORT to integer: %d\n", rv);
        badParam_bool = 1;
    } else
        *MYPORT = rv;
    if ((rv = strtoi(REMOTEPORTchar)) < 0) {
        fprintf(stderr, "Failed to convert REMOTEPORT to integer: %d\n", rv);
        badParam_bool = 1;
    } else
        *REMOTEPORT = rv;

    if (!badParam_bool) {
        if (*MYPORT < 1025 || *MYPORT > 65535 || *REMOTEPORT < 1025 || *REMOTEPORT > 65535) {
            badParam_bool = 1;
        }
    }
    return badParam_bool;
}

// check if stdin is empty
// return 1 if not empty, return 0 if empty
int stdinIsNotEmpty() {
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
    memset(arr, 0, sizeof(p));

    // fgets return NULL on failure
    if (fgets(arr, size, stdin)) {
        return 0;
    } else
        return -1;
}

// Used in displaying recvfrom() error:
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
// if remoteAddr is not specified (passed in NULL), the func assumes addr=localHost
// note: the user of this func will be responsible for freeing the linked list created
struct addrinfo *getaddrrinfoList(int portNum, char *remoteAddr) {
    struct addrinfo hints;
    struct addrinfo *servinfo;
    int rv;
    char portNumChar[6];
    sprintf(portNumChar, "%d", portNum);    // since 65535 is max
    // This is the modern way of packing a struct: automatically with getaddrinfo()
    // addrinfo hint is used to specify parameters to setup socket address structure for subsequent use
    memset(&hints, 0, sizeof hints);
    if (!remoteAddr)
        hints.ai_flags = AI_PASSIVE; // use my IP
    hints.ai_family = AF_INET; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;

    //  getaddrinfo() is reentrant and allows programs to eliminate IPv4-versus-IPv6 dependencies.
    if ((rv = getaddrinfo(remoteAddr, portNumChar, &hints, &servinfo)) != 0) {
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
    // second argument as NULL to specify address=local host
    servinfo = getaddrrinfoList(portNum, NULL);
    if (!servinfo) {
        fprintf(stderr, "getaddrinfo failed in setupHostUDPSocket(%d)\n", portNum);
        return -1;
    }
    // loop through all the Internet address results, make a socket, and bind to the first we can (should only be one?)
    for (p = servinfo; p != NULL; p = p->ai_next) {
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
struct addrinfo *setupDestAddrInfo(int remotePortNum, char *remoteAddr) {
    struct addrinfo *p1, *p2;

    p1 = getaddrrinfoList(remotePortNum, remoteAddr);
    if (!p1)
        return NULL;
    else {
        // we only need to keep one valid addrinfo block
        p2 = p1->ai_next;
        while (p2) {
            free(p1);
            p1 = p2;
            p2 = p2->ai_next;
        }
        return p1;
    }
}

// destroy socket & the socket_addr object in a sockInfo obj
void sockInfo_destroy(sockInfo *mySockInfo) {
    freeaddrinfo(mySockInfo->destAddrInfo);
    close(mySockInfo->sockFD);
}

// request for packages to be put onto the list
// Note: This function assumes the caller already holds the key to the list
void jobEnqueue(jobType type, char *str, list *aList) {
    jobPckg *newJob = malloc(sizeof(jobPckg));
    strcpy(newJob->msg, str);
    newJob->type = type;
    ListPrepend(aList, newJob);
}

// request for the next in queue package (last in list) to be dequeued, if the jobPckg matches the select data type
// return the pointer to the package on match
// else return NULL
// Note: This function assumes the caller already holds the key to the list
jobPckg *jobDequeue(list *aList) {
    // check the last package
    return ListTrim(aList);
}

// for use in ListFree()
void jobPckgFree(jobPckg *aPckg) {
    free(aPckg);
}

// if string is all space, return 1
// else return 0
int isAllSpace(char *head, int strLen) {
    char *ptr;
    int allSpace_bool = 1;
    ptr = head;
    int i = 0;
    while (*ptr != '\0' && i < strLen) {
        if (!isspace(*ptr))
            allSpace_bool = 0;
        i++;
        ptr++;
    }
    return allSpace_bool;
}

// -------------------------------- threads ------------------------------------------------------------
// it is essential to have threads return void*: https://stackoverflow.com/a/10457390
void *recordKBInput(void *t) {
    char msg[MAXMSGLENGTH+1];
    int numbytes;

    while (!terminateSIG_bool) {
        if (stdinIsNotEmpty()) {
            if (!getstdinStr(msg, MAXMSGLENGTH)) {
                numbytes = (int) strlen(msg);
                if (numbytes) {
                    // single "!" (plus whatever whitespace) char is sent -> terminate
                    if (msg[0] == 33 &&
                        isAllSpace(msg + 1, MAXMSGLENGTH - 1)) {    //ASCII "!"=33, designated termination signal
                        terminateSIG_bool = 1;
                    } else {
                        // create send & print jobPckg
                        pthread_mutex_lock(&printList_mutex);
                        jobEnqueue(WROTE, msg, printJobMgmtQueue);
                        pthread_mutex_unlock(&printList_mutex);
                    }
                    // even if msg=="!", we would like to send the termination signal to the remote host anyways
                    pthread_mutex_lock(&sendList_mutex);
                    jobEnqueue(DEFAULT, msg, sendJobMgmtQueue);
                    pthread_mutex_unlock(&sendList_mutex);

                    // wake up threads printScreen & sendUDPDatagram if they are currently blocked (print_sem == -1 || send_sem==-1)
                    sem_post(&print_sem);
                    sem_post(&send_sem);
                }
            }
        }
    }
    pthread_exit(NULL);
}

// thread handles scenario of when the remote host sends "!" to indicate terminate
/*
 * rcvUDPDatagram() is special because it is impossible to wake thread from rcvfrom() and terminate,
 * in the case of when terminateSIG is sent from recordKBInput() thread.
 * As a result, we will call pthread_cancel(thread[3]) after all other threads are terminated.
 * A better approach might be having recordKBInput() handle the cancelling,
 * but that would require passing in the threadID of rcvUDPDatagram() to recordKBInput(), increasing code complexity.
 */
void *rcvUDPDatagram(void *t) {
    // setup cancellation
    int rv;
    if((rv=pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL))!=0)
        fprintf(stderr,"Error in pthread_setcancelstate() for rcvUDPDatagram(): %s\n",strerror(rv));
    if((rv=pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL))!=0)
        fprintf(stderr,"Error in pthread_setcanceltype() for rcvUDPDatagram(): %s\n",strerror(rv));

    struct sockaddr_storage their_addr; // even tho in this assignment we defined the communication with IPv4,
    // using sockaddr_storage instead of sockaddr_in allocates larger size and is generally a safer approach
    socklen_t addr_len;
    char msg[MAXMSGLENGTH];
    char addrStr[INET6_ADDRSTRLEN];
    // unpack the socket info
    sockInfo *mySockInfo = (sockInfo *) t;
    int sockFD = mySockInfo->sockFD;
    int numbytes;

#ifdef DEBUG
    printf("Now listening for packets.\n");
#endif
    // RECEIVING msg -------------------------
    while (!terminateSIG_bool) {
        addr_len = sizeof(their_addr);   // addr_len will get overwritten with recvfrom as the actual sender IP length,
        // reset this to sockaddr_storage to accommodate both IPv4 and IPv6
        //reset msg
        memset(msg, 0, sizeof msg);
        // Incoming data is buffered at the socket until read
        // https://stackoverflow.com/a/7843683
        if ((numbytes = recvfrom(sockFD, msg, MAXMSGLENGTH - 1, 0, (struct sockaddr *) &their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            fprintf(stderr, "Address length is now modified to %d\n", (int) addr_len);
        } else {
            // append str terminating char to received msg
            msg[numbytes] = '\0';
#ifdef DEBUG
            printf("Got packet from %s\n",
                   inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *) &their_addr), addrStr,
                             sizeof addrStr));
            printf("Packet is %d bytes long\n", numbytes);
            printf("Packet contains \"%s\"\n", msg);
#endif
            // single "!" char is received -> terminate
            if (msg[0] == 33 &&isAllSpace(msg+1,MAXMSGLENGTH-1)) {    //ASCII '!'=33, designated termination signal
                terminateSIG_bool = 1;
            } else {
                // append extracted msg to list to wait for printing:
                pthread_mutex_lock(&printList_mutex);
                jobEnqueue(RCVED, msg, printJobMgmtQueue);
                pthread_mutex_unlock(&printList_mutex);
            }
            // wake up thread printScreen if it is currently blocked (print_sem == -1)
            sem_post(&print_sem);
        }
    }
    // also wake up sendUDPDatagram thread, in case its currently blocked
    sem_post(&send_sem);
    pthread_exit(NULL);
}

void *printScreen(void *t) {
    jobPckg *printJob;

    // once termination signal is received (either thru keyboard or sent from remote, we can abandom all print jobs and terminate)
    while (!terminateSIG_bool) {
        // wait for signal that there is a printing job in queue
        sem_wait(&print_sem);

        pthread_mutex_lock(&printList_mutex);
        printJob = jobDequeue(printJobMgmtQueue);
        pthread_mutex_unlock(&printList_mutex);
        // upon receiving "!", recordKBInput() thread only unblocks printScreen() thread, no jobPckg was added
        if (printJob != NULL) {
            if(isAllSpace(printJob->msg,MAXMSGLENGTH)){}
                // ignore job
            else if (printJob->type == RCVED)
                printf("He/She: %s", printJob->msg);
            else
                printf("Me: %s", printJob->msg);
            // destroy the package after we are done with it
            free(printJob);
        }
    }
    printf("\n\nThanks for using this s-talk app designed by Shawn. Have a nice day!\n");
    pthread_exit(NULL);
}

void *sendUDPDatagram(void *t) {
    jobPckg *sendJob;
    // unpack the socket info
    int sockFD = ((sockInfo *) t)->sockFD;
    struct addrinfo *destAddrInfo = ((sockInfo *) t)->destAddrInfo;
    int numbytes;

#ifdef DEBUG
    printf("Now ready to send packets.\n");
#endif

    // send thread is the only thread that cannot terminate upon JUST receiving terminating signal
    // It carries the responsibility of sending this signal to the remote before shutting down
    // thus we set it to break only when the sending queue is empty. I.e., all sending jobs completed
    while (!terminateSIG_bool ) {
        // wait for signal that there is a printing job in queue
        sem_wait(&send_sem);

        pthread_mutex_lock(&sendList_mutex);
        sendJob = jobDequeue(sendJobMgmtQueue);
        pthread_mutex_unlock(&sendList_mutex);

        if (sendJob != NULL && !isAllSpace(sendJob->msg,MAXMSGLENGTH)) {
            if ((numbytes = sendto(sockFD, sendJob->msg, strlen(sendJob->msg), 0,
                                   destAddrInfo->ai_addr, destAddrInfo->ai_addrlen)) == -1) {
                fprintf(stderr,
                        "Error: sendUDPDatagram(). msg attempting to be sent was %s\nmsg was %d bytes long.\n %d bytes were sent successfully: \n%s\n",
                        sendJob->msg, (int) strlen(sendJob->msg), numbytes, strerror(errno));
            }
            // destroy the package after we are done with it
            free(sendJob);
        }
    }
    pthread_exit(NULL);
}

// argv[1]: MY_PORT; argv[2]: REMOTE_ADDR/HOSTNAME; argv[3]: REMOTE_PORT;
int main(int argc, char *argv[]) {
    int i;  // loop index
    int rv; //return value for functions

    // initialize with no terminating signal sent
    terminateSIG_bool = 0;

    // ensure port #, open socket for UDP_in/out, and define socket_addr for UDP_out
    sockInfoPassToThread = malloc(sizeof(sockInfo));
    char remoteAddr[INET6_ADDRSTRLEN];    // set it as the max length possible for portability
    int MYPORT;      // the port THIS instance will be using
    int REMOTEPORT;     // the port the REMOTE instance will be using. I.e., port we will attempt to send TO
    char MYPORTchar[5];      // the port THIS instance will be using
    char REMOTEPORTchar[5];     // the port the REMOTE instance will be using. I.e., port we will attempt to send TO
    int sockFD;

    pthread_attr_t attr;
    pthread_t threads[NUMTHREADS];


    // |-------------------------------------------------------------------------|
    // |                        Ports parameter check                            |
    // |-------------------------------------------------------------------------|
    int badPort_bool = 0;
    if (argc > 2) {
        // defined as return 0 upon success
        badPort_bool = convertCharPortsToInt(argv[1], argv[3], &MYPORT, &REMOTEPORT);
        strcpy(remoteAddr,argv[2]);
    }

    // ensure port #, open socket for UDP_in/out, and define socket_addr for UDP_out
    // not specified: pass in 0 as false value and valid port # will be prompted for user input
    if (argc < 3 || badPort_bool) {
        i = 5;
        badPort_bool=1;
        while (i-- > 0 && badPort_bool) {
            printf("Please re-enter the program parameters as such: [my port number] [remote machine name/address] [remote port number]\nValid port number range: 1025-65535\n");
            // clear char arrs
            memset(MYPORTchar, 0, sizeof MYPORTchar);
            memset(REMOTEPORTchar, 0, sizeof REMOTEPORTchar);
            memset(remoteAddr, 0, sizeof remoteAddr);
            // returns the # of variables assigned
            if (scanf("%s%s%s", MYPORTchar, remoteAddr, REMOTEPORTchar) == 3)
                badPort_bool = convertCharPortsToInt(MYPORTchar, REMOTEPORTchar, &MYPORT, &REMOTEPORT);
        }
    }
    // port #s still bad: terminate
    if (badPort_bool) {
        fprintf(stderr,
                "Parameters invalid.\nPlease re-enter the program parameters as such: [my port number] [remote machine name/address] [remote port number]\nValid port number range: 1025-65535\nGoodbye.");
        return 1;
    }

    // |-------------------------------------------------------------------------|
    // |                   Spawn socket for receiving UPD msgs                   |
    // |-------------------------------------------------------------------------|
    // Note: the following code was previously a separate func, but a bug of encounter SIGABRT upon returning reObj was encountered.
    //      This was suspected to be due to underlying sockaddr & sockaddr_in conversion failure (bcuz of different size?)
    //      This is a workaround by sacrificing encapsulation and putting it in main to avoid passing sockinfo around
#ifdef DEBUG
    printf("Setting up Local Socket...\n");
#endif
    if ((sockFD = setupHostUDPSocket(MYPORT)) == -1) {
        fprintf(stderr, "Failed to create and bind receiving socket. Maybe try a different Port # ?");
        return 2;
    }// success!
    else {
        sockInfoPassToThread->sockFD = sockFD;
#ifdef DEBUG
        printf("Success!\n");
#endif
    }

    // |-------------------------------------------------------------------------|
    // |             get remote socket addrinfo for outgoing UDP msgs            |
    // |-------------------------------------------------------------------------|
#ifdef DEBUG
    printf("Setting up Destination (Remote) Socket info...\n");
#endif
    if ((sockInfoPassToThread->destAddrInfo = setupDestAddrInfo(REMOTEPORT, remoteAddr)) == NULL) {
        fprintf(stderr, "Failed to create destination addrinfo struct. Try a different Port # next time\nBye!\n");
        return 3;
    }// else: success!
#ifdef DEBUG
    printf("Success!\n");
#endif

    // list initialization
    printJobMgmtQueue = ListCreate();
    sendJobMgmtQueue = ListCreate();

    // semaphores initialization
    if (sem_init(&print_sem, 0, 0) == -1)
        // note: perror() is similar to printf("%s",strerror(errno)): https://stackoverflow.com/12102357
        perror("sem_init for print_sem failed\n");
    if (sem_init(&send_sem, 0, 0) == -1)
        perror("sem_init for send_sem failed\n");
    if ((rv = pthread_mutex_init(&printList_mutex, NULL)) != 0)
        fprintf(stderr, "mutex_init for printList_mutex failed: %s\n", strerror(rv));
    if ((rv = pthread_mutex_init(&sendList_mutex, NULL)) != 0)
        fprintf(stderr, "mutex_init for sendList_mutex failed: %s\n", strerror(rv));

    // for portability explicitly create threads in a joinable state
    if(pthread_attr_init(&attr)!=0)
        perror("Error in pthread_attr_init()");
    if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE)!=0)
        perror("Error in pthread_attr_setdetachstate()");
    pthread_create(&threads[0], &attr, recordKBInput, NULL);
    pthread_create(&threads[1], &attr, printScreen, NULL);
    pthread_create(&threads[2], &attr, sendUDPDatagram, (void *) sockInfoPassToThread);

    // we treat rcvUDPDatagram() specially. See thread header for details
    pthread_create(&threads[3], NULL, rcvUDPDatagram, (void *) sockInfoPassToThread);
//    pthread_exit(NULL);   //main() will block upon finishing and be kept alive to support the threads it created until they are done
    // use pthread_join() instead, as we need to cleanup once the other threads finishes
    for (i = 0; i < NUMTHREADS-1; i++)
        pthread_join(threads[i], NULL);

    // handle the terminating of rcvUDPDatagram() thread
    if((rv=pthread_cancel(threads[3]))!=0)
        fprintf(stderr,"pthread_cancel(rcvUDPDatagram) returned error: %s\n",strerror(rv));

    // cleanup
    sockInfo_destroy(sockInfoPassToThread);
    ListFree(printJobMgmtQueue, jobPckgFree);
    ListFree(sendJobMgmtQueue, jobPckgFree);

    if ((rv = pthread_mutex_destroy(&printList_mutex)) != 0)
        fprintf(stderr, "pthread_mutex_destroy for printList_mutex failed: %s\n", strerror(rv));
    if ((rv = pthread_mutex_destroy(&sendList_mutex)) != 0)
        fprintf(stderr, "pthread_mutex_destroy for sendList_mutex failed: %s\n", strerror(rv));
    if (sem_destroy(&print_sem) == -1)
        perror("sem_destroy for print_sem failed\n");
    if (sem_destroy(&send_sem) == -1)
        perror("sem_destroy for send_sem failed\n");

    return 0;
}