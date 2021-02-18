#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    if (_known_ethernet_address.count(next_hop_ip)) {
        EthernetFrame ethernetFrame;
        ethernetFrame.header().type = EthernetHeader::TYPE_IPv4;
        ethernetFrame.header().src = _ethernet_address;
        ethernetFrame.header().dst = _known_ethernet_address[next_hop_ip].second;
        ethernetFrame.payload() = dgram.serialize();
        _frames_out.push(ethernetFrame);
        return;
    }
    _queued_sends.emplace_back(dgram, next_hop);
    if (!_is_looking_up.count(next_hop_ip) || _curr_time >= _is_looking_up[next_hop_ip] + 5000) {
        _is_looking_up[next_hop_ip] = _curr_time;
        EthernetFrame ethernetFrame;
        ethernetFrame.header().type = EthernetHeader::TYPE_ARP;
        ethernetFrame.header().src = _ethernet_address;
        ethernetFrame.header().dst = ETHERNET_BROADCAST;
        ARPMessage arpMessage;
        arpMessage.opcode = ARPMessage::OPCODE_REQUEST;
        arpMessage.sender_ethernet_address = _ethernet_address;
        arpMessage.sender_ip_address = _ip_address.ipv4_numeric();
        // arpMessage.target_ethernet_address = ETHERNET_BROADCAST;
        arpMessage.target_ip_address = next_hop_ip;
        ethernetFrame.payload() = arpMessage.serialize();
        _frames_out.push(ethernetFrame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().dst != ETHERNET_BROADCAST && frame.header().dst != _ethernet_address) {
        return std::nullopt;
    }
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram internetDatagram;
        ParseResult result = internetDatagram.parse(frame.payload());
        if (result != ParseResult::NoError) {
            return std::nullopt;
        }
        _known_ethernet_address[internetDatagram.header().src] = make_pair(_curr_time, frame.header().src);

        return internetDatagram;
    } else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arpMessage;
        ParseResult result = arpMessage.parse(frame.payload());
        if (result != ParseResult::NoError) {
            return std::nullopt;
        }
        _known_ethernet_address[arpMessage.sender_ip_address] =
            make_pair(_curr_time, arpMessage.sender_ethernet_address);
        if (arpMessage.opcode == ARPMessage::OPCODE_REQUEST &&
            arpMessage.target_ip_address == _ip_address.ipv4_numeric()) {
            EthernetFrame ethernetFrame;
            ethernetFrame.header().type = EthernetHeader::TYPE_ARP;
            ethernetFrame.header().src = _ethernet_address;
            ethernetFrame.header().dst = arpMessage.sender_ethernet_address;
            ARPMessage am;
            am.opcode = ARPMessage::OPCODE_REPLY;
            am.sender_ethernet_address = _ethernet_address;
            am.sender_ip_address = _ip_address.ipv4_numeric();
            am.target_ethernet_address = arpMessage.sender_ethernet_address;
            am.target_ip_address = arpMessage.sender_ip_address;
            ethernetFrame.payload() = am.serialize();
            _frames_out.push(ethernetFrame);
        }
        try_send();
    }
    return std::nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _curr_time += ms_since_last_tick;
    auto iter = _known_ethernet_address.begin();
    while (iter != _known_ethernet_address.end()) {
        if (_curr_time >= iter->second.first + 30000) {
            _known_ethernet_address.erase(iter++);
        } else {
            iter++;
        }
    }
    try_send();
}

void NetworkInterface::try_send() {
    for (size_t i = 0; i < _queued_sends.size(); i++) {
        auto x = _queued_sends[i];
        IPv4Datagram frame = x.first;
        Address next_hop = x.second;
        if (_known_ethernet_address.count(next_hop.ipv4_numeric())) {
            EthernetFrame ethernetFrame;
            ethernetFrame.header().type = EthernetHeader::TYPE_IPv4;
            ethernetFrame.header().src = _ethernet_address;
            ethernetFrame.header().dst = _known_ethernet_address[next_hop.ipv4_numeric()].second;
            ethernetFrame.payload() = frame.serialize();
            _frames_out.push(ethernetFrame);
            if (i + 1 != _queued_sends.size()) {
                std::swap(_queued_sends[i], _queued_sends[_queued_sends.size() - 1]);
                _queued_sends.pop_back();
                i--;
            } else {
                _queued_sends.pop_back();
            }
        }
    }
}