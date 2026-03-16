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

typedef struct ant
{
    int ID;
    int path[MAX_PATH_LENGTH];
    int path_length;
}ant_t;


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

void child_work(node_t node, int index, int fd_w[MAX_GRAPH_NODES], int destination_idx){
    srand(getpid());
    set_handler(sig_handler, SIGINT);
    printf("{%d}: ", index);
    for(int i = 0; i < node.neighbours_num; i++){
        printf("%d ", node.indexes[i]);
    }
    printf("\n");
    read_fd = node.pipe[0];
    int fd_r = open(FIFO_NAME, O_WRONLY);
    while(!stop_work){
        ant_t ant;
        if(read(node.pipe[0], &ant, sizeof(ant)) < 0){
            if(errno == EINTR || errno == EBADF){
                break;
            }
            ERR("read");
        }
        msleep(100);
        ant.path[ant.path_length++] = index;
        if(node.neighbours_num == 0 || ant.path_length == MAX_PATH_LENGTH){
            printf("Ant {%d}: got lost\n", ant.ID);
        }
        else if(index == destination_idx){
            printf("Ant {%d}: found food\n", ant.ID);
            if(write(fd_r, &ant, sizeof(ant)) == -1){
                ERR("write");
            }
        }
        else{
            int r = rand()%node.neighbours_num;
            if(write(fd_w[node.indexes[r]], &ant, sizeof(ant)) == -1){
                if(errno == EINTR){
                    break;
                }
                if(errno == EPIPE){
                    printf("Ant {%d}: got lost\n", ant.ID);
                    continue;
                }
                fprintf(stderr, "%d\n", r);
                ERR("write");
            }
            if(rand()%50 == 0){
                printf("Node {%d}: collapsed\n", index);
                break;
            }
        }
    }
    close(fd_r);
}



int main(int argc, char* argv[])
{
    set_handler(SIG_IGN, SIGINT);
    set_handler(SIG_IGN, SIGPIPE);
    unlink(FIFO_NAME);
    mkfifo(FIFO_NAME, 0666);
    if (argc != 4)
        usage(argc, argv);
    int starting_index = atoi(argv[2]);
    int destination_index = atoi(argv[3]);
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
            child_work(graph.nodes[i], i, fd_w, destination_index);
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
    for(int i = 0; i < graph.node_num; i++){
        if(i == starting_index){
            close(graph.nodes[i].pipe[0]);
        }
        else{
            close(graph.nodes[i].pipe[0]);
            close(graph.nodes[i].pipe[1]);
        }
    }    

    int fd = open(FIFO_NAME, O_RDONLY | O_NONBLOCK);
    if(fd == -1){
        ERR("open");
    }


    int ID = 0;
    while(1){
        msleep(1000);
        ant_t ant = {};
        ant.ID = ID++;
        if(write(graph.nodes[starting_index].pipe[1], &ant, sizeof(ant)) == -1){
            if(errno == EPIPE){
                break;
            }
            ERR("write");
        }
        if(read(fd, &ant, sizeof(ant)) < 0){
            if(errno == EAGAIN){
                continue;
            }
            ERR("read");
        }
        printf("Ant {%d} path: ", ant.ID);
        for(int i = 0; i < ant.path_length; i++){
            printf("%d ", ant.path[i]);
        }
        printf("\n");
    }

    
    kill(0, SIGINT);
    while(wait(NULL) > 0){}
    close(graph.nodes[starting_index].pipe[1]);
    close(fd);
    unlink(FIFO_NAME);
    exit(EXIT_SUCCESS);
}
