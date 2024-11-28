The general steps on many ping client implementations are the following:

1. configure socket to work with icmp pings
2. create random packet id
3. assemble header
4. create packet
5. compute and add checksum
6. open socket and send packet
7. wait for response with same id and type to arrive within specified time
8. read response and get trip time
