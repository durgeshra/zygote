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

/**
 * Number of process groups
 */
int numGroups = 3;

/**
 * Process group a child belongs to,
 * this variable is set just before forking,
 * so that the child inherits the value and
 * it can be accessed in the child
 */
int childGroup = -1;

/**
 * Number of active USAPs
 */
vector<int> activeUsaps(numGroups, 0);

/**
 * Maximum Pool Size
 */
int usapPoolSizeMax = 10;

/**
 * Minimum Pool Size before it is refilled
 */
int usapPoolSizeMin = 5;

/**
 * Maximum number of active processes
 */
int activeProcessesMax = 15;

/**
 * PIDs of the forked processes
 */
vector<vector<int>> zygoteSocketPIDs(numGroups, vector<int>(usapPoolSizeMax, -1));

/**
 * Number of USAPs currently in existence
 */
vector<int> numUsaps(numGroups, 0);

/**
 * Client FDs of pending requests
 */
vector<queue<int>> pendingClientFD(numGroups);

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
 * Number of requests handled successfully
 */
int requestsHandled = 0;

/**
 * Mapping of child PIDs with corresponding groups,
 * accessed when a child process is terminated
 * to decrease the number of active USAPs of the corresponding group
 */
unordered_map<int, int> childPIDGroup;

/**
 * To track time taken in request handling
 */
struct timeval startTime;

void sigint(int snum);

/**
 * Sends FD from parent to child
 */
static void sendFD(int socket, int fd)
{
    cout << "LOG (sendFD " << getpid() << "): Sending FD " << fd << " from parent to child via FD " << socket << endl;

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
        cout << "LOG (sendFD): Failed to send message" << endl;
}

/**
 * Receives FD sent from the parent
 */
static int receiveFD(int socket)
{
    cout << "LOG (receiveFD " << getpid() << "): Receiving client FD from parent via FD " << socket << endl;

    struct msghdr msg = {0};

    char m_buffer[256];
    struct iovec io = {.iov_base = m_buffer, .iov_len = sizeof(m_buffer)};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    char c_buffer[256];
    msg.msg_control = c_buffer;
    msg.msg_controllen = sizeof(c_buffer);

    if (recvmsg(socket, &msg, 0) < 0)
        cout << "LOG (receiveFD): Failed to receive message" << endl;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

    unsigned char *data = CMSG_DATA(cmsg);

    cout << "LOG (receiveFD): About to extract FD" << endl;
    int fd = *((int *)data);
    cout << "LOG (receiveFD): Extracted FD " << fd << endl;

    return fd;
}

/**
 * Waits for a SIGINT, tht's all this function does
 * Called whenever forking leads to creation of a child
 */
void handleChild(){
    cout << "LOG (handleChild " << getpid() << " ): Waiting..." << endl;
    pause();
}

/**
 * Refills th USAP pool when the number of available forks falls below the minimum pool size
 */
