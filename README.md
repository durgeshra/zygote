# Samsung Electronics Internship 2020: Durgesh Agrawal

## Milestone 1

### Zygote Server
• Pre-forks 10 processes in the USAP pool and waits for a socket connection from Zygote Client.
• On receiving a request, sets a process into action.
• If the USAP pool now contains less than 5 inactive processes, refills the pool.
### Zygote Client
• Sends request to Zygote Server via socket connection n times per second.
### Process
• After being set into action by Zygote Server, it prints log and exits.

## Milestone 2

### Zygote Server
• Maintains 3 process groups that are completely independent of each other.
• Receives a group id from client, and assigns the request to USAP pool of appropriate group based on the group id.
• Refills each USAP pool independently if a pool contains less than 5 processes.
### Zygote Client
• Sends request with a particular group id to Zygote Server via socket connection n times per second.
• Prints group name received.
Process
• After being set into action by Zygote Server, it sends group name to Zygote Client, prints log and exits.

## Milestone 3

### Zygote Server
• Limits the number of process of each group that can run at a time to 15.
• If 15 requests of a group are running and another request of the same group is received, it is put on hold until a running process finishes.

## How to use:

Run `./start.sh` in one terminal.

Run `./zygoteClient.sh` in another terminal.
