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
#include <fcntl.h>
#include <unordered_map>

#define PORT 8080

using namespace std;

struct sockaddr_in address;
int addrlen = sizeof(address);
int server_fd;
int parentPID;
int childGroup = -1;

/**
 * Number of process groups
 */
int numGroups = 3;

/**
 * Number of active USAPs
 */
vector<int> activeUsaps(numGroups, 0);

/**
 * Parent-Child sockets for sending client information to child
 */
vector<vector<pair<int, int>>> parentChildSock(numGroups);

/**
 * Indices of available USAPs in the vector zygoteSocketPIDs
 */
vector<queue<int>> availableIndices(numGroups);

/** 
 * Indices of unavailable USAPs in the vector zygoteSocketPIDs
 */
vector<queue<int>> unavailableIndices(numGroups);

/**
 * Group Names
 */
vector<string> groupNames{"Alpha", "Beta", "Gamma", "Delta", "Epsilon"};

/**
 * Mapping of child PIDs with corresponding groups,
 * accessed when a child process is terminated
 */
unordered_map<int, int> childPIDGroup;

void sigint(int snum);
void sigusr1(int snum);

static void wyslij(int socket, int fd) // send fd by socket
{
    struct msghdr msg = {0};
    char m_buffer[256];
    char buf[CMSG_SPACE(sizeof(fd))];
    memset(buf, '\0', sizeof(buf));
    struct iovec io = {.iov_base = m_buffer, .iov_len = 3};

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

    *((int *)CMSG_DATA(cmsg)) = fd;

    msg.msg_controllen = CMSG_SPACE(sizeof(fd));

    if (sendmsg(socket, &msg, 0) < 0)
        printf("Failed to send message\n");
}

static int odbierz(int socket) // receive fd from socket
{
    struct msghdr msg = {0};

    char m_buffer[256];
    struct iovec io = {.iov_base = m_buffer, .iov_len = sizeof(m_buffer)};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    char c_buffer[256];
    msg.msg_control = c_buffer;
    msg.msg_controllen = sizeof(c_buffer);

    if (recvmsg(socket, &msg, 0) < 0)
        printf("Failed to receive message\n");

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

    unsigned char *data = CMSG_DATA(cmsg);

    printf("About to extract fd\n");
    int fd = *((int *)data);
    printf("Extracted fd %d\n", fd);

    return fd;
}

/**
 * Refills th USAP pool when the number of available forks falls below the minimum pool size
 */
