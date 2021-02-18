#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;



size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity();}

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active)   return ;
    _time_since_last_segment_received = 0;
    if (seg.header().syn) {
        printf("setting syn!\n");
        _get_syn = true;
    }
    if (seg.header().rst) {
        unclean_shutdown(false);
        return ;
    }
    _receiver.segment_received(seg);
    if (seg.header().ack)
        _sender.ack_received(seg.header().ackno, seg.header().win);
    if (seg.length_in_sequence_space()) {
        if (_get_syn) {
            _sender.fill_window();
        }
        if (_sender.segments_out().empty()) _sender.send_empty_segment();
        push_out_segments();
    }
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t ret = _sender.stream_in().write(data);
    if (_get_syn) {
        _sender.fill_window();
    }
    push_out_segments();
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active)   return ;
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        unclean_shutdown(true);
    }
    if (_get_syn) {
        _sender.fill_window();
    }
    push_out_segments();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    if (_get_syn) {
        _sender.fill_window();
    }
    push_out_segments();
}

void TCPConnection::connect() {
    _get_syn = true;
    if (_get_syn) {
        _sender.fill_window();
    }
    push_out_segments();
}

void TCPConnection::unclean_shutdown(bool send_rst) {
    if (send_rst) {
        _need_rst = true;
        if (_get_syn) {
            _sender.fill_window();
        }
        if (_sender.segments_out().empty()) _sender.send_empty_segment();
        push_out_segments();
    }
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _active = false;
}

void TCPConnection::push_out_segments() {
    // 不主动开始发送 syn

    TCPSegment tcpSegment;
    while (!_sender.segments_out().empty()) {
        tcpSegment = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            tcpSegment.header().ack = true;
            tcpSegment.header().ackno = _receiver.ackno().value();
            tcpSegment.header().win = _receiver.window_size();
        }
        if (_need_rst) {
            tcpSegment.header().rst = true;
            _need_rst = false;
        }
        _segments_out.push(tcpSegment);
    }
    maybe_clean_shutdown();
}

void TCPConnection::maybe_clean_shutdown() {
    if (_receiver.stream_out().input_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }
    if (_receiver.stream_out().input_ended() && _sender.bytes_in_flight() == 0 && _sender.stream_in().eof()) {
        if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
            _active = false;
        }
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            unclean_shutdown(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

bool TCPConnection::is_syn_closed() {
    return _sender.next_seqno_absolute() == 0;
}