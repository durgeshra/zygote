#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <string>

#define PORT 8080

using namespace std;

int main(int argc, char const *argv[])
{
    int zygoteClientSocket = 0, valread;

    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    /**
     * Converts IPv4 and IPv6 addresses from text to binary form\
     */
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        printf("LOG: Invalid address / Address not supported\n");
        return -1;
    }

    /**
     * Number of process groups
     */
    int numGroups = 3;

    /**
     * Number of reuests to be sent per unit time
     */
    int reqPerSec = 25;
    float sleepInterval = 1.0 / reqPerSec;
    time_t startTime = time(NULL);

    int requestNum = 0;

    while (true)
    {
        if ((zygoteClientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            printf("LOG: Socket creation error\n");
            return -1;
        }

        if (connect(zygoteClientSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        {
            printf("LOG: Connection Failed");
            return -1;
        }

        string data = "Group";
        data.append(to_string(requestNum % numGroups));

        char toSend[data.length() + 1];
        strcpy(toSend, data.c_str());

        char buffer[1024] = {0};
        requestNum += 1;

        send(zygoteClientSocket, toSend, strlen(toSend), 0);
        printf("LOG: Data sent from client to server\n");
        valread = read(zygoteClientSocket, buffer, 1024);
        printf("LOG: Data received from server\n%s\n", buffer);

        close(zygoteClientSocket);
        usleep(sleepInterval * 1e6);
    }

    return 0;
}