void refillUsaps(vector<int> &numUsaps, int group, int usapPoolSizeMax, vector<vector<int>> &zygoteSocketPIDs, vector<queue<int>> &availableIndices, vector<queue<int>> &unavailableIndices)
{
    childGroup = group; // Useful for the child to know its own group
    while (numUsaps[group] < usapPoolSizeMax)
    {
        int pid = fork();
        if (pid == 0)
            return;
        else
        {
            int unavailableIndex = unavailableIndices[group].front();
            unavailableIndices[group].pop();
            zygoteSocketPIDs[group][unavailableIndex] = pid;
            numUsaps[group] += 1;
            availableIndices[group].push(unavailableIndex);
            childPIDGroup[pid] = group;
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
    vector<vector<int>> zygoteSocketPIDs(numGroups, vector<int>(usapPoolSizeMax, -1));

    /**
     * Number of USAPs currently in existence
     */
    vector<int> numUsaps(numGroups, 0);

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
     * Fill parentChildSock completely before forking child processes
     */
    for (int g = 0; g < numGroups; g++)
    {
        for (int i = 0; i < usapPoolSizeMax; i++)
        {
            int sv[2];
            if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0)
            {
                printf("Error creating unix-domain socket pair\n.");
            }
            pair<int, int> newSock(sv[0], sv[1]);
            parentChildSock[g].push_back(newSock);
        }
    }

    /**
     * Fill up unavailableIndices before forking. Useful to identify receiving end of socket in the child,
     * and maintaining uniformity ith refillUsaps function
     */
    for (int g = 0; g < numGroups; g++)
    {
        for (int i = 0; i < usapPoolSizeMax; i++)
        {
            unavailableIndices[g].push(i);
        }
    }

    /**
     * Forks new processes until maximum pool size is reached
     */
    for (int g = 0; g < numGroups; g++)
    {
        childGroup = g; // Useful for the child to know its own group
        while (numUsaps[g] < usapPoolSizeMax)
        {
            lastForkPID = fork();
            if (lastForkPID == 0)
                break; // Child
            else
            {
                int unavailableIndex = unavailableIndices[g].front();
                unavailableIndices[g].pop();
                zygoteSocketPIDs[g][unavailableIndex] = lastForkPID;
                availableIndices[g].push(numUsaps[g]);
                numUsaps[g] += 1;
                childPIDGroup[lastForkPID] = g;
            }
        }
        if (lastForkPID == 0)
            break; // Child
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

            int client;
            int validRequestReceived = 0;
            int group = -1;

            /**
             * Accepts incoming connection
             */
            while (!validRequestReceived)
            {
                if ((client = accept(server_fd, (struct sockaddr *)&address,
                                     (socklen_t *)&addrlen)) < 0)
                {
                    printf("Failure!\n");
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                printf("Server LOG %d: Connection accepted!\n", parentPID);

                char requestGroup[64] = {0};

                int valread = read(client, requestGroup, 64);

                if (valread < 0)
                {
                    char toSend[] = "Error in reveiving group number from client.\n";
                    send(client, toSend, strlen(toSend), 0);
                    close(client);
                }

                string groupAssigned(requestGroup);
                if (groupAssigned.substr(0, 5) != "Group")
                {
                    char toSend[] = "Invalid group format from the client side.\n";
                    send(client, toSend, strlen(toSend), 0);
                    close(client);
                }
                else
                {
                    group = stoi(groupAssigned.substr(5, groupAssigned.length()));
                    if (group >= numGroups)
                    {
                        char toSend[] = "Invalid group number from the client side.\n";
                        send(client, toSend, strlen(toSend), 0);
                        close(client);
                    }
                    else
                    {
                        validRequestReceived = 1;
                    }
                }
            }

            /**
             * Ensure that the number of active processes is not more than the maximum pool size
             */
            while (activeUsaps[group] >= usapPoolSizeMax)
            {
                usleep(1e4);
                continue;
            }

            /**
             * Decides which child to assign the next request to
             */
            int indexAcquired = availableIndices[group].front();
            int sendFDSock = parentChildSock[group][indexAcquired].first;
            availableIndices[group].pop();
            unavailableIndices[group].push(indexAcquired);
            numUsaps[group] -= 1;

            printf("Server LOG %d: Assigning next request to PID: %d %d\n", parentPID, zygoteSocketPIDs[group][indexAcquired], indexAcquired);

            wyslij(sendFDSock, client);
            close(client);

            /**
             * Send SIGINT to the child who is about to handle the next incoming request
             */
            kill(zygoteSocketPIDs[group][indexAcquired], SIGINT);

            activeUsaps[group] += 1;

            /**
             * Refill the pool if numUsaps falls below the minimum pool size
             */
            if (numUsaps[group] <= usapPoolSizeMin)
            {
                refillUsaps(numUsaps, group, usapPoolSizeMax, zygoteSocketPIDs, availableIndices, unavailableIndices);
                if (getpid() != parentPID) // Necessary to move child processes formed in the refillUsaps function out of this (parent) block
                    break;
            }

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

    int indexOfPID = unavailableIndices[childGroup].front();
    int recvFDSock = parentChildSock[childGroup][indexOfPID].second;

    int client = odbierz(recvFDSock);

    int childPID = getpid();

    string data = "Response from server (PID=";
    data.append(to_string(childPID));
    data.append("): Group Assigned is ");
    data.append(groupNames[childGroup]);

    char toSend[data.length() + 1];
    strcpy(toSend, data.c_str());

    printf("Server LOG %d: Request processed from client.\n", childPID);

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
    int pid = waitpid(-1, NULL, WNOHANG);
    int terminatedChildGroup = childPIDGroup[pid];
    childPIDGroup.erase(pid);
    activeUsaps[terminatedChildGroup] -= 1;
}
