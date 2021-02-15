#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity)
    , _capacity(capacity)
    , _segments({})
    , _segments_size(0)
    , _first_unassembled(0)
    , _is_eof(false)
    , _eof_index(0) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (eof) {
        _is_eof = true;
        _eof_index = index + data.length();
        if (_is_eof && _output.bytes_written() == _eof_index) {
            _output.end_input();
        }
    }
    if (data.length() == 0)
        return;
    std::vector<Segment> new_segments = {{index, index + data.length() - 1, data}};
    if (_first_unassembled > new_segments[0].end_byte)
        return;
    if (_first_unassembled > new_segments[0].start_byte) {
        new_segments[0].content = new_segments[0].content.substr(_first_unassembled - new_segments[0].start_byte);
        new_segments[0].start_byte = _first_unassembled;
    }
    auto iter = upper_bound(_segments.begin(), _segments.end(), new_segments[0]);
    if (iter != _segments.begin()) {
        iter--;  // 找到一个 start_byte 小于等于 index 的判定重叠。最多找一个
        if (iter->end_byte >= new_segments[0].end_byte) {
            return;
        } else if (iter->end_byte >= new_segments[0].start_byte) {
            new_segments[0].content = new_segments[0].content.substr(1 + iter->end_byte - new_segments[0].start_byte);
            new_segments[0].start_byte = iter->end_byte + 1;
        }
    }
    for (size_t i = 0; i < new_segments.size(); i++) {
        iter = lower_bound(_segments.begin(), _segments.end(), new_segments[i]);
        if (iter == _segments.end())
            continue;
        if (iter->start_byte > new_segments[i].end_byte)
            continue;
        if (iter->end_byte < new_segments[i].end_byte) {
            Segment segment = {iter->end_byte + 1,
                               new_segments[i].end_byte,
                               new_segments[i].content.substr(iter->end_byte + 1 - new_segments[i].start_byte)};
            new_segments.push_back(segment);
        }
        new_segments[i].end_byte = iter->start_byte - 1;  // 注意：这里可能出现 bug，所以要拿 content 长度判定
        new_segments[i].content = new_segments[i].content.substr(0, iter->start_byte - new_segments[i].start_byte);
    }
    size_t new_add = 0;
    for (auto &new_segment : new_segments)
        new_add += new_segment.content.length();
    _segments_size += new_add;
    for (auto &new_segment : new_segments)
        if (new_segment.content.length()) {
            _segments.insert(upper_bound(_segments.begin(), _segments.end(), new_segment), new_segment);
        }
    for (size_t i = 0; i < _segments.size(); i++)
        if (i && _segments[i - 1].end_byte + 1 == _segments[i].start_byte) {
            _segments[i - 1].end_byte = _segments[i].end_byte;
            _segments[i - 1].content = _segments[i - 1].content + _segments[i].content;
            _segments.erase(_segments.begin() + i);
            i--;
        }
    while (!_segments.empty() && _segments_size + _output.buffer_size() > _capacity) {
        size_t minus = _segments_size + _output.buffer_size() - _capacity;
        Segment &last_segment = _segments[_segments.size() - 1];
        if (minus >= last_segment.content.length()) {
            _segments.pop_back();
            _segments_size -= last_segment.content.length();
        } else {
            last_segment.content = last_segment.content.substr(0, last_segment.content.length() - minus);
            last_segment.end_byte -= minus;
            _segments_size -= minus;
        }
    }
    if (!_segments.empty() && _first_unassembled == _segments[0].start_byte) {
        size_t writted = _output.write(_segments[0].content);
        _segments_size -= writted;
        if (writted == _segments[0].content.length()) {
            _segments.erase(_segments.begin());
        } else {
            _segments[0].content = _segments[0].content.substr(writted);
            _segments[0].start_byte += writted;
        }
        _first_unassembled += writted;
    }
    if (_is_eof && _output.bytes_written() == _eof_index) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _segments_size; }

bool StreamReassembler::empty() const { return _segments_size == 0; }
