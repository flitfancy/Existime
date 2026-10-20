"""The device session must recover from proxy DNS and stale DHCP addresses."""
import unittest
from unittest.mock import Mock, patch
import socket
import struct

import app as runtime
import board_discovery as discovery


HOST = 'firefly-voice.local'


def response(address='192.168.1.40', host=HOST, ttl=120):
    name = b''.join(bytes((len(part),)) + part.encode() for part in host.split('.')) + b'\0'
    return (struct.pack('!6H', 0, 0x8400, 1, 1, 0, 0) + name + struct.pack('!HH', 1, 0x8001)
            + b'\xc0\x0c' + struct.pack('!HHIH', 1, 0x8001, ttl, 4) + socket.inet_aton(address))


class MdnsTests(unittest.TestCase):
    def test_compressed_board_reply(self):
        self.assertEqual(discovery.mdns_addresses(response(), HOST), ['192.168.1.40'])

    def test_unrelated_name_and_goodbye_are_ignored(self):
        self.assertEqual(discovery.mdns_addresses(response(host='other.local'), HOST), [])
        self.assertEqual(discovery.mdns_addresses(response(ttl=0), HOST), [])

    def test_proxy_fake_ip_is_rejected(self):
        self.assertEqual(discovery.mdns_addresses(response(address='198.18.0.118'), HOST), [])

    def test_malformed_packets_do_not_escape_or_loop(self):
        valid = response()
        for size in range(len(valid)):
            with self.subTest(size=size):
                self.assertEqual(discovery.mdns_addresses(valid[:size], HOST), [])
        cycle = struct.pack('!6H', 0, 0x8400, 1, 0, 0, 0) + b'\xc0\x0c\0\1\0\1'
        self.assertEqual(discovery.mdns_addresses(cycle, HOST), [])

    def test_mdns_reply_prevents_system_dns_lookup(self):
        with patch.object(discovery, 'resolve_mdns_ipv4', return_value='192.168.1.40'), \
                patch.object(discovery.socket, 'getaddrinfo') as dns:
            self.assertEqual(discovery.discover_board_ipv4(HOST), '192.168.1.40')
        dns.assert_not_called()

    def test_dns_fallback_never_returns_fake_ip(self):
        fake = [(2, 1, 6, '', ('198.18.0.118', 7866))]
        with patch.object(discovery, 'resolve_mdns_ipv4', return_value=''), \
                patch.object(discovery.socket, 'getaddrinfo', return_value=fake):
            self.assertEqual(discovery.discover_board_ipv4(HOST), '')
        real = fake + [(2, 1, 6, '', ('192.168.1.41', 7866))]
        with patch.object(discovery, 'resolve_mdns_ipv4', return_value=''), \
                patch.object(discovery.socket, 'getaddrinfo', return_value=real):
            self.assertEqual(discovery.discover_board_ipv4(HOST), '192.168.1.41')

    def test_tun_and_loopback_interfaces_are_excluded(self):
        addresses = [(2, 2, 17, '', (ip, 0)) for ip in
                     ('198.18.0.1', '127.0.0.1', '192.168.1.15', '192.168.1.15')]
        with patch.object(discovery.socket, 'getaddrinfo', return_value=addresses):
            self.assertEqual(discovery.lan_interfaces(), ['192.168.1.15'])

    def test_mdns_socket_uses_physical_interface_and_closes(self):
        connection = Mock()
        connection.recvfrom.return_value = (response(), ('192.168.1.40', 5353))
        with patch.object(discovery, 'lan_interfaces', return_value=['192.168.1.15']), \
                patch.object(discovery.socket, 'socket', return_value=connection), \
                patch.object(discovery.select, 'select', return_value=([connection], [], [])):
            self.assertEqual(discovery.resolve_mdns_ipv4(HOST), '192.168.1.40')
        connection.bind.assert_called_once_with(('192.168.1.15', 0))
        self.assertEqual(connection.sendto.call_args.args[1], ('224.0.0.251', 5353))
        self.assertTrue(connection.sendto.call_args.args[0].endswith(b'\0\1\x80\1'))
        connection.close.assert_called_once()

    def test_no_lan_interface_returns_without_waiting(self):
        with patch.object(discovery, 'lan_interfaces', return_value=[]), \
                patch.object(discovery.select, 'select') as wait:
            self.assertEqual(discovery.resolve_mdns_ipv4(HOST), '')
        wait.assert_not_called()


class BoardDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.saved = dict(runtime.state)
        self.addCleanup(lambda: (runtime.state.clear(), runtime.state.update(self.saved)))
        runtime.state.update(wifi_ip='', wifi_connected=False)

    def test_discovery_uses_lan_reply_instead_of_proxy_fake_ip(self):
        fake = [(2, 1, 6, '', ('198.18.0.118', 7866))]
        with patch.object(runtime.socket, 'getaddrinfo', return_value=fake), \
                patch.object(runtime, 'discover_board_ipv4', return_value='192.168.1.40', create=True):
            self.assertEqual(runtime.discover_wifi_ip(), '192.168.1.40')
        self.assertFalse(runtime.state['wifi_connected'])

    def test_failed_cached_address_is_rediscovered(self):
        runtime.state.update(wifi_ip='192.168.1.39', wifi_connected=True)
        connection = Mock()
        with patch.object(runtime, 'SocketConnection', side_effect=[OSError('old address'), connection]) as factory, \
                patch.object(runtime, 'discover_wifi_ip', return_value='192.168.1.40'):
            self.assertIs(runtime.open_board(), connection)
        self.assertEqual([call.args[0] for call in factory.call_args_list],
                         ['192.168.1.39', '192.168.1.40'])
        self.assertTrue(runtime.state['wifi_connected'])
        self.assertEqual(runtime.state['wifi_ip'], '192.168.1.40')

    def test_fake_cached_address_is_never_connected(self):
        runtime.state['wifi_ip'] = '198.18.0.118'
        with patch.object(runtime, 'SocketConnection', return_value=Mock()) as factory, \
                patch.object(runtime, 'discover_wifi_ip', return_value='192.168.1.40'):
            runtime.open_board()
        factory.assert_called_once_with('192.168.1.40', 7866)


if __name__ == '__main__':
    unittest.main()
