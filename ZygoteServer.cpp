#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/time.h>
#include <string>
#include <vector>
#include <queue>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>

#define PORT 8080

using namespace std;

struct sockaddr_in address;
int addrlen = sizeof(address);
int server_fd;
int parentPID;
int activeUsaps = 0;

void sigint(int snum);
void sigusr1(int snum);

/**
 * Refills th USAP pool when the number of available forks falls below the minimum pool size
 */
void refillUsaps(int &numUsaps, int usapPoolSizeMax, vector<int> &zygoteSocketPIDs, queue<int> &availableIndices, queue<int> &unavailableIndices)
{
    while (numUsaps < usapPoolSizeMax)
    {
        int pid = fork();
        if (pid == 0)
            return;
        else
        {
            int unavailableIndex = unavailableIndices.front();
            unavailableIndices.pop();
            zygoteSocketPIDs[unavailableIndex] = pid;
            numUsaps += 1;
            availableIndices.push(unavailableIndex);
        }
    }
    return;
}

int main(int argc, char const *argv[])
{
    int opt = 1;

    /**
     * Maximum Pool Size
     */
    int usapPoolSizeMax = 10;

    /**
     * Minimum Pool Size before it is refilled
     */
    int usapPoolSizeMin = 5;

    /**
     * PIDs of the forked processes
     */
    vector<int> zygoteSocketPIDs;

    /**
     * Number of USAPs currently in existence
     */
    int numUsaps = 0;

    /** 
     * Indices of available USAPs in the vector zygoteSocketPIDs
     */
    queue<int> availableIndices;

    /** 
     * Indices of unavailable USAPs in the vector zygoteSocketPIDs
     */
    queue<int> unavailableIndices;

    /**
     * PID of the parent process
     */
    parentPID = getpid();

    /**
     * Registers sighandlers
     */
    void childTerminated(int);
    signal(SIGCHLD, childTerminated); // Triggered on the termination of a child process
    signal(SIGINT, sigint);           // Parent gives a green signal to child
    signal(SIGUSR1, sigusr1);         // Child sends message to parent that a connection request has been received

    /**
     *  Createssocket file descriptor
     */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /**
     *  Forcefully attaches socket to the port 8080
     */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    /**
     *  Forcefully attaching socket to the port 8080
     */
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    /**
     * PID of the last forked process
     */
    int lastForkPID = -1;

    printf("Server LOG %d: Forking...\n", parentPID);

    /**
     * Forks new processes until maximum pool size is reached
     */
    while (numUsaps < usapPoolSizeMax)
    {
        lastForkPID = fork();
        if (lastForkPID == 0)
            break; // Child
        else
        {
            zygoteSocketPIDs.push_back(lastForkPID);
            availableIndices.push(numUsaps);
            numUsaps += 1;
        }
    }

    struct timeval startTime, stopTime;
    gettimeofday(&startTime, NULL);
    int requestsHandled = 0;

    /**
     * Parent process allocates incoming requests to the children and 
     * maintains the number of children between minimum and maximum pool size
     */
    if (getpid() == parentPID)
    {
        while (true)
        {
            /**
             * Ensure that the number of active processes is not more than the maximum pool size
             */
            while (activeUsaps >= usapPoolSizeMax)
            {
                usleep(1e4);
                continue;
            }

            /**
             * Decides which child to assign the next request to
             */
            int indexAcquired = availableIndices.front();
            availableIndices.pop();
            unavailableIndices.push(indexAcquired);
            numUsaps -= 1;

            printf("Server LOG %d: Assigning next request to PID: %d\n", parentPID, zygoteSocketPIDs[indexAcquired]);

            /**
             * Send SIGINT to the child who is about to handle the next incoming request
             */
            kill(zygoteSocketPIDs[indexAcquired], SIGINT);

            activeUsaps += 1;

            /**
             * Refill the pool if numUsaps falls below the minimum pool size
             */
            if (numUsaps <= usapPoolSizeMin)
            {
                refillUsaps(numUsaps, usapPoolSizeMax, zygoteSocketPIDs, availableIndices, unavailableIndices);
                if (getpid() != parentPID) // Necessary to move child processes formed in the refillUsaps function out of this (parent) block
                    break;
            }

            /**
             * Receives SIGUSR1, saying that child has received a connection request
             * Proceeds with assigning next request to a new PID on receiving SIGUSR1
             */
            pause();

            requestsHandled += 1;
            if (requestsHandled % 25 == 0)
            {
                gettimeofday(&stopTime, NULL);
                double seconds = (double)(stopTime.tv_usec - startTime.tv_usec) / 1000000 + (double)(stopTime.tv_sec - startTime.tv_sec);
                printf("Server LOG %d: %d requests handled in %f seconds\n", parentPID, requestsHandled, seconds);
            }
        }
    }

    /**
     * Child process accepts socket connection and handles the request on receiving SIGINT from the parent
     */
    if (getpid() != parentPID)
    {
        // std::cout << "Child process: " << getpid() << std::endl;
        pause();
    }

    return 0;
}

void sigint(int snum)
{
    signal(SIGINT, sigint);
    int childPID = getpid();

    int client;
    string data = "Response from server: Sent from PID ";
    data.append(to_string(childPID));
    char toSend[data.length() + 1];
    strcpy(toSend, data.c_str());

    int valread;
    char buffer[1024] = {0};

    /**
     * Accepts incoming connection
     */
    if ((client = accept(server_fd, (struct sockaddr *)&address,
                         (socklen_t *)&addrlen)) < 0)
    {
        printf("Failure!\n");
        perror("accept");
        exit(EXIT_FAILURE);
    }
    printf("Server LOG %d: Connection accepted!\n", childPID);

    /** 
     * Signals the parent that a connection request has been received, 
     * so that a new child can be assigned for the next incoming request
     */
    kill(parentPID, SIGUSR1);

    valread = read(client, buffer, 1024);
    printf("Server LOG %d: Data read from client.\n", childPID);
    printf("Server LOG %d: %s\n", childPID, buffer);

    send(client, toSend, strlen(toSend), 0);

    close(client);
    exit(0);
}

void sigusr1(int snum)
{
    return;
}

/**
 * Executed when a child has been terminated
 */
void childTerminated(int snum)
{
    activeUsaps -= 1;
}
