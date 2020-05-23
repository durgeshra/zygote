#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>

#define PORT 8080

// USE SIGNALS!!!!!!!!!!!!!!!!!!!!

using namespace std;

struct sockaddr_in address;
int addrlen = sizeof(address);
int server_fd;
int parentPID;
int activeUsaps = 0;

void sigint(int snum);
void sigusr1(int snum);

void refillUsaps(int &numUsaps, int usapPoolSizeMax, vector<int> &socketPIDs, vector<int> &availableIndices, vector<int> &unavailableIndices)
{
    // printf("FFFFFFF %d\n", getpid());
    while (numUsaps < usapPoolSizeMax)
    {
        int pid = fork();
        if (pid == 0)
            return;
        else
        {
            int unavailableIndex = unavailableIndices.back();
            unavailableIndices.pop_back();
            socketPIDs[unavailableIndex] = pid;
            numUsaps += 1;
            availableIndices.push_back(unavailableIndex);
        }
    }
    return;
}

int main(int argc, char const *argv[])
{
    void childTerminated(int);
    signal(SIGCHLD, childTerminated);

    int opt = 1;

    int usapPoolSizeMax = 1;
    int usapPoolSizeMin = 1;
    vector<int> socketPIDs;
    int numUsaps = 0;
    vector<int> availableIndices;
    vector<int> unavailableIndices;

    parentPID = getpid();

    printf("Parent PID: %d\n", parentPID);

    // Registering sighandlers
    signal(SIGINT, sigint);   // parent gives a green signal to child
    signal(SIGUSR1, sigusr1); // child sends message to parent that a connection request has been received

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Forcefully attaching socket to the port 8080
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

    int pid = -1;

    // Forking...
    printf("Forking...\n");

    while (numUsaps < usapPoolSizeMax)
    {
        pid = fork();
        if (pid == 0)
            break; // Child
        else
        {
            // std::cout << "OOLALA" << pid << std::endl;
            socketPIDs.push_back(pid);
            availableIndices.push_back(numUsaps);
            numUsaps += 1;
        }
    }

    if (getpid() == parentPID)
    {
        while (true)
        {
            // // Refill if no USAP available
            // if (availableIndices.size() == 0)
            // {
            //     refillUsaps(numUsaps, usapPoolSizeMax, socketPIDs, availableIndices, unavailableIndices);
            // }

            while (activeUsaps >= usapPoolSizeMax)
            {
                usleep(1e4);
                continue;
            }

            int indexAcquired = availableIndices.back();
            availableIndices.pop_back();
            unavailableIndices.push_back(indexAcquired);
            numUsaps -= 1;

            printf("Assigning next request to PID: %d\n", socketPIDs[indexAcquired]);

            kill(socketPIDs[indexAcquired], SIGINT);

            activeUsaps += 1;
            // printf("ACTIVE USAPS: %d\n", activeUsaps);

            if (numUsaps < usapPoolSizeMin)
            {
                refillUsaps(numUsaps, usapPoolSizeMax, socketPIDs, availableIndices, unavailableIndices);
                if (getpid() != parentPID)
                    break;
            }
            // printf("AAA %d %d\n", numUsaps, getpid());

            // pause(); // Receives SIGUSR1, saying that child has received a connection request. Can proceed with assigning next request to a new PID

            // // Don't create any more process until actveUsaps is maxed out
            // while (activeUsaps >=     usapPoolSizeMax)
            // {
            //     usleep(1e4);
            //     continue;
            // }
        }
    }
    if (getpid() != parentPID)
    {
        std::cout << "Child process: " << getpid() << std::endl;
        pause();
    }

    return 0;
}

void sigint(int snum)
{

    signal(SIGINT, sigint);
    int client;
    char *data = "Response from server";
    int valread;
    char buffer[1024] = {0};
    printf("SIGINT Received %d\n", getpid());

    if ((client = accept(server_fd, (struct sockaddr *)&address,
                         (socklen_t *)&addrlen)) < 0)
    {
        printf("Failure!\n");
        perror("accept");
        exit(EXIT_FAILURE);
    }
    printf("Connection accepted!\n");
    kill(parentPID, SIGUSR1);

    valread = read(client, buffer, 1024);
    printf("Server LOG %d: Data read from client.\n", getpid());
    printf("Server LOG %d: %s\n", getpid(), buffer);
    send(client, data, strlen(data), 0);
    printf("Response sent from server\n");
    close(client);
    exit(0);
}

void sigusr1(int snum)
{
    return;
}

void childTerminated(int snum)
{
    int pid;
    int status;

    pid = wait(&status);
    activeUsaps -= 1;
    // printf("");
}
