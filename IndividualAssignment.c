#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM_CHILD 3
#define BUF_SIZE  256

static volatile sig_atomic_t interrupted = 0;
static int child_no = 0;   
static char child_int_msg[64];   

/* Ctrl + C handler for the parent */
void parent_sigint(int sig)
{
    (void)sig;
    const char msg[] =
        "\n\n[PARENT] *** Interrupt (Ctrl + C) detected! ***\n"
        "[PARENT] Stopping children and shutting down...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    interrupted = 1;
}

/* Ctrl + C handler for each child */
void child_sigint(int sig)
{
    (void)sig;
    write(STDOUT_FILENO, child_int_msg, strlen(child_int_msg));
    _exit(0);
}

void set_handler(void (*handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   
    sigaction(SIGINT, &sa, NULL);
}


ssize_t read_full(int fd, char *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r == 0)
            return got;         
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        got += r;
    }
    return got;
}


void child_process(int read_fd, int ack_fd)
{
    char msg[BUF_SIZE];

    snprintf(child_int_msg, sizeof(child_int_msg),
             "[CHILD %d] Interrupt received, terminating.\n", child_no);
    set_handler(child_sigint);
    printf("[CHILD %d] Started (PID %d, parent PID %d)\n",
           child_no, getpid(), getppid());
    fflush(stdout);

    
    while (read_full(read_fd, msg, BUF_SIZE) == BUF_SIZE) {
        printf("[CHILD %d | PID %d] Message received from parent: \"%s\"\n",
               child_no, getpid(), msg);
        fflush(stdout);
        write(ack_fd, "k", 1);   
    }

    printf("[CHILD %d] Pipe closed by parent, exiting.\n", child_no);
    close(read_fd);
    close(ack_fd);
    exit(0);
}

int main(void)
{
    int pipes[NUM_CHILD][2];   /* parent -> child message pipes */
    int ack[2];                /* child -> parent acknowledgement pipe */
    pid_t pids[NUM_CHILD];
    char msg[BUF_SIZE];
    int i;

    printf("===============================================\n");
    printf("  Fork & Pipe Message Passing Program\n");
    printf("  Parent PID: %d   Children: %d\n", getpid(), NUM_CHILD);
    printf("  Type a message and press Enter to send it.\n");
    printf("  Type \"exit\" to quit, or press Ctrl + C.\n");
    printf("===============================================\n");
    fflush(stdout);

    if (pipe(ack) == -1) {
        perror("pipe");
        exit(1);
    }

    /* Create one pipe and one child for each slot */
    for (i = 0; i < NUM_CHILD; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            exit(1);
        }

        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            exit(1);
        }

        if (pids[i] == 0) {
            /* Child: close every pipe end it does not use */
            int j;
            child_no = i + 1;
            for (j = 0; j < i; j++)
                close(pipes[j][1]);      
            close(pipes[i][1]);          /* own write end */
            close(ack[0]);               /* ack read end */
            child_process(pipes[i][0], ack[1]);
        }
        close(pipes[i][0]);
    }
    close(ack[1]);

    set_handler(parent_sigint);
    sleep(1);   

    
    while (!interrupted) {
        printf("\n[PARENT] Enter message: ");
        fflush(stdout);

        if (fgets(msg, BUF_SIZE, stdin) == NULL)
            break;               

        msg[strcspn(msg, "\n")] = '\0';
        if (strcmp(msg, "exit") == 0) {
            printf("[PARENT] Exit command received.\n");
            break;
        }
        if (msg[0] == '\0')
            continue;

        printf("[PARENT] Sending \"%s\" to %d children through pipes...\n",
               msg, NUM_CHILD);
        fflush(stdout);

        for (i = 0; i < NUM_CHILD; i++) {
            if (write(pipes[i][1], msg, BUF_SIZE) != BUF_SIZE)
                perror("write");
        }

        /* Wait until every child has displayed the message */
        for (i = 0; i < NUM_CHILD && !interrupted; i++) {
            char c;
            if (read(ack[0], &c, 1) <= 0)
                break;
        }
    }

    /* Closing the write ends tells the children there is nothing more */
    for (i = 0; i < NUM_CHILD; i++)
        close(pipes[i][1]);
    close(ack[0]);

    for (i = 0; i < NUM_CHILD; i++)
        waitpid(pids[i], NULL, 0);

    if (interrupted)
        printf("[PARENT] Program terminated by user interrupt (Ctrl + C).\n");
    else
        printf("[PARENT] All children finished. Goodbye!\n");

    return 0;
}
