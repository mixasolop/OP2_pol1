#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_GRAPH_NODES 32
#define MAX_PATH_LENGTH (2 * MAX_GRAPH_NODES)

#define FIFO_NAME "/tmp/colony_fifo"

volatile sig_atomic_t stop_work;
static int read_fd;
typedef struct node
{
    int indexes[MAX_GRAPH_NODES];
    int neighbours_num;
    int pipe[2];
}node_t;


typedef struct graph
{  
    node_t nodes[MAX_GRAPH_NODES];
    int node_num;
}graph_t;



int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

void sig_handler(int signal){
    if(signal == SIGINT){
        stop_work = 1;
        close(read_fd);
    }
}

void msleep(int ms)
{
    struct timespec tt;
    tt.tv_sec = ms / 1000;
    tt.tv_nsec = (ms % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

void usage(int argc, char* argv[])
{
    printf("%s graph start dest\n", argv[0]);
    printf("  graph - path to file containing colony graph\n");
    printf("  start - starting node index\n");
    printf("  dest - destination node index\n");
    exit(EXIT_FAILURE);
}

graph_t read_colony(char* name){
    FILE* fl = fopen(name, "r");
    if(fl == NULL){
        ERR("fopen");
    }

    graph_t graph;
    if(fscanf(fl, "%d", &graph.node_num) != 1){
        ERR("fscanf");
    }

    while(1){
        int from;
        int to;
        if(fscanf(fl, "%d %d", &from, &to) != 2){
            break;
        }  
        node_t* node_ptr = &graph.nodes[from];
        node_ptr->indexes[node_ptr->neighbours_num++] = to;
    }
    fclose(fl);
    return graph;
}

void child_work(node_t node, int index, int fd_w[MAX_GRAPH_NODES]){
    set_handler(sig_handler, SIGINT);
    printf("{%d}: ", index);
    for(int i = 0; i < node.neighbours_num; i++){
        printf("%d ", node.indexes[i]);
    }
    printf("\n");
    read_fd = node.pipe[0];
    while(!stop_work){
        char p;
        if(read(node.pipe[0], &p, 1) < 0){
            if(errno == EINTR || errno == EBADF){
                break;
            }
            ERR("read");
        }
    }
}



int main(int argc, char* argv[])
{
    set_handler(SIG_IGN, SIGINT);
    if (argc != 4)
        usage(argc, argv);

    graph_t graph = read_colony(argv[1]);
    for(int i = 0; i < graph.node_num; i++){
        if(pipe(graph.nodes[i].pipe) == -1){
            ERR("pipe");
        }
    }
    for(int i = 0; i < graph.node_num; i++){
        pid_t pid = fork();
        if(pid == -1){
            ERR("fork");
        }
        else if(pid == 0){
            int fd_w[MAX_GRAPH_NODES];
            for(int j = 0; j < graph.node_num; j++){
                if(i == j){
                    close(graph.nodes[i].pipe[1]);
                }
                else{
                    close(graph.nodes[j].pipe[0]);
                    fd_w[j] = graph.nodes[j].pipe[1];
                }
            }
            child_work(graph.nodes[i], i, fd_w);
            for(int j = 0; j < graph.node_num; j++){
                if(i == j){
                    close(graph.nodes[i].pipe[0]);
                }
                else{
                    close(graph.nodes[j].pipe[1]);
                }
            }
            exit(EXIT_SUCCESS);
        }
    }

    while(wait(NULL) > 0){}

    exit(EXIT_SUCCESS);
}
