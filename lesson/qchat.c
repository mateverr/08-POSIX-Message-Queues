#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

static void sig_end()
{
    exit(EXIT_SUCCESS);
}

struct my_msg 
{
   long int msg_type;
   char data[BUFSIZ];
};

int main(int argc, char *argv[])
{
    key_t key;
    int msgID, child, nbytes;
    struct my_msg a_msg;
    char buffer[BUFSIZ] = "";
    
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <[1, 2]>\n", *argv);
        exit(EXIT_FAILURE);
    }

    key = ftok("./qchat", 202203);
    msgID = msgget(key, 0666 | IPC_CREAT); //110110110 | 1010000000000 -> 1010110110110
    if (msgID == -1)
    {
        perror("msgget() failed\n"); exit(EXIT_FAILURE);
    }

    signal(SIGUSR1, sig_end);
    argv++;
    if (strcmp(*argv, "1") == 0)
    {
        child = fork();
        switch (child)
        {
            case -1: perror("Forking failed\n"); exit(EXIT_FAILURE);
            case  0: while (strncmp(a_msg.data, "end chat", 8))
                     {
                         if ((nbytes = msgrcv(msgID, (void *) &a_msg, BUFSIZ, 2, 0)) > 0)
                             write(1, a_msg.data, nbytes);
                     }
                     if (msgctl(msgID, IPC_RMID, 0) == -1)
                     {
                         perror("msgctl failed: "); exit(EXIT_FAILURE);
                     }
                     break;
            default: while (strncmp(buffer, "end chat", 8))
                     {
                         if ((nbytes = read(0, buffer, BUFSIZ)) > 0)
                         {
                             a_msg.msg_type = 1; strcpy(a_msg.data, buffer);
                             msgsnd(msgID, (void *) &a_msg, nbytes,  0);
                         }
                     }
        }
    }
    else if (strcmp(*argv, "2") == 0)
    {
        child = fork();
        switch (child)
        {
            case -1: perror("Forking failed\n"); exit(EXIT_FAILURE);
            case  0: while (strncmp(a_msg.data, "end chat", 8))
                     {
                         if ((nbytes = msgrcv(msgID, (void *) &a_msg, BUFSIZ, 1, 0)) > 0)
                             write(1, a_msg.data, nbytes);
                     }
                     if (msgctl(msgID, IPC_RMID, 0) == -1)
                     {
                         perror("msgctl failed: "); exit(EXIT_FAILURE);
                     }
                     break;
            default: while (strncmp(buffer, "end chat", 8))
                     {
                         if ((nbytes = read(0, buffer, BUFSIZ)) > 0)
                         {
                             a_msg.msg_type = 2; strcpy(a_msg.data, buffer);
                             msgsnd(msgID, (void *) &a_msg, nbytes,  0);
                         }
                     }
        }
    }
        
    if (child) kill(child, SIGUSR1);
    else kill(getppid(), SIGUSR1);
   
    exit(EXIT_SUCCESS);
}