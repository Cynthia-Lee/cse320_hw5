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

void *pbx_client_service(void *arg) {
    // argument is a pointer to the integer file descriptor to be used to communicate with the client
    // Once this file descriptor has been retrieved, the storage it occupied needs to be freed
    // thread must then become detatched, so that it does not have to be explicitly reaped
    // must register the client file descriptor with the PBX module
    // thread should enter a service loop in which it repeatedly receives a message sent by the client
        // parses the message, and carries out the specified command
    // the actual work involved in carrying out the command is performed
        // by calling the functions provided by the PBX module

    // arg = pointer to variable that holds file descriptor for client connection
        // must be freed once retrieved

    // SIGPIPE handler
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal error");
        return NULL;
    }

    int connfd = *((int *)arg);
    Pthread_detach(pthread_self());
    free(arg);

    TU *tu = pbx_register(pbx, connfd);

    FILE *stream;
    char *buffer;
    char *temp;
    int buffer_size = MAXLINE;
    char ch;
    int i = 0;
    int flag = 0;

    buffer = (char *)malloc(buffer_size);

    if ((stream = fdopen(connfd, "r")) != NULL) {
        // service loop
        while ((ch = fgetc(stream)) != EOF) { // ends when EOF is seen
            // checks
            if (ferror(stream)) {
                perror("ferror error");
                break;
            }
            if (feof(stream)) {
                perror("feof error");
                break;
            }

            if (i >= buffer_size) { // check realloc
                temp = realloc(buffer, buffer_size * 2);
                if (!temp) {
                    perror("realloc error");
                    break;
                }
                buffer_size = buffer_size * 2;
                buffer = temp;
            }

            buffer[i] = ch;
            i = i + 1;

            if (ch == '\n' && buffer[i-2] == '\r') {
                buffer[i-2] = '\0';
                i = 0;
                if (flag == 1) { // dial #
                    int ext = atoi(buffer); // need another check
                    tu_dial(tu, ext);
                } else if (flag == 2) { // chat ...
                    tu_chat(tu, buffer);
                } else if ((strcmp(buffer, tu_command_names[TU_PICKUP_CMD])) == 0) { // pickup
                    tu_pickup(tu);
                } else if ((strcmp(buffer, tu_command_names[TU_HANGUP_CMD])) == 0) { // hangup
                    tu_hangup(tu);
                } else if ((strcmp(buffer, tu_command_names[TU_CHAT_CMD])) == 0) { // chat
                    tu_chat(tu, "");
                }
                flag = 0;
            } // EOL

            if (ch == ' ' && flag == 0) { // dial #, chat ...
                buffer[i-1] = '\0';
                if (strcmp(buffer, tu_command_names[TU_DIAL_CMD]) == 0) {
                    flag = 1;
                    i = 0;
                } else if (strcmp(buffer, tu_command_names[TU_CHAT_CMD]) == 0) {
                    flag = 2;
                    i = 0;
                } else {
                    buffer[i-1] = ' ';
                }
            }

        } // EOF service loop
    } else {
        perror("fdopen error");
    }

    debug("----------------server.c ending");
    free(buffer);
    pbx_unregister(pbx, tu);
    Close(connfd);
    return NULL;

    // executes a "service loop" that receives messages from the client and dispatches to
        // appropriate functions to carry out the client's request
    // service loop ends when the network connection shuts down and EOF is seen
    // could occur either as a result of the client explicitly closing the connection,
        // a timeout in the network causing the connection to be closed
        // the main thread of the server shutting down the connection as part of graceful termination
}