#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {
    return _sender.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const {
    return _sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const {
    return _receiver.unassembled_bytes();
}

size_t TCPConnection::time_since_last_segment_received() const {
    return _time_since_last_segment_received_counter;
}

bool TCPConnection::real_send() {
    bool isSend = false;
    while (!_sender.segments_out().empty()) {
        isSend = true;
        TCPSegment segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        set_ack_and_windowsize(segment);
        _segments_out.push(segment);
    }
    return isSend;
}
void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_segment_received_counter = 0;
    if (seg.header().rst) {
        // 一个突然的、强制的中断,Reset 的缩写
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        return;
    }
    _receiver.segment_received(seg);
    if (check_inbound_ended() && !_sender.stream_in().eof()) {
        //接收方已经收到了 FIN，表示对方说"我没话说了"
        //我这边还有话要说（发送方的数据流还没结束）
        //也没必要在结束时等待了
        _linger_after_streams_finish = false;
    }
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        // 对方：[已读] [窗口大小：100字] [10:01] <- 收到 ACK，带窗口大小
        // 那发送方就继续说
        real_send();
    }
    if (seg.length_in_sequence_space() > 0) {
        // 收到了有实际的内容,尝试回复
        _sender.fill_window();
        bool isSend = real_send();
        if (!isSend) {
//        对方：你在吗？[有实际内容]
//        你：
//          1. 检查是否有要回复的消息
//          2. 如果没有要回复的:
//             - 至少要发送一个"已读"标记
//             - 告诉对方"我看到了，但我暂时没有要说的"
            _sender.send_empty_segment();
            TCPSegment ACKSeg = _sender.segments_out().front();
            _sender.segments_out().pop();
            set_ack_and_windowsize(ACKSeg);
            _segments_out.push(ACKSeg);
        }
    }

    return;
}

bool TCPConnection::active() const {
     return _active;
 }

void TCPConnection::set_ack_and_windowsize(TCPSegment &segment) {
    optional<WrappingInt32> ackno = _receiver.ackno();
    if (ackno.has_value()) {
        segment.header().ack = true;
        segment.header().ackno = ackno.value();
    }
    // 本地接收器的接收能力
    size_t window_size = _receiver.window_size();
    // TCP 头部的窗口大小字段只有 16 位
    // 就像微信显示99+
    segment.header().win = static_cast<uint16_t>(window_size);
    return;
}

void TCPConnection::connect() {
    _sender.fill_window();
    real_send();
}

size_t TCPConnection::write(const string &data) {
    if (!data.size()) {
        return 0;
    }
    size_t actually_write = _sender.stream_in().write(data);
    _sender.fill_window();
    real_send();
    return actually_write;
}

// 决定结束通话
void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    real_send();
}

void TCPConnection::send_RST() {
    _sender.send_empty_segment();
    TCPSegment RSTSeg = _sender.segments_out().front();
    _sender.segments_out().pop();
    set_ack_and_windowsize(RSTSeg);
    RSTSeg.header().rst = true;
    _segments_out.push(RSTSeg);
}

// prereqs1 : The inbound stream has been fully assembled and has ended.
bool TCPConnection::check_inbound_ended() {
    return _receiver.unassembled_bytes() == 0 && _receiver.stream_out().input_ended();
}

// 检查发送方向是否完全结束
bool TCPConnection::check_outbound_ended() {
    // 我不想再说了
    return _sender.stream_in().eof()
    // 我要说的话都说完了吗,+2 是因为需要算上 SYN 和 FIN 标志
    && _sender.next_seqno_absolute()==_sender.stream_in().bytes_written()+2
    // 对方都收到了
    && _sender.bytes_in_flight() == 0;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received_counter += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.segments_out().size() > 0) {
        //如果输出队列中产生了需要重传的数据段
        // 类似于微信中红色感叹号的消息,需要重发 retransmit segment
        TCPSegment retxSeg = _sender.segments_out().front();
        _sender.segments_out().pop();
        set_ack_and_windowsize(retxSeg);
        if (_sender.consecutive_retransmissions()>_cfg.MAX_RETX_ATTEMPTS) {
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            retxSeg.header().rst = true;
            _active = false;
        }
        _segments_out.push(retxSeg);
    }
    if (check_inbound_ended() && check_outbound_ended()) {
        // 检查双向数据流都结束后,优雅关闭
        if (!_linger_after_streams_finish) {
            // 立即结束
            _active =false;
        } else if (_time_since_last_segment_received_counter>=10*_cfg.rt_timeout) {
            // 在流结束后再徘徊一会儿
            _active = false;
        }
    }
}

// 相当于 java 中的 finalize
TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            // 表示这是一个"不干净的关闭"。就像视频通话时手机突然没电了，而不是正常的说再见挂断。
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            send_RST();
            _active = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
