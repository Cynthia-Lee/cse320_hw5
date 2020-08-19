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

struct tu
{
    int ext; // extension number
    int fd; // file descriptor
    TU_STATE state; // current state
    TU *peer;
    sem_t mutex;
};

struct pbx
{
    TU *clients[PBX_MAX_EXTENSIONS]; // extension = index of client
    sem_t mutex;
};

// maintain registry of connected clients
// manage the TU objects associated with these clients
// map each extension number to the associated TU object
// maintain for each TU, the file descriptor of the underlying network connection
    // and current state of the TU
// use the file descriptor to issue a response to the client
    // as well as any asynchronous notifications, whenever tu_xxx function is called
// PBX and TU objects will be accessed concurrently by multiple threads

int notification(TU *tu) {
    FILE *stream;
    if ((stream = fdopen(tu_fileno(tu), "w")) != NULL) {
        if (tu->state == TU_ON_HOOK) {
            // ON HOOK #
            if (fprintf(stream, "%s%s%d%s", tu_state_names[tu->state], " ", tu_extension(tu), EOL) < 0) {
                perror("fprintf error");
                return -1;
            }
        } else if (tu->state == TU_CONNECTED) {
            // CONNECTED #
            if (fprintf(stream, "%s%s%d%s", tu_state_names[tu->state], " ", tu_extension(tu->peer), EOL) < 0) {
                perror("fprintf  error");
                return -1;
            }
        } else {
            // STATE
            if (fprintf(stream, "%s%s", tu_state_names[tu->state], EOL) < 0) {
                perror("fprintf error");
                return -1;
            }
        }
    } else {
        perror("fdopen error");
        return -1;
    }
    if (fflush(stream) == EOF) {
        return -1;
    }
    return 0;
}

// initialize a new PBX
PBX *pbx_init() {
    // debug("init");
    PBX *new_pbx;
    if ((new_pbx = malloc(sizeof(PBX))) == NULL) {
        return NULL;
    }
    if (sem_init(&(new_pbx->mutex), 0, 1) < 0) {
        return NULL;
    }
    P(&(new_pbx->mutex));
    debug("init - locked pbx");
    for (int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
        new_pbx->clients[i] = NULL;
    }
    V(&(new_pbx->mutex));
    debug("init - unlocked pbx");
    return new_pbx;
}

// shut down a PBX
void pbx_shutdown(PBX *pbx) {
    debug("shutdown");
    // shutting down all network connections, waiting for all server threads to terminate
    // and freeing all associated resources
    // if there are any registered extensions, the associated network connections are shut down
    // which will cause the server threads to terminate
    // Once all the server threads are terminated, any remaining sources associated with
        // the PBX are freed
    // The PBX object itself is freed and should not be used again

    // shut down the network connections to all registered clients
    // shutdown(2) shut down a socket for reading, writing or both, without closing fd
    for (int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
        // all registered clients
        if (pbx->clients[i] != NULL) {
            P(&(pbx->clients[i]->mutex));
            debug("shutdown - locked tu mutex %d", i);
            if (shutdown(tu_fileno(pbx->clients[i]), SHUT_RDWR) != 0) {
                perror("shutdown error");
                V(&(pbx->clients[i]->mutex));
                return;
            }
            V(&(pbx->clients[i]->mutex));
            debug("shutdown - unlocked tu mutex %d", i);
        }
    }
    // required to wait for all client service threads to unregister the associated TUs
    // loop until all clients are null
    int count = 0;
    while (1) {
        count = 0;
        for (int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
            if (pbx->clients[i] == NULL) {
                count += 1;
            }
        }
        if (count == PBX_MAX_EXTENSIONS) {
            break;
        }
    }

    free(pbx);
    debug("pbx freed");
}

