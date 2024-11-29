The general steps on many ping client implementations are the following:

1. configure socket to work with icmp pings
2. create random packet id
3. assemble header
4. create packet
5. compute and add checksum
6. open socket and send packet
7. wait for response with same id and type to arrive within specified time
8. read response and get trip time

TODOs:
* When checking packets, check for fragmentation last. This can avoid
  non echo reply packets being an issue.
* Maybe look into making the discard part of the state machine be
  pausable, this should avoid issues with big packets hogging the
  CPU
* Handle EWOULDBLOCK and EAGAIN when in the middle of a packet read:
  I'm not sure this is necesary, double check with someone that knows
  about lwip
* Maybe handle fragmented ping replies, I'm not sure if that's even
  allowed in IP. This would probably be easier if we added more
  pauses in the code like for packet discard.
* Overhaul the error types
* Put debug statements that can be skipped with a macro, printing
  takes a surprisingly long time
* Check the contents of the ping reply, or at least provide a flag
  to disable/enable it
