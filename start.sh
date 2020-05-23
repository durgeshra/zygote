#!/bin/bash

fuser -k 8080/tcp

g++ ZygoteServer.cpp -o zygoteServer
g++ ZygoteClient.cpp -o zygoteClient

echo "Starting Zygote Server... Please run the client executable from another terminal."
echo "---------------------------------------------------------------------------------"
./zygoteServer