#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>
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

void refillUsaps(int &numUsaps, int usapPoolSizeMax, vector<int> &socketPIDs, queue<int> &availableIndices, queue<int> &unavailableIndices)
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
            socketPIDs[unavailableIndex] = pid;
            numUsaps += 1;
            availableIndices.push(unavailableIndex);
        }
    }
    return;
}

int main(int argc, char const *argv[])
{
    void childTerminated(int);
    signal(SIGCHLD, childTerminated);

    int opt = 1;

    int usapPoolSizeMax = 10;
    int usapPoolSizeMin = 5;
    vector<int> socketPIDs;
    int numUsaps = 0;
    queue<int> availableIndices;
    queue<int> unavailableIndices;

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

    int lastForkPID = -1;

    printf("Forking...\n");

    while (numUsaps < usapPoolSizeMax)
    {
        lastForkPID = fork();
        if (lastForkPID == 0)
            break; // Child
        else
        {
            socketPIDs.push_back(lastForkPID);
            availableIndices.push(numUsaps);
            numUsaps += 1;
        }
    }

    if (getpid() == parentPID)
    {
        while (true)
        {
            while (activeUsaps >= usapPoolSizeMax)
            {
                usleep(1e4);
                continue;
            }

            int indexAcquired = availableIndices.front();
            availableIndices.pop();
            unavailableIndices.push(indexAcquired);
            numUsaps -= 1;

            printf("Server LOG %d: Assigning next request to PID: %d\n", parentPID, socketPIDs[indexAcquired]);

            kill(socketPIDs[indexAcquired], SIGINT);

            activeUsaps += 1;

            if (numUsaps <= usapPoolSizeMin)
            {
                refillUsaps(numUsaps, usapPoolSizeMax, socketPIDs, availableIndices, unavailableIndices);
                if (getpid() != parentPID)
                    break;
            }

            pause(); // Receives SIGUSR1, saying that child has received a connection request. Can proceed with assigning next request to a new PID
        }
    }

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

    // printf("SIGINT Received %d\n", getpid());

    if ((client = accept(server_fd, (struct sockaddr *)&address,
                         (socklen_t *)&addrlen)) < 0)
    {
        printf("Failure!\n");
        perror("accept");
        exit(EXIT_FAILURE);
    }
    printf("Server LOG %d: Connection accepted!\n", childPID);

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

void childTerminated(int snum)
{
    int pid;
    int status;

    pid = wait(&status);
    activeUsaps -= 1;
}
