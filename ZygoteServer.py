##################################################
# Zygote Server
# Waits for a connection from zygote
# while maintaining preforked processes
##################################################
# Before testing, run `fuser -k 12345/tcp`

import socket
import time
import os

usapPoolSizeMax = 10
usapPoolSizeMin = 5

socketPIDs = []
pipes = []      # [(readFD, writeFD)]
numUsaps = 0
availableIndices = []

sock = socket.socket()
port = 12345
sock.bind(('', port))

print("Socket binded to "+str(port))

sock.listen(5)
print("Listening...")

startTime = time.time()

newFork = None
while numUsaps < usapPoolSizeMax:
    newFork = os.fork()

    if newFork > 0:     # Parent
        readFD, writeFD = os.pipe()
        pipes.append((readFD, writeFD))
        socketPIDs.append(newFork)
        numUsaps += 1
        availableIndices += [numUsaps-1]        # As of now, all USAPs are available
    else:               # Child, don't fork from here
        break

if newFork > 0:
    while True:
        client, addr = sock.accept()
        indexAcquired = availableIndices.pop()
        numUsaps -= 1
        writeFD = pipes[indexAcquired][1]
        print("Assigning request to PID "+str(socketPIDs[indexAcquired]))

        


else:

    client = ##

    clientData = client.recv(1024)
        
    print("Server LOG: Connection received from "+str(addr)+" at time "+str(time.time()-startTime))
    print("Data received from client: "+clientData)

    client.send("Connection established")

    client.close()