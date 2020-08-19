#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "pbx.h"
#include "server.h"
#include "debug.h"

#include "csapp.h"

volatile sig_atomic_t term = 0;

static void terminate(int status);

void sighup_handler(int sig) {
    // clean termination of the server
    term = 1;
    // terminate(EXIT_SUCCESS);
}

/*
 * "PBX" telephone exchange simulation.
 *
 * Usage: pbx <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    debug("%d", getpid());
    int option;
    char  *port;
    while((option = getopt(argc, argv, "p:")) != EOF) {
        switch(option) {
            case 'p':
                if ((atoi(optarg)) < 1024) {
                    fprintf(stderr, "Permission deined. Port number must be 1024 or above.\n");
                    exit(EXIT_FAILURE);
                }
                port = argv[2];
                break;
            default:
                fprintf(stderr, "Usage: %s -p <port>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (argc != 3) {
        fprintf(stderr, "Usage: %s -p <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    // Signal(SIGHUP, sighup_handler);
    struct sigaction sa;
    sa.sa_handler = sighup_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, NULL);

    // Perform required initialization of the PBX module.
    debug("Initializing PBX...");
    pbx = pbx_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function pbx_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.

    listenfd = Open_listenfd(port);
    while (term == 0) {
        clientlen = sizeof(struct sockaddr_storage);
        if ((connfdp = malloc(sizeof(int))) == NULL) {
            perror("malloc error");
            Close(listenfd);
            terminate(EXIT_FAILURE);
        }
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if(term == 1) {
            break;
        }
        Pthread_create(&tid, NULL, pbx_client_service, connfdp);
    }

    /*
    fprintf(stderr, "You have to finish implementing main() "
	    "before the PBX server will function.\n");
    */

    Close(listenfd);
    terminate(EXIT_SUCCESS);
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    debug("Shutting down PBX...");
    pbx_shutdown(pbx);
    debug("PBX server terminating");
    exit(status);
}
