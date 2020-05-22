##################################################
# Zygote Server
# Waits for a connection from zygote
# while maintaining preforked processes
##################################################
# Before testing, run `fuser -k 12345/tcp`

import socket
import time
import os
from multiprocessing import Queue

usapPoolSizeMax = 10
usapPoolSizeMin = 5

socketPIDs = []
queues = []
numUsaps = 0
availableIndices = []
unavailableIndices = []
activeUsaps = 0
queueIndexForPID = {}

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
        queues.append(Queue())
        socketPIDs.append(newFork)
        queueIndexForPID[socketPIDs[numUsaps]] = numUsaps
        availableIndices.append(numUsaps)        # As of now, all USAPs are available
        numUsaps += 1
    else:               # Child, don't fork
        break
    
def refillUsaps():

    global numUsaps, usapPoolSizeMax, socketPIDs, availableIndices, newFork, queueIndexForPID

    assert(newFork!=0)
    while numUsaps < usapPoolSizeMax:
        newFork = os.fork()

        if newFork > 0:     # Parent
            unavailableIndex = unavailableIndices.pop()
            socketPIDs[unavailableIndex] = newFork
            queueIndexForPID[socketPIDs[unavailableIndex]] = unavailableIndex
            numUsaps += 1
            availableIndices.append(unavailableIndex)
        else:               # Child, don't fork
            break
    

if newFork > 0:
    refillUsaps()

    while True:
        client, addr = sock.accept()

        if len(availableIndices)==0:
            refillUsaps()

        indexAcquired = availableIndices.pop()
        # socketPIDs[indexAcquired] = -1
        unavailableIndices.append(indexAcquired)
        queue = queues[indexAcquired]
        numUsaps -= 1

        print("Assigning request to PID "+str(socketPIDs[indexAcquired]))
        
        activeUsaps += 1
        queue.put(client)
        queue.put(addr)

        while activeUsaps >= usapPoolSizeMax:
            time.sleep(0.01)
            continue
        
        if numUsaps < usapPoolSizeMin:
            refillUsaps()       


else:

    pid = os.getpid()
    while not pid in queueIndexForPID:
        continue
    
    queue = queueIndexForPID[pid]

    client = queue.get()
    addr = queue.get()

    clientData = client.recv(1024)
        
    print("Server LOG: Connection received from "+str(addr)+" at time "+str(time.time()-startTime))
    print("Data received from client: "+clientData)

    client.send("Connection established")

    client.close()
    del queueIndexForPID[pid]
    activeUsaps -= 1
    exit(0)
    