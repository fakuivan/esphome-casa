#pragma once
#include <memory>

#include "esphome/components/network/ip_address.h"
#include "esphome/components/socket/socket.h"
#include "etl/expected.h"
#include "etl/optional.h"
#include "etl/variant.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"

namespace ping_client {

void set_socket_timeout(socket::Socket &socket_, int timeout) {
  socket_.setsockopt(SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

uint16_t random_id() {
  uint16_t value;
  random_bytes(static_cast<uint8_t *>(static_cast<void *>(&value)),
               sizeof(value));
  return value;
}

#pragma pack(push, 1)
template <typename Header, size_t PayloadSize>
struct packet {
  Header header;
  std::array<uint8_t, PayloadSize> payload;
};
#pragma pack(pop)

using ping_req_packet = packet<icmp_echo_hdr, 4>;
template <size_t PayloadSize>
using ip_packet = packet<ip_hdr, PayloadSize>;

void init_ping_packet(ping_req_packet &packet, uint16_t sequence_number,
                      uint16_t id) {
  auto &header = packet.header;

  ICMPH_TYPE_SET((&header), ICMP_ECHO);
  ICMPH_CODE_SET((&header), 0);
  header.chksum = 0;
  header.id = htons(id);
  header.seqno = htons(sequence_number);

  for (auto &data : packet.payload) {
    data = 'Q';
  }

  header.chksum =
      inet_chksum(reinterpret_cast<void *>(&packet), sizeof(packet));
}

// Similar to `bit_cast`, except it can grab a subset of the
// origin memory of the object instead of having to cast the
// entire thing. Also the variable being filled into can be
// passed by reference.
template <typename To, typename From,
          enable_if_t<sizeof(To) <= sizeof(From) &&
                          is_trivially_copyable<From>::value &&
                          is_trivially_copyable<To>::value,
                      int> = 0>
bool bit_cast_to(const From &from, To &to, size_t begin) {
  if (begin + sizeof(To) > sizeof(From)) {
    return false;
  }
  memcpy(&to, reinterpret_cast<const unsigned char *>(&from) + begin,
         sizeof(To));
  return true;
}

template <typename NextHeader>
NextHeader get_ip_packet_next_header(ip_packet<sizeof(NextHeader)> &packet) {
  const auto ip_header_length = IPH_HL_BYTES(&packet.header);
  assert(ip_header_length <= sizeof(packet.header));
  NextHeader header;
  assert(bit_cast_to(packet, header, ip_header_length));
  return header;
}

struct BadSourceAddress {};

struct Errno {
  int code;
};

struct BufferEnd {};

struct DiscardError {
  size_t discarded;
  etl::variant<Errno, BufferEnd, BadSourceAddress> error;
};

template <typename Addr, typename AddrComparator>
etl::optional<DiscardError> recvfrom_discard(socket::Socket &sock,
                                             AddrComparator &&same_addr,
                                             const Addr &addr,
                                             size_t bytes_to_discard,
                                             size_t discard_buffer_size) {
  char discard_buffer[discard_buffer_size];
  ssize_t received;
  size_t total_received = 0;
  for (size_t bytes_left = bytes_to_discard; bytes_left > 0;
       bytes_left -= received) {
    Addr next_addr;
    socklen_t next_addr_size = sizeof(next_addr);
    size_t bytes_expected = std::min(sizeof(discard_buffer), bytes_left);
    received = sock.recvfrom(
        reinterpret_cast<void *>(discard_buffer), bytes_expected,
        reinterpret_cast<sockaddr *>(&next_addr), &next_addr_size);
    if (received < 0) {
      return {{total_received, Errno{errno}}};
    }
    total_received += received;
    if (next_addr_size != sizeof(next_addr) || !same_addr(addr, next_addr)) {
      return {{total_received, BadSourceAddress{}}};
    }
    if (received != bytes_expected) {
      // return ShortPacketWhatever
      return {{total_received, BufferEnd{}}};
    }
  }
  return {};
}

// Like a more debuggable assert, it won't completely crash the microcontroller,
// but it should at least disable the esphome component or cause the socket
// to be restarted.
//
// if assert is like "I'm stating this will hold otherwise everything goes to
// shit" Fatal is more like "I'm hoping to god this doesn't happen otherwise the
// library guys are insane"
struct Fatal {
  std::string reason;
};

using recvfrom_truncated_return =
    etl::expected<size_t, etl::variant<Fatal, Errno, BadSourceAddress>>;

// Reads the first bytes of an ipv4 packet and dumps the rest, next call to
// recvfrom should start with a new packet. returns the total size of the
// packet.
template <size_t PayloadSize, typename Addr, typename AddrComparator>
recvfrom_truncated_return recvfrom_truncated(AddrComparator &&same_addr,
                                             socket::Socket &sock,
                                             ip_packet<PayloadSize> &packet,
                                             Addr &addr,
                                             size_t discard_buffer_size = 512) {
  using unexpected = recvfrom_truncated_return::unexpected_type;
  constexpr auto peek_size = sizeof(packet);
  auto addr_size = sizeof(addr);
  ssize_t received =
      sock.recvfrom(reinterpret_cast<void *>(&packet), peek_size,
                    reinterpret_cast<sockaddr *>(&addr), &addr_size);

  if (received < 0) {
    return unexpected{Errno{errno}};
  }
  if (addr_size != sizeof(addr)) {
    return unexpected{BadSourceAddress{}};
  }
  if (received < peek_size) {
    return received;
  }
  assert(received == peek_size);

  uint16_t packet_length_from_header = ntohs(IPH_LEN(&packet.header));
  assert(packet_length_from_header >= received);
  if (IPH_OFFSET(&packet.header) != 0) {
    return unexpected{Fatal{"Cannot handle fragmented packets"}};
  }

  auto discard_error = recvfrom_discard(sock, std::move(same_addr), addr,
                                        packet_length_from_header - received,
                                        discard_buffer_size);

  if (!discard_error) {
    return packet_length_from_header;
  }

  DiscardError &error = *discard_error;
  if (auto errno_err = etl::get_if<Errno>(&error.error)) {
    // TODO: Is this a possible case?
    if (errno_err->code == EWOULDBLOCK || errno_err->code == EAGAIN) {
      return unexpected{
          Fatal{"Expected entire packet to be available in one go"}};
    }
    return unexpected{std::move(*errno_err)};
  }
  if (etl::holds_alternative<BufferEnd>(error.error)) {
    return unexpected{Fatal{"Expected more bytes for packet"}};
  }
  if (etl::holds_alternative<BadSourceAddress>(error.error)) {
    return unexpected{
        Fatal{"Expected the rest of the packet to come from the same address"}};
  }
  assert(false);
}

template <typename T>
ssize_t sendto_ipv4(socket::Socket &sock, const T &obj, int flags,
                    const sockaddr_in &addr) {
  return sock.sendto(
      // TODO: Fix esphome::Socket::sendto to respect const parameters
      const_cast<void *>(reinterpret_cast<const void *>(&obj)), sizeof(obj),
      flags, const_cast<sockaddr *>(reinterpret_cast<const sockaddr *>(&addr)),
      sizeof(addr));
}

struct Timeout {};

struct Waiting {};

struct BadPacket {};

struct OtherPacket {};

struct Reply {};

struct PingReply {
  uint latency_ms;
};

class Ping {
 private:
  std::unique_ptr<socket::Socket> sock;
  uint16_t sequence = 0;
  uint16_t id;
  uint timeout;
  unsigned long last_send_time;
  in_addr_t remote_address;
  bool waiting = false;

  Ping(std::unique_ptr<socket::Socket> &&sock, uint timeout,
       in_addr_t remote_address)
      : sock{std::move(sock)},
        id{random_id()},
        timeout{timeout},
        remote_address{remote_address} {}

  etl::optional<Errno> send_ping() {
    assert(sock);
    sequence++;
    ping_req_packet packet;
    init_ping_packet(packet, sequence, id);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = remote_address;
    auto sent = sendto_ipv4(*sock, packet, 0, addr);
    if (sent == -1) {
      return {Errno{errno}};
    }
    // It should not be possible to send a partial packet since this is not a
    // managed connection and the socket cannot be closed by the remote (is my
    // understanding)
    assert(sent == sizeof(packet));
    return {};
  }

  etl::variant<Waiting, BadPacket, Errno, BadSourceAddress, Fatal, Reply,
               OtherPacket>
  listen_ping() {
    assert(sock);

    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    ssize_t packet_len;
    ip_packet<sizeof(icmp_echo_hdr)> packet;
    // will it all be recieved in one single call?

    auto read_result = recvfrom_truncated(
        [](const sockaddr_in &addr1, const sockaddr_in &addr2) {
          return addr1.sin_addr.s_addr == addr2.sin_addr.s_addr &&
                 addr1.sin_family == addr2.sin_family;
        },
        *sock, packet, addr);
    if (!read_result) {
      auto error = read_result.error();
      if (auto maybe_errno = etl::get_if<Errno>(&error)) {
        if (maybe_errno->code == EWOULDBLOCK || maybe_errno->code == EAGAIN) {
          return Waiting{};
        }
        return {std::move(*maybe_errno)};
      }
      if (auto maybe_fatal = etl::get_if<Fatal>(&error)) {
        return {std::move(*maybe_fatal)};
      }
      if (auto maybe_bad_address = etl::get_if<BadSourceAddress>(&error)) {
        return {
            Fatal{"Got bad address size form recvfrom, maybe the socket was "
                  "not configured for ipv4?"}};
      }
      assert(false);
    }
    packet_len = read_result.value();
    if (addr.sin_addr.s_addr != remote_address) {
      return BadSourceAddress{};
    }
    if (packet_len != sizeof(packet)) {
      return BadPacket{};
    }
    // is checking packet type in ip header for icmp required?
    auto echo_header = get_ip_packet_next_header<icmp_echo_hdr>(packet);
    if (echo_header.type != ICMP_ER || ntohs(echo_header.id) != id ||
        ntohs(echo_header.seqno) != sequence) {
      return OtherPacket{};
    }
    return Reply{};
  }

  friend etl::optional<Ping> make_ping(uint timeout, in_addr_t remote_address);

 public:
  etl::variant<PingReply, Timeout, Waiting, Errno, Fatal> ping(
      unsigned long now_milliseconds) {
    if (!waiting) {
      last_send_time = now_milliseconds;
      auto result = send_ping();
      if (result) {
        return {std::move(*result)};
      }
      waiting = true;
      return {Waiting{}};
    }
    if ((last_send_time + timeout) > now_milliseconds) {
      last_send_time = now_milliseconds;
      auto result = send_ping();
      if (result) {
        return {std::move(*result)};
      }
      return {Timeout{}};
    }
    for (auto ping_result = listen_ping();
         !etl::holds_alternative<Waiting>(ping_result);
         ping_result = listen_ping()) {
      if (etl::holds_alternative<Reply>(ping_result)) {
        waiting = false;
        return PingReply{last_send_time - now_milliseconds};
      }
      if (etl::holds_alternative<BadPacket>(ping_result) ||
          etl::holds_alternative<OtherPacket>(ping_result) ||
          etl::holds_alternative<BadSourceAddress>(ping_result)) {
        continue;
      }
      if (auto fatal = etl::get_if<Fatal>(&ping_result)) {
        return {std::move(*fatal)};
      }
      if (auto errno_ = etl::get_if<Errno>(&ping_result)) {
        return {std::move(*errno_)};
      }
      assert(false);
    }
    return Waiting{};
  }
};

etl::optional<Ping> make_ping(uint timeout, in_addr_t remote_address) {
  auto sock = socket::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (!sock) {
    return {};
  }
  set_socket_timeout(*sock, timeout);

  return Ping{std::move(sock), timeout, remote_address};
}

etl::optional<Ping> make_ping(uint timeout, network::IPAddress addr) {
  if (!addr.is_ip4()) {
    return {};
  }
  return make_ping(timeout, static_cast<ip4_addr>(addr).addr);
}

template <typename T>
std::shared_ptr<T> make_copyable(etl::optional<T> &&maybe) {
  if (maybe) {
    return std::make_shared<T>(std::move(*maybe));
  }
  return std::shared_ptr<T>{};
}
}  // namespace ping_client