# ping_component

The general steps on many ping client implementations are the following:

1. configure socket to work with icmp pings
2. create random packet id
3. assemble header
4. create packet
5. compute and add checksum
6. open socket and send packet
7. wait for response with same id and type to arrive within specified time
8. read response and get trip time

## TODOs
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

## `USE_SOCKET_IMPL_` is kind of a mess

The file [`socket/__init__.py`](https://github.com/esphome/esphome/blob/30477c764d9353381ef6e8bf186bae703cffbc7f/esphome/components/socket/__init__.py#L15-L20) defines the implementation of the esphome socket interface for each platform.

So far only the platform `host` and `esp32` implement `IMPLEMENTATION_BSD_SOCKETS`
which is unfortunate because all the other ones don't define `recvfrom`, only `read`.
`read` is fine for things like UDP, but I'm not sure it will work for RAW sockets,
since, as far as I can see, it doesn't give you the whole packet, rather the part
after the IP header. This means it won't be possible to get the destination address
or the IP packet checksum, length, fragment, etc. How about using BPFs? Are those
only available in BSD implementation? Does `read` on RAW sockets give you the whole
packet?

As for the platform `IMPLEMENTATION_LWIP_TCP`, both the [udp](https://github.com/esphome/esphome/blob/30477c764d9353381ef6e8bf186bae703cffbc7f/esphome/components/udp/udp_component.h#L14) and wake_on_lan component
use the "WiFiUdp" library. I'm not sure if that supports RAW sockets, I doubt it. The only
platforms that have this issue however are the `esp8266` and `rp2040`, all the libretiny
chips support sockets.
