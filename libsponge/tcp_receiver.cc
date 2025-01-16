#include "tcp_receiver.hh"

#include <algorithm>
// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader header = seg.header();

    // 如果还没建立连接
    // 且收到的 seg 里没有 syn 这样希望建立连接的标志，那么不处理这种 seg
    if (!header.syn && !_synReceived) {
        return;
    }

    string data = seg.payload().copy();
    bool eof = false;

    // 如果还没建立连接，
    // 但收到的 seg 里有 syn 这样希望建立连接的标志，那么就开始建立连接
    if (header.syn && !_synReceived) {
        _synReceived = true;
        _isn = header.seqno;
        if (header.fin) {
            _finReceived = eof = true;
        }
        _reassembler.push_substring(data, 0, eof);
        return;
    }

    // 如果已经建立连接
    if (_synReceived && header.fin) {
        _finReceived = eof = true;
    }

    uint64_t checkpoint = _reassembler.ack_index();
    uint64_t abs_seqno = unwrap(header.seqno, _isn, checkpoint);
    uint64_t stream_idx = abs_seqno > 0 ? abs_seqno - 1 : 0;
    _reassembler.push_substring(data, stream_idx, eof);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_synReceived) {
        return nullopt;
    } else {
        // 期待收到的下一个字节的序号
        uint64_t expect = _reassembler.ack_index() + 1;
        // 这是处理 FIN 标志的特殊情况
        // 当接收到 FIN 且所有数据都已经读出时（stream 为空）
        // 需要再加1，因为 FIN 占用一个序号 这表示已经收到了所有数据，包括 FIN
        uint64_t extra = _reassembler.empty() && _finReceived;

        // 绝对序号转换为相对序号
        return wrap(expect + extra, _isn);
    }
}

// 流量控制
size_t TCPReceiver::window_size() const {
    //当前缓冲区已经使用的大小, 也就是已经收到但还未被上层应用读走的数据量
    uint64_t used = _reassembler.stream_out().buffer_size();
    if (used > _capacity) {
        return 0;
    }
    return _capacity - used;
}