// register a TU client with a PBX
TU *pbx_register(PBX *pbx, int fd) {
    debug("register %d", fd);
    // TU is assigned an extension number and it is initialized to the TU_ON_HOOK state
    // Notification of the assigned extension number is sent to the underlying network client
    P(&(pbx->mutex));
    debug("register - locked pbx");
    // check that there isn't a tu already on that fd
    if (pbx->clients[fd] == NULL) {
        TU *client;
        if ((client = malloc(sizeof(TU))) == NULL) {
            V(&(pbx->mutex));
            return NULL;
        }
        if (sem_init(&(client->mutex), 0, 1) < 0) {
            V(&(pbx->mutex));
            return NULL;
        }
        // debug("register - init tu mutex");

        P(&(client->mutex));
        debug("register - locked tu mutex %d", fd);
        client->ext = fd; // extension
        client->fd = fd;// fd
        client->state = TU_ON_HOOK;// state
        client->peer = NULL;
        pbx->clients[client->ext] = client; // add client to pbx's array

        // notification
        notification(client); // ON HOOK #

        V(&(client->mutex));
        debug("register - unlocked tu mutex %d", fd);

        V(&(pbx->mutex));
        debug("register - unlocked pbx mutex");
        return client;
    }
    V(&(pbx->mutex));
    debug("register failed - unlocked pbx mutex");
    debug("%d", fd);
    return NULL;
}

