#include "segel.h"
#include "request.h"

#define ERROR -1
// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// HW3: Parse the new arguments too
void getargs(int *port, int *threads, int *queue_size, char **schedalg,  int argc, char *argv[])
{
    if (argc != 5) {
	fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <schedalg>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    *schedalg = (char*)malloc(sizeof(char)*strlen(argv[4]));
    strcpy(*schedalg, argv[4]);
}

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_master = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_thread = PTHREAD_COND_INITIALIZER;
int req_handled = 0;
int count = 0;

void* handleThread(void* h_t_params){
    HTParams handle_thread_params = (HTParams)h_t_params;
    Queue q = handle_thread_params->q;
    TData t_data = createTdata(handle_thread_params->t_id);
    while(1){
        pthread_mutex_lock(&m);
        while(q->size == 0){
            pthread_cond_wait(&c_thread, &m);
        }
        int sd = q->head->sd;
        updateDispatch(q->head);
        Node req_node = createNode(sd, &(q->head->arrival));
        req_node->dispatch = q->head->dispatch;
        removeFromHead((void*)q);
        //pthread_cond_broadcast(&c_master);
        //pthread_cond_broadcast(&c_thread);
        req_handled++;
        pthread_mutex_unlock(&m);
        requestHandle(t_data, req_node);
        pthread_mutex_lock(&m);
        req_handled--;
        pthread_cond_broadcast(&c_master);
        //pthread_cond_broadcast(&c_thread);
        pthread_mutex_unlock(&m);
        Close(sd);
        free(req_node);
    }
}


int main(int argc, char *argv[]) {
    int listenfd, connfd, port, clientlen, max_threads, req_q_size;
    struct sockaddr_in clientaddr;
    char *schedalg = "";

    getargs(&port, &max_threads, &req_q_size, &schedalg, argc, argv);

    listenfd = Open_listenfd(port);

    OVERLOAD_HANDLING olh;
    if (strcmp("block", schedalg) == 0) {
        olh = BLOCK;
    } else if (strcmp("dt", schedalg) == 0) {
        olh = DROP_TAIL;
    } else if (strcmp("dh", schedalg) == 0) {
        olh = DROP_HEAD;
    } else if (strcmp("random", schedalg) == 0) {
        olh = DROP_RANDOM;
    }
    else
    {
        exit(1);
    }

    Queue req_q = createQ(req_q_size);

    // 
    // HW3: Create some threads...
    //
    pthread_t *threads = (pthread_t *) malloc(sizeof(*threads) * max_threads);
    if (!threads) {
        exit(1);
    }
    for (int i = 0; i < max_threads; i++) {
        HTParams h_t_params = createHTParams(req_q, i);
        if (pthread_create(threads + i, NULL, handleThread, (void *) h_t_params) == ERROR)
            exit(1);
    }


    struct timeval arrival;
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t * ) & clientlen);
        updateTimeArrival(&arrival);

        pthread_mutex_lock(&m);
        QParams p = createQParams(req_q, connfd, arrival);

        if (p == NULL || p->q == NULL) {
            exit(1);
        }

        if (olh == BLOCK) {
            while (req_handled + p->q->size == p->q->max_size) {
                pthread_cond_wait(&c_master, &m);
            }
        } else if (olh == DROP_TAIL) {
            if (req_handled + p->q->size == p->q->max_size) {
                Close(connfd);
                // pthread_cond_broadcast(&c_thread);
                pthread_mutex_unlock(&m);
                continue;
            }
        } else if (olh == DROP_HEAD) {
            if (p->q->size == 0 && (req_handled == p->q->max_size)) {
                Close(connfd);
                // pthread_cond_broadcast(&c_thread);
                pthread_mutex_unlock(&m);
                continue;
            } else if (req_handled + p->q->size == p->q->max_size) {
                Close(p->q->head->sd);
                removeFromHead((void *) (p->q));
            }
        } else { //  if(olh == DROP_RANDOM)
            if(p->q->size == 0 && (req_handled == p->q->max_size))
            {
                Close(connfd);
                pthread_mutex_unlock(&m);
                continue;
            }
            else if(req_handled + p->q->size == p->q->max_size)
            {
                int count = 0;
                int initial_size = p->q->size;
                if(initial_size % 2 != 0)
                    initial_size++;
                while(count < initial_size/2)
                {
                    // Node tmp = p->q->head;
                    // while(tmp != NULL)
                    // {
                    //     if(rand() % 2 == 0)
                    //     {
                    //         Close(p->q->head->sd);
                    //         removeFromHead((void *) (p->q));
                    //         count++;
                    //     }
                    //     tmp = tmp->next;
                    // }

                    int to_delete = (rand() % p->q->size) + 1;
                    if(to_delete == 1)
                    {
                        Close(p->q->head->sd);
                        removeFromHead((void *) (p->q));
                    }
                    else
                    {
                        removeFromPos((void*) (p->q), to_delete); // removeFromPos closes the connection
                    }
                    count++;
                }
            }
        }

        insert((void *) p);

        pthread_cond_broadcast(&c_thread);
        pthread_mutex_unlock(&m);

        // 
        // HW3: In general, don't handle the request in the main thread.
        // Save the relevant info in a buffer and have one of the worker threads 
        // do the work. 
        // 
    }
}