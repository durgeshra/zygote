##################################################
# Zygote Server
# Waits for a connection from zygote
# while maintaining preforked processes
##################################################
# Before testing, run `fuser -k 12345/tcp`

import socket
import time

usapPoolSizeMax = 10
usapPoolSizeMin = 5

sock = socket.socket()
port = 12345
sock.bind(('', port))

print("Socket binded to "+str(port))

sock.listen(5)
print("Listening...")

startTime = time.time()

while True:
    client, addr = sock.accept()
    print("Server LOG: Connection received from "+str(addr)+" at time "+str(time.time()-startTime))

    client.send("Connection established")

    client.close()