void refillUsaps(int group)
{
    cout << "LOG (refillUsaps): Refilling USAP Pool for group " << group << endl;
    childGroup = group; // Used by the child to know its own group
    while (numUsaps[group] < usapPoolSizeMax)
    {
        int pid = fork();
        if (pid == 0)
            handleChild();
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

/**
 * Schedules a pending client request now that number of active processes is not maxed out
 */
void scheduleRequest(int group, int client)
{
    /**
     * Decides which child to assign the next request to
     */
    int indexAcquired = availableIndices[group].front();

    /**
     * FD of the sending end of socket connection `between parent and child'
     * Used to send the FD of the client to child
     */
    int sendFDSock = parentChildSock[group][indexAcquired].first;

    availableIndices[group].pop();
    unavailableIndices[group].push(indexAcquired);
    numUsaps[group] -= 1;

    cout << "LOG (main " << getpid() << "): Assigning next request to PID: " << zygoteSocketPIDs[group][indexAcquired] << endl;

    sendFD(sendFDSock, client);
    close(client);

    cout << "LOG (main " << getpid() << "): Sending SIGINT to child PID: " << zygoteSocketPIDs[group][indexAcquired] << endl;

    /**
     * Send SIGINT to the child who is about to handle the next incoming request
     */
    kill(zygoteSocketPIDs[group][indexAcquired], SIGINT);

    activeUsaps[group] += 1;

    requestsHandled += 1;
    if (requestsHandled % 25 == 0)
    {
        struct timeval stopTime;
        gettimeofday(&stopTime, NULL);
        double seconds = (double)(stopTime.tv_usec - startTime.tv_usec) / 1000000 + (double)(stopTime.tv_sec - startTime.tv_sec);
        cout << "LOG (main " << parentPID << "): " << requestsHandled << " requests handled in " << seconds << " seconds" << endl;
    }

    /**
     * Refill the pool if numUsaps falls below the minimum pool size
     */
    if (numUsaps[group] <= usapPoolSizeMin)
    {
        refillUsaps(group);
    }

    return;
}

int main(int argc, char const *argv[])
{
    int opt = 1;

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

    cout << "LOG (main): Listening for connections..." << endl;

    /**
     * PID of the last forked process
     */
    int lastForkPID = -1;

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
                cout << "Error creating unix-domain socket pair" << endl;
            }
            pair<int, int> newSock(sv[0], sv[1]);
            parentChildSock[g].push_back(newSock);
        }
    }

    /**
     * Fill up unavailableIndices before forking.
     * Used to identify receiving end of socket in the child,
     * and maintaining uniformity with refillUsaps function
     */
    for (int g = 0; g < numGroups; g++)
    {
        for (int i = 0; i < usapPoolSizeMax; i++)
        {
            unavailableIndices[g].push(i);
        }
    }

    cout << "LOG (main " << parentPID << "): Filling USAP pools for the first time" << endl;

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
                handleChild();
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
    }

    /**
     * Parent process allocates incoming requests to the children and 
     * maintains the number of children between minimum and maximum pool size
     */
    if (getpid() == parentPID)
    {
        cout << "LOG (main " << parentPID << "): Inside parent" << endl;
        gettimeofday(&startTime, NULL);

        while (true)
        {
            /**
             * FD of the client
             */
            int client;

            /**
             * Set to 1 if a valid request is received from the client
             */
            int validRequestReceived = 0;

            /**
             * Process group to which the incoming request is to be assigned
             */
            int group = -1;

            cout << "LOG (main " << getpid() << "): Looking for a new connection..." << endl;

            /**
             * Accepts incoming connection until a valid request is received
             */
            while (!validRequestReceived)
            {
                if ((client = accept(server_fd, (struct sockaddr *)&address,
                                     (socklen_t *)&addrlen)) < 0)
                {
                    cout << "LOG (main): Failure!" << endl;
                    exit(EXIT_FAILURE);
                }
                

                cout << "LOG (main " << getpid() << "): Connection accepted!" << endl;

                /**
                 * Process group ID as requested by the client
                 * Format: Group<Number>
                 * Example: Group0, Group1, etc.
                 */
                char requestGroup[64] = {0};
                int valread = read(client, requestGroup, 64);

                if (valread < 0)
                {
                    char toSend[] = "Error in reveiving group number from client.";
                    send(client, toSend, strlen(toSend), 0);
                    close(client);
                }

                string groupAssigned(requestGroup);
                if (groupAssigned.substr(0, 5) != "Group")
                {
                    char toSend[] = "Invalid group format from the client side.";
                    send(client, toSend, strlen(toSend), 0);
                    close(client);
                }
                else
                {
                    group = stoi(groupAssigned.substr(5, groupAssigned.length()));
                    if (group < 0 || group >= numGroups)
                    {
                        char toSend[] = "Invalid group number from the client side.";
                        send(client, toSend, strlen(toSend), 0);
                        close(client);
                    }
                    else
                    {
                        validRequestReceived = 1;
                    }
                }
            }

            cout << "LOG (main " << getpid() << "): Process group of new request: " << group << endl;

            cout << getpid() << " " << activeUsaps[0] << activeUsaps[1] << activeUsaps[2] << endl;

            /**
             * Ensure that the number of active processes is not more than the maximum pool size
             */
            if (activeUsaps[group] >= activeProcessesMax)
            {
                cout << "LOG (main " << parentPID << "): Active processes maxed out for group " << group << ", putting the request " << client << " on hold..." << endl;
                pendingClientFD[group].push(client);
                continue;
            }

            scheduleRequest(group, client);
        }
    }

    return 0;
}

void sigint(int snum)
{
    /**
     * Signal received by the parent to start working
     */
    signal(SIGINT, sigint);

    int childPID = getpid();

    cout << "LOG (sigint " << childPID << "): SIGINT received from parent" << endl;

    /**
     * Index of PID in parentChildSock
     * unavailableIndices.front() in the parent just before forking
     * contains the index of this child
     */
    int indexOfPID = unavailableIndices[childGroup].front();

    /**
     * FD of the receiving end of socket connection `between parent and child'
     * Used to receive the FD of the client from parent
     */
    int recvFDSock = parentChildSock[childGroup][indexOfPID].second;
    /**
     * FD of the client whose request this child is supposed to handle
     */
    int client = receiveFD(recvFDSock);

    cout << "LOG (sigint " << childPID << "): Received client FD: " << client << endl;

    string data = "Response from server (PID=";
    data.append(to_string(childPID));
    data.append("): Group Assigned is ");
    data.append(groupNames[childGroup]);

    char toSend[data.length() + 1];
    strcpy(toSend, data.c_str());

    usleep(5e6);

    send(client, toSend, strlen(toSend), 0);

    cout << "LOG (sigint " << childPID << "): Request processed from client" << endl;

    close(client);
    exit(0);
}

/**
 * Executed when a child has been terminated
 */
void childTerminated(int snum)
{
    int pid = waitpid(-1, NULL, WNOHANG);

    cout << "LOG (childTerminated " << getpid() << "): Process with PID " << pid << " exited" << endl;

    int terminatedChildGroup = childPIDGroup[pid];
    childPIDGroup.erase(pid);
    activeUsaps[terminatedChildGroup] -= 1;

    cout << activeUsaps[0] << activeUsaps[1] << activeUsaps[2] << endl;

    /**
     * If the process that just exited made the number of active process
     * less than activeProcessesMax, schedule a pending request if available
     */
    if (activeUsaps[terminatedChildGroup] == activeProcessesMax - 1)
    {
         if (!pendingClientFD[terminatedChildGroup].empty())
        {
            int client = pendingClientFD[terminatedChildGroup].front();
            cout << "LOG (childTerminated " << getpid() << "): Scheduling request from queue for client with FD " << client << endl;
            pendingClientFD[terminatedChildGroup].pop();
            scheduleRequest(terminatedChildGroup, client);
        }
    }

    return;
}
