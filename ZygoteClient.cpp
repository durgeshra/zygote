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
    int sock = 0, valread;
    struct sockaddr_in serv_addr;

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    int reqPerSec = 1;
    float sleepInterval = 1.0 / reqPerSec;
    time_t startTime = time(NULL);

    int requestNum = 0;

    while (true)
    {

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            printf("\n Socket creation error \n");
            return -1;
        }

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        {
            printf("\nConnection Failed \n");
            return -1;
        }
        
        string data = "Command from client: ";
        data.append(to_string(requestNum));

        char toSend[data.length() + 1];
        strcpy(toSend, data.c_str());

        char buffer[1024] = {0};
        requestNum += 1;

        send(sock, toSend, strlen(toSend), 0);
        printf("Client LOG: Data sent from client.\n");
        valread = read(sock, buffer, 1024);
        printf("Client LOG: %s\n", buffer);

        close(sock);
        usleep(sleepInterval * 1e6);
    }

    return 0;
}
