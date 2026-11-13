#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <mqueue.h>

// dir name for posix msgQ (can be any name but "/" is needed in the front)
#define Q_NAME_1 "/pos_msgQ1"
#define Q_NAME_2 "/pos_msgQ2"
#define Q_PERM 0660
#define MAX_MSG 3
#define MAX_MSG_SIZE 256

static void sig_end()
{
    exit(EXIT_SUCCESS);
}

struct my_msg
{
    // long int msg_type;
    char data[MAX_MSG_SIZE];
};

int main(int argc, char *argv[])
{
    // key_t key;
    int child, nbytes;
    struct my_msg a_msg;
    struct mq_attr q_attr;     // must provide this attr struct
    mqd_t mqd1, mqd2;          // use for posix msgQ ID data type
    unsigned int priority = 0; // use this instead of long msg_type
    char buffer[MAX_MSG_SIZE];
    int msg_size = sizeof(struct my_msg);

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <[1, 2]>\n", *argv);
        exit(EXIT_FAILURE);
    }

    memset(buffer, '\0', MAX_MSG_SIZE);

    // attr init
    q_attr.mq_maxmsg = MAX_MSG;
    q_attr.mq_msgsize = sizeof(struct my_msg);

    mqd1 = mq_open(Q_NAME_1, O_RDWR | O_CREAT, 0660, &q_attr);
    if (mqd1 == -1)
    {
        perror("mq_open failed ");
        exit(EXIT_FAILURE);
    }

    mqd2 = mq_open(Q_NAME_2, O_RDWR | O_CREAT, 0660, &q_attr);
    if (mqd2 == -1)
    {
        perror("mq_open failed ");
        exit(EXIT_FAILURE);
    }

    signal(SIGUSR1, sig_end);
    argv++;
    if (strcmp(*argv, "1") == 0)
    {
        child = fork();
        switch (child)
        {
        case -1:
            perror("Forking failed\n");
            exit(EXIT_FAILURE);
        case 0:
            while (strncmp(a_msg.data, "end chat", 8))
            {
                nbytes = mq_receive(mqd2, (void *)&a_msg, msg_size, &priority);
                if (nbytes == -1)
                { // priority for receive need to be the reference
                    perror("mq_receive failed ");
                    exit(EXIT_FAILURE);
                }
                fputs(a_msg.data, stdout);
            }

            mq_close(mqd2); // close the posix msgQ
            if (mq_unlink(Q_NAME_2) == -1)
            { // unlink posix msgQ
                perror("mq_unlink failed ");
                exit(EXIT_FAILURE);
            }
            break;
        default:
            while (strncmp(buffer, "end chat", 8))
            {
                if (fgets(buffer, MAX_MSG_SIZE, stdin) != NULL)
                {
                    strcpy(a_msg.data, buffer);
                    if (mq_send(mqd1, (void *)&a_msg, msg_size, priority) == -1)
                    { // send must contain sizeof struct and priority value
                        perror("mq_send failed ");
                        exit(EXIT_FAILURE);
                    }
                }
            }
            mq_close(mqd1); // close the posix msgQ
        }
    }
    else if (strcmp(*argv, "2") == 0)
    {
        child = fork();
        switch (child)
        {
        case -1:
            perror("Forking failed\n");
            exit(EXIT_FAILURE);
        case 0:
            while (strncmp(a_msg.data, "end chat", 8))
            {
                nbytes = mq_receive(mqd1, (void *)&a_msg, msg_size, &priority);
                if (nbytes == -1)
                { // priority for receive need to be the reference
                    perror("mq_receive failed ");
                    exit(EXIT_FAILURE);
                }
                fputs(a_msg.data, stdout);
            }

            mq_close(mqd1); // close the posix msgQ
            if (mq_unlink(Q_NAME_1) == -1)
            { // unlink posix msgQ
                perror("mq_unlink failed ");
                exit(EXIT_FAILURE);
            }
            break;
        default:
            while (strncmp(buffer, "end chat", 8))
            {
                if (fgets(buffer, MAX_MSG_SIZE, stdin) != NULL)
                {
                    strcpy(a_msg.data, buffer);
                    if (mq_send(mqd2, (void *)&a_msg, msg_size, priority) == -1)
                    { // send must contain sizeof struct and priority value
                        perror("mq_send failed ");
                        exit(EXIT_FAILURE);
                    }
                }
            }
            mq_close(mqd2); // close the posix msgQ
        }
    }

    if (child)
        kill(child, SIGUSR1);
    else
        kill(getppid(), SIGUSR1);

    exit(EXIT_SUCCESS);
}