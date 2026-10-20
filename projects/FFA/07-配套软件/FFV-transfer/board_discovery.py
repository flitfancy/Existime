"""Resolve the LAN device with mDNS, independently of proxy DNS interception."""
import ipaddress
import select
import socket
import struct
import time


LAN_NETWORKS = tuple(ipaddress.ip_network(value) for value in
                     ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16', '169.254.0.0/16'))


def is_lan_ipv4(value):
    try:
        address = ipaddress.IPv4Address(value)
    except (ValueError, TypeError):
        return False
    return any(address in network for network in LAN_NETWORKS)


def lan_interfaces():
    try:
        addresses = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET, socket.SOCK_DGRAM)
    except OSError:
        return []
    return sorted({item[4][0] for item in addresses if is_lan_ipv4(item[4][0])})


def _read_name(packet, offset):
    labels, seen, end = [], set(), None
    while True:
        if offset in seen or offset >= len(packet):
            raise ValueError('Invalid DNS name')
        seen.add(offset)
        size = packet[offset]
        if size & 0xc0 == 0xc0:
            if offset + 1 >= len(packet):
                raise ValueError('Truncated DNS pointer')
            if end is None:
                end = offset + 2
            offset = ((size & 0x3f) << 8) | packet[offset + 1]
            continue
        if size & 0xc0 or offset + 1 + size > len(packet):
            raise ValueError('Invalid DNS label')
        offset += 1
        if size == 0:
            return '.'.join(labels).lower(), end if end is not None else offset
        labels.append(packet[offset:offset + size].decode('ascii'))
        if sum(len(label) + 1 for label in labels) > 255:
            raise ValueError('DNS name too long')
        offset += size


def mdns_addresses(packet, host):
    """Accept live IPv4 records for the exact requested name, including compression."""
    try:
        _, flags, questions, answers, authority, additional = struct.unpack_from('!6H', packet)
        if not flags & 0x8000 or flags & 0xf or questions + answers + authority + additional > 128:
            return []
        offset, output = 12, []
        for _ in range(questions):
            _, offset = _read_name(packet, offset)
            offset += 4
        for _ in range(answers + authority + additional):
            name, offset = _read_name(packet, offset)
            kind, record_class, ttl, length = struct.unpack_from('!HHIH', packet, offset)
            offset += 10
            end = offset + length
            if end > len(packet):
                raise ValueError('Truncated DNS record')
            if name == host.lower().rstrip('.') and kind == 1 and record_class & 0x7fff == 1 and ttl and length == 4:
                address = socket.inet_ntoa(packet[offset:end])
                if is_lan_ipv4(address) and address not in output:
                    output.append(address)
            offset = end
        return output
    except (ValueError, UnicodeError, struct.error, OSError):
        return []


def resolve_mdns_ipv4(host, timeout=1.5):
    labels = host.rstrip('.').split('.')
    name = b''.join(bytes((len(label),)) + label.encode('ascii') for label in labels) + b'\0'
    # QU asks the board to reply to our ephemeral unicast port, avoiding a 5353 listener.
    query = struct.pack('!6H', 0, 0, 1, 0, 0, 0) + name + struct.pack('!HH', 1, 0x8001)
    sockets = []
    try:
        for address in lan_interfaces():
            connection = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sockets.append(connection)
            try:
                connection.bind((address, 0))
                connection.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(address))
                connection.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
                connection.setblocking(False)
                connection.sendto(query, ('224.0.0.251', 5353))
            except OSError:
                connection.close()
                sockets.pop()
        deadline = time.monotonic() + timeout
        while sockets:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            ready, _, _ = select.select(sockets, [], [], remaining)
            for connection in ready:
                try:
                    packet, source = connection.recvfrom(9000)
                except OSError:
                    continue
                if source[1] == 5353:
                    for address in mdns_addresses(packet, host):
                        if address == source[0]:
                            return address
        return ''
    finally:
        for connection in sockets:
            connection.close()


def discover_board_ipv4(host, port=7866):
    if host.lower().rstrip('.').endswith('.local'):
        address = resolve_mdns_ipv4(host)
        if address:
            return address
    try:
        addresses = socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_STREAM)
    except OSError:
        return ''
    return next((item[4][0] for item in addresses if is_lan_ipv4(item[4][0])), '')
