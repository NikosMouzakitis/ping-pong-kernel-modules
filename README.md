# ping-pong-kernel-modules

<load.sh>
Two kernel modules(server and client) are inserted into the linux kernel.

<start_server_command.sh>
It initializes the server module, after writing into its dedicated device, and awaits
for connection.

<ping_pong.sh>
It writes the ping pong command into the client's device and the ping-pong process
takes place.

Client sends an integer value to server, which as a minimal transformation is 
increasing each value by one, before sending the packet back to client.
Operation takes place for a 3 server replies cycle.


*Tested with OpenSuse running Linux linux-s1kn 4.12.14-lp150.12.25-default
