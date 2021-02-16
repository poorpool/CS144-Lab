#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (seg.header().fin) {
        _has_fin = true;
        _fin = seg.header().seqno + seg.header().syn + seg.payload().size();
    }
    if (seg.header().syn) {
        if (_has_syn)
            return;
        _has_syn = true;
        _isn = seg.header().seqno;
        _reassembler.push_substring(seg.payload().copy(), 0, seg.header().fin);
    } else {
        if (!_has_syn)
            return;
        _reassembler.push_substring(seg.payload().copy(),
                                    unwrap(seg.header().seqno - 1, _isn, _reassembler.stream_out().bytes_written()),
                                    seg.header().fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_has_syn)
        return nullopt;
    uint64_t ret = _reassembler.stream_out().bytes_written() + 1;
    if (_has_fin && _isn != _fin && wrap(ret, _isn) == _fin)
        ret++;
    return wrap(ret, _isn);
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
