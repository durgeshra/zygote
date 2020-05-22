##################################################
# Zygote Client
# Sends connection requests to ZygoteServer
##################################################

import socket
import time

reqPerSec = 25
sleepInterval = 1.0/reqPerSec
port = 12345

startTime = time.time()

requestNum = 0

while True:
    sock = socket.socket()
    sock.connect(('127.0.0.1', port))

    sock.send("Request Number "+str(requestNum))
    requestNum += 1
    print("Client LOG: Message received - "+sock.recv(1024)+" from server at time "+str(time.time()-startTime))

    sock.close()

    time.sleep(sleepInterval)