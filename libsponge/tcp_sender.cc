#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {
    _retransmission_timer.setTimeout(retx_timeout);
}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    TCPSegment tcpSegment;
    if (!_syn) {
        _syn = true;
        tcpSegment.header().syn = true;
        tcpSegment.header().seqno = next_seqno();
        _next_seqno += tcpSegment.length_in_sequence_space();
        _segments_out.push(tcpSegment);
        _bytes_in_flight += tcpSegment.length_in_sequence_space();
        _outstanding_segments.push(tcpSegment);
        if (!_retransmission_timer._has_start) {
            _retransmission_timer.start();
        }
    } else {
        uint16_t window_size = _window_size ? _window_size : 1;
        uint64_t remain_size;
        while (!_fin) {
            remain_size = window_size - _next_seqno + _ackno; // 减去发出去的
            if (remain_size == 0)   break;
            string read_string = _stream.read(min(remain_size, TCPConfig::MAX_PAYLOAD_SIZE));
            tcpSegment.payload() = Buffer(std::move(read_string));
            if (_stream.eof() && tcpSegment.length_in_sequence_space() < remain_size) {
                _fin = true;
                tcpSegment.header().fin = true;
            }
            if (tcpSegment.length_in_sequence_space() == 0) break;
            tcpSegment.header().seqno = next_seqno();
            _next_seqno += tcpSegment.length_in_sequence_space();
            _segments_out.push(tcpSegment);
            _bytes_in_flight += tcpSegment.length_in_sequence_space();
            _outstanding_segments.push(tcpSegment);
            if (!_retransmission_timer._has_start) {
                _retransmission_timer.start();
            }
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t absolute_ackno = unwrap(ackno, _isn, _ackno);
    _window_size = window_size;
    if (absolute_ackno <= _ackno)    return ;
    _ackno = absolute_ackno;
    while (!_outstanding_segments.empty()) {
        TCPSegment tcpSegment = _outstanding_segments.front();
        if (unwrap(tcpSegment.header().seqno, _isn, _ackno) + tcpSegment.length_in_sequence_space() <= _ackno) {
            _bytes_in_flight -= tcpSegment.length_in_sequence_space();
            _outstanding_segments.pop();
        } else {
            break;
        }
    }
    _retransmission_timer.setTimeout(_initial_retransmission_timeout);
    _retransmission_timer.start();
    fill_window();
    _consecutive_retransmissions = 0;
    if (_bytes_in_flight == 0)
        _retransmission_timer.stop();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _retransmission_timer.tick(ms_since_last_tick);
    if (_retransmission_timer.is_timeout() && !_outstanding_segments.empty()) {
        TCPSegment tcpSegment = _outstanding_segments.front();
        _segments_out.push(tcpSegment);
        if (tcpSegment.header().syn || _window_size) { // 第一个死活收不到也要加倍
            _consecutive_retransmissions++;
            _retransmission_timer.double_timeout();
            _retransmission_timer.start();
        }
        _retransmission_timer.start();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment tcpSegment;
    tcpSegment.header().seqno = next_seqno();
    _segments_out.push(tcpSegment);
}