// unregister a TU from a PBX
int pbx_unregister(PBX *pbx, TU *tu) {
    debug("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    debug("unregister %d", tu->ext);
    // This object is freed as a result of the call and must not be used again
    P(&(pbx->mutex));
    debug("unregister - locked pbx");
    P(&(tu->mutex));
    debug("unregister - locked tu mutex %d", tu->ext);
    if (pbx->clients[tu_extension(tu)] == tu) {
        if (tu->peer != NULL) {
            // two locks
            TU_STATE check_state = tu->state;
            TU_STATE check_peer_state = tu->state;
            TU *check_peer = tu->peer;

            V(&(tu->mutex));
            debug("unregister - release tu mutex %d", tu->ext);
            // simultaneously
            if (tu_extension(tu) < tu_extension(tu->peer)) {
                P(&(tu->mutex)); P(&(tu->peer->mutex));
                debug("unregister - locked tu mutex %d then %d", tu_extension(tu), tu_extension(tu->peer));
                debug("%s%s", "unregister test2 ", tu_state_names[tu->state]);
            } else {
                P(&(tu->peer->mutex)); P(&(tu->mutex));
                debug("unregister - locked tu mutex %d then %d", tu_extension(tu->peer), tu_extension(tu));
                debug("%s%s", "unregister test2 ", tu_state_names[tu->state]);
            }

            // check tu same state, same peer
            if ((check_state != tu->state && check_peer_state != check_peer->state) ||
                (check_peer != tu->peer && check_peer->peer != tu)) {
                // need to try again
                V(&(tu->peer->mutex));
                V(&(tu->mutex));
                V(&(pbx->mutex));
                pbx_unregister(pbx, tu);
            }

            // TU peer
            // if RING BACK or CONNECTED then DIAL TONE
            if ((tu->peer->state == TU_RING_BACK) || (tu->peer->state == TU_CONNECTED)) {
                tu->peer->state = TU_DIAL_TONE;
            } else if (tu->peer->state == TU_RINGING) {
                // if RINGING then ON HOOK
                tu->peer->state = TU_ON_HOOK;
            }
            notification(tu->peer);
            tu->peer->peer = NULL;
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
            debug("tu peer");

            pbx->clients[tu_extension(tu)] = NULL;
            free(tu);
            V(&(pbx->mutex));
            debug("unregister - unlocked pbx");
            return 0;
        } else { // TU had no peer
            V(&(tu->mutex));
            debug("unregister - unlocked tu mutex %d", tu->ext);
            pbx->clients[tu_extension(tu)] = NULL;
            free(tu);
            V(&(pbx->mutex));
            debug("unregister - unlocked pbx");
            return 0;
        }
    }
    // TU not in pbx client array
    free(tu);
    V(&(pbx->mutex));
    debug("unregister - unlocked pbx");
    return -1;
}

// Get the file descriptor for the network connection underlying a TU
int tu_fileno(TU *tu) {
    if (&(tu->fd) == NULL) {
        return -1;
    }
    return (tu->fd);
}

// Get the extension number for a TU
int tu_extension(TU *tu) {
    if (&(tu->ext) == NULL) {
        return -1;
    }
    return (tu->ext);
}

// Take a TU receiver off-hook (i.e. pick up the handset)
int tu_pickup(TU *tu) {
    debug("pickup %d", tu->ext);
    // if the TU was in the TU_ON_HOOK state, then it goes to the TU_DIAL_TONE state
    // if the TU was in the TU_RINGING state, then it goes to the TU_CONNECTED state
    // if the TU was in any other state, then it remains in that state
    P(&(pbx->mutex));
    debug("pickup - locked pbx mutex");
    P(&(tu->mutex));
    debug("pickup - locked tu mutex %d", tu->ext);
    if (tu->state == TU_ON_HOOK) {
        tu->state = TU_DIAL_TONE;
    } else if (tu->state == TU_RINGING) {
        debug("pickup - simultaneously ##########################################");
        debug("%s%s", "pickup test1 ", tu_state_names[tu->state]);

        TU_STATE check_state = tu->state;
        TU_STATE check_peer_state = tu->state;
        TU *check_peer = tu->peer;

        V(&(tu->mutex));
        debug("pickup - unlocked tu mutex %d", tu->ext);
        // simultaneously
        if (tu_extension(tu) < tu_extension(tu->peer)) {
            P(&(tu->mutex)); P(&(tu->peer->mutex));
            debug("pickup - locked tu mutex %d then %d", tu_extension(tu), tu_extension(tu->peer));
            debug("%s%s", "pickup test2 ", tu_state_names[tu->state]);
        } else {
            P(&(tu->peer->mutex)); P(&(tu->mutex));
            debug("pickup - locked tu mutex %d then %d", tu_extension(tu->peer), tu_extension(tu));
            debug("%s%s", "pickup test2 ", tu_state_names[tu->state]);
        }

        // check tu same state, same peer
        if ((check_state != tu->state && check_peer_state != check_peer->state) ||
            (check_peer != tu->peer && check_peer->peer != tu)) {
            // need to try again
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
            V(&(pbx->mutex));
            tu_pickup(tu);
        }

        tu->state = TU_CONNECTED;
        // calling TU also transitiosn to the TU_CONNECTED state
        tu->peer->state = TU_CONNECTED;
        // if the new state is TU_CONNECTED, then the calling TU is also notified of new state
        if (notification(tu->peer) == -1) {
            V(&(tu->mutex));
            V(&(pbx->mutex));
            return -1;
        }
        V(&(tu->peer->mutex));
        debug("pickup - unlocked tu mutex %d", tu_extension(tu->peer));
    }

    // notification of the new state is sent to the network client
    if (notification(tu) == -1) {
        V(&(tu->mutex));
        V(&(pbx->mutex));
        return -1;
    }
    V(&(tu->mutex));
    debug("pickup - unlocked tu mutex %d", tu->ext);
    V(&(pbx->mutex));
    debug("pickup - unlocked pbx mutex");
    // error for I/O, TU_ERROR return 0
    return 0;
}

// Hang up a TU (i.e. replace the handset on the switchook)
int tu_hangup(TU *tu) {
    debug("hangup %d", tu->ext);
    //  if the TU was in the TU_CONNECTED state, then goes to TU_ON_HOOK state
        // simultaneously, peer TU transitions to the TU_DIAL_TONE state
    // if TU was in the TU_RING_BACK state, then goes to the TU_ON_HOOK state
        // calling TU (TU_RINGING state) simultaneously transitions to the TU_ON_HOOK state
    // if the TU was in the TU_RINGING state, then goes to TU_ON_HOOK state
        // called TU (TU_RING_BACK state) simultaneously transitions to the TU_DIAL_TONE state
    // if TU was in the TU_DIAL_TONE, TU_BUSY_SIGNAL, or TU_ERROR state, then goes to the TU_ON_HOOK state
    // if TU was in any other state, then there is no change of state

    // notification of the new state is sent to the network client
    // also for peer
    P(&(pbx->mutex));
    P(&(tu->mutex));
    debug("hangup - locked tu mutex %d", tu_extension(tu));
    if (tu->state == TU_CONNECTED) {
        debug("hangup - simultaneously ##########################################1");
        debug("%s%s", "hangup test1 ", tu_state_names[tu->state]);

        TU_STATE check_state = tu->state;
        TU_STATE check_peer_state = tu->state;
        TU *check_peer = tu->peer;

        V(&(tu->mutex));
        // simultaneously
        if (tu_extension(tu) < tu_extension(tu->peer)) {
            P(&(tu->mutex)); P(&(tu->peer->mutex));
            debug("hangup - locked tu mutex %d then %d", tu_extension(tu), tu_extension(tu->peer));
            debug("%s%s", "hangup test2 ", tu_state_names[tu->state]);
        } else {
            P(&(tu->peer->mutex)); P(&(tu->mutex));
            debug("hangup - locked tu mutex %d then %d", tu_extension(tu->peer), tu_extension(tu));
            debug("%s%s", "hangup test2 ", tu_state_names[tu->state]);
        }

        // check tu same state, same peer
        if ((check_state != tu->state && check_peer_state != check_peer->state) ||
            (check_peer != tu->peer && check_peer->peer != tu)) {
            // need to try again
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
            V(&(pbx->mutex));
            tu_hangup(tu);
        }

        tu->state = TU_ON_HOOK;
        tu->peer->state = TU_DIAL_TONE;
        // peer notified
        if (notification(tu->peer) == -1) {
            V(&(tu->peer->mutex));
            tu->peer = NULL;
            V(&(tu->mutex));
            V(&(pbx->mutex));
            return -1;
        }
        V(&(tu->peer->mutex));
        debug("hangup - unlocked tu mutex %d", tu_extension(tu->peer));

    } else if (tu->state == TU_RING_BACK) {
        debug("hangup - simultaneously ##########################################2");
        debug("%s%s", "hangup test1 ", tu_state_names[tu->state]);

        TU_STATE check_state = tu->state;
        TU_STATE check_peer_state = tu->state;
        TU *check_peer = tu->peer;

        V(&(tu->mutex));
        // simultaneously
        if (tu_extension(tu) < tu_extension(tu->peer)) {
            P(&(tu->mutex)); P(&(tu->peer->mutex));
            debug("hangup - locked tu mutex %d then %d", tu_extension(tu), tu_extension(tu->peer));
            debug("%s%s", "hangup test2 ", tu_state_names[tu->state]);
        } else {
            P(&(tu->peer->mutex)); P(&(tu->mutex));
            debug("hangup - locked tu mutex %d then %d", tu_extension(tu->peer), tu_extension(tu));
            debug("%s%s", "hangup test2 ", tu_state_names[tu->state]);
        }

        // check tu same state, same peer
        if ((check_state != tu->state && check_peer_state != check_peer->state) ||
            (check_peer != tu->peer && check_peer->peer != tu)) {
            // need to try again
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
            V(&(pbx->mutex));
            tu_hangup(tu);
        }

        tu->state = TU_ON_HOOK;
        tu->peer->state = TU_ON_HOOK; // was in TU_RINGING

        // peer notified
        if (notification(tu->peer) == -1) {
            V(&(tu->peer->mutex));
            tu->peer = NULL;
            V(&(tu->mutex));
            V(&(pbx->mutex));
            return -1;
        }
        V(&(tu->peer->mutex));
        debug("hangup - unlocked tu mutex %d", tu_extension(tu->peer));

    } else if (tu->state == TU_RINGING) {
        debug("hangup - simultaneously ##########################################3");
        debug("%s%s", "hangup test1 ", tu_state_names[tu->state]);

        TU_STATE check_state = tu->state;
        TU_STATE check_peer_state = tu->state;
        TU *check_peer = tu->peer;

        V(&(tu->mutex));
        // simultaneously
        if (tu_extension(tu) < tu_extension(tu->peer)) {
            P(&(tu->mutex)); P(&(tu->peer->mutex));
            debug("hangup - locked tu mutex %d then %d", tu_extension(tu), tu_extension(tu->peer));
            debug("%s%s", "hangup test2 ", tu_state_names[tu->state]);
        } else {
            P(&(tu->peer->mutex)); P(&(tu->mutex));
            debug("hangup - locked tu mutex %d then %d", tu_extension(tu->peer), tu_extension(tu));
            debug("%s%s", "hangup test2 ", tu_state_names[tu->state]);
        }

        // check tu same state, same peer
        if ((check_state != tu->state && check_peer_state != check_peer->state) ||
            (check_peer != tu->peer && check_peer->peer != tu)) {
            // need to try again
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
            V(&(pbx->mutex));
            tu_hangup(tu);
        }

        tu->state = TU_ON_HOOK;
        tu->peer->state = TU_DIAL_TONE; // was in TU_RING_BACK

        // peer notified
        if (notification(tu->peer) == -1) {
            V(&(tu->peer->mutex));
            tu->peer = NULL;
            V(&(tu->mutex));
            V(&(pbx->mutex));
            return -1;
        }
        V(&(tu->peer->mutex));
        debug("hangup - unlocked tu mutex %d", tu_extension(tu->peer));

    } else if ((tu->state == TU_DIAL_TONE) || (tu->state == TU_BUSY_SIGNAL) || (tu->state == TU_ERROR)) {
        tu->state = TU_ON_HOOK;
    } // else no change of state

    tu->peer = NULL;
    // notification of the new state
    if (notification(tu) == -1) {
        V(&(tu->mutex));
        V(&(pbx->mutex));
        return -1;
    }
    V(&(tu->mutex));
    debug("hangup - unlocked tu mutex %d", tu_extension(tu));
    V(&(pbx->mutex));
    return 0;
}

// Dial an extension on a TU
int tu_dial(TU *tu, int ext) {
    debug("dial %d calling %d", tu->ext, ext);
    // if the specified extension number does not refer to any registered extension, then
        // TU transitions to the TU_ERROR state

    // otherwise, if TU was in the TU_DIAL_TONE state
        // if dialed extension was in the TU_ON_HOOK state,
            // then calling TU transitions to the TU_RING_BACK state
            // and the dialed TU simultaneously transitions to the TU_RINGING state
        // if the dialed extension was not in the TU_ON_HOOK state,
            // then the calling TU transitions to the TU_BUSY_SIGNAL state
            // and there is no change to the state of the dialed extension
    // if the TU was in any state other than TU_DIAL_TONE, then there is no state change

    // in all cases, a notification of the new state is sent to the network client underlying this TU
    // if the new state is TU_RING_BACK, then the called extension is also notified of its
        // new state (TU_RINGING)
    P(&(pbx->mutex));
    P(&(tu->mutex));
    debug("dial - locked tu mutex %d", tu_extension(tu));
    if (tu->state != TU_DIAL_TONE) {
        if (notification(tu) == -1) {
            V(&(tu->mutex));
            V(&(pbx->mutex));
            return -1;
        }
        V(&(tu->mutex));
        debug("dial - unlocked tu mutex %d", tu_extension(tu));
        V(&(pbx->mutex));
        return 0;
    }

    for (int t = 0; t < PBX_MAX_EXTENSIONS; t++) {
        if (pbx->clients[t] != NULL && pbx->clients[t]->ext == ext) {
            if (tu->state == TU_DIAL_TONE) {
                // other TU is on TU_ON_HOOK state
                if (pbx->clients[ext]->state == TU_ON_HOOK) {
                    debug("dial - simultaneously ##########################################");
                    debug("%s%s", "dial test1 ", tu_state_names[tu->state]);

                    TU_STATE check_state = tu->state;
                    TU_STATE check_peer_state = tu->state;
                    TU *check_peer = tu->peer;

                    V(&(tu->mutex));
                    debug("dial - released tu mutex %d", tu_extension(tu));
                    // simultaneously
                    if (tu_extension(tu) < tu_extension(pbx->clients[ext])) {
                        P(&(tu->mutex)); P(&(pbx->clients[ext]->mutex));
                        debug("dial - locked tu mutex %d then %d", tu_extension(tu), tu_extension(pbx->clients[ext]));
                        debug("%s%s", "dial test2 ", tu_state_names[tu->state]);
                    } else {
                        P(&(pbx->clients[ext]->mutex)); P(&(tu->mutex));
                        debug("dial - locked tu mutex %d then %d", tu_extension(pbx->clients[ext]), tu_extension(tu));
                        debug("%s%s", "dial test2 ", tu_state_names[tu->state]);
                    }

                    // check tu same state, same peer
                    if ((check_state != tu->state && check_peer_state != check_peer->state) ||
                        (check_peer != tu->peer && check_peer->peer != tu)) {
                        // need to try again
                        V(&(tu->peer->mutex));
                        V(&(tu->mutex));
                        V(&(pbx->mutex));
                        tu_dial(tu, ext);
                    }

                    tu->peer = pbx->clients[ext];
                    tu->peer->peer = tu;

                    tu->state = TU_RING_BACK;
                    tu->peer->state = TU_RINGING;

                    // peer notified
                    if (notification(tu->peer) == -1) {
                        V(&(pbx->clients[ext]->mutex));
                        V(&(pbx->mutex));
                        return -1;
                    }
                    V(&(pbx->clients[ext]->mutex));
                    debug("dial - unlocked tu mutex %d", tu_extension(pbx->clients[ext]));

                } else { // other TU is not on the TU_ON_HOOK state
                    tu->state = TU_BUSY_SIGNAL;
                }
            } //  else no state change

            // notification of the new state is sent to network client
            if (notification(tu) == -1) {
                V(&(tu->mutex));
                V(&(pbx->mutex));
                return -1;
            }
            V(&(tu->mutex));
            debug("dial - unlocked tu mutex %d", tu_extension(tu));
            V(&(pbx->mutex));
            return 0;
        }
    }
    // extension does not refer to any registered extension
    tu->state = TU_ERROR;
    if (notification(tu) == -1) {
        V(&(tu->mutex));
        V(&(pbx->mutex));
        return -1;
    }
    V(&(tu->mutex));
    debug("dial - unlocked tu mutex %d", tu_extension(tu));
    V(&(pbx->mutex));
    return 0;
}

// "Chat" over a connection
int tu_chat(TU *tu, char *msg) {
    debug("chat %d", tu->ext);
    // if the state of the TU is not TU_CONNECTED, then nothing is sent and -1 is returned
    // otherwise, the specified message is sent via the network connection to the peer TU
    // in all cases, the states of the TUs are left unchanged and a notification
        // containing the current state is sent to the TU sending the chat
    P(&(pbx->mutex));
    P(&(tu->mutex));
    debug("chat - locked tu mutex %d", tu_extension(tu));
    if (tu->state == TU_CONNECTED) {
        debug("%s%s", "chat test1 ", tu_state_names[tu->state]);

        TU_STATE check_state = tu->state;
        TU_STATE check_peer_state = tu->state;
        TU *check_peer = tu->peer;

        V(&(tu->mutex));
        // simultaneously
        if (tu_extension(tu) < tu_extension(tu->peer)) {
            P(&(tu->mutex)); P(&(tu->peer->mutex));
            debug("hangup - locked tu mutex %d then %d", tu_extension(tu), tu_extension(tu->peer));
            debug("%s%s", "chat test2 ", tu_state_names[tu->state]);
        } else {
            P(&(tu->peer->mutex)); P(&(tu->mutex));
            debug("hangup - locked tu mutex %d then %d", tu_extension(tu->peer), tu_extension(tu));
            debug("%s%s", "chat test2 ", tu_state_names[tu->state]);
        }

        // check tu same state, same peer
        if ((check_state != tu->state && check_peer_state != check_peer->state) ||
            (check_peer != tu->peer && check_peer->peer != tu)) {
            // need to try again
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
            V(&(pbx->mutex));
            tu_chat(tu, msg);
        }

        // message sent to peer TU
        // CHAT <msg>
        FILE *stream;
        if ((stream = fdopen(tu_fileno(tu->peer), "w")) != NULL) {
            if (fprintf(stream, "%s%s%s", "CHAT ", msg, EOL) < 0) {
                perror("fprintf error");
                return -1;
            }
            fflush(stream);
        } else {
            perror("fdopen error");
            V(&(tu->peer->mutex));
            V(&(pbx->mutex));
            return -1;
        }
        V(&(tu->peer->mutex));
        debug("chat - unlocked tu peer mutex");
    } else {
        // nothing is sent and -1 is returned
        notification(tu);
        V(&(tu->mutex));
        debug("chat - unlocked tu mutex %d", tu_extension(tu));
        V(&(pbx->mutex));
        return -1;
    }

    if (notification(tu) == -1) {
        V(&(tu->mutex));
        V(&(pbx->mutex));
        return -1;
    }
    V(&(tu->mutex));
    debug("chat - unlocked tu mutex %d", tu_extension(tu));
    V(&(pbx->mutex));
    return 0;
}