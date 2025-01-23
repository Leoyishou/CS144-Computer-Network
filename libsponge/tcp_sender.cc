#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

#include <algorithm>

#include <iostream>

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
    , _stream(capacity)
    , _next_seqno(0)
    , _alarm(retx_timeout)
    , _rwindow(1)
    , _recx_windowsize(1) // Receiver's Window Size"
    , _acknos(0)  // 确认号, 我已经收到了序号小于_acknos的所有数据
    , _RTO(retx_timeout)  // Retransmission TimeOut（重传超时时间）
    , _conse_retrans(0)  // Consecutive Retransmissions（连续重传次数）
    , _synSend(false)
    , _finSend(false)
     {}

uint64_t TCPSender::bytes_in_flight() const {
    return _next_seqno - _acknos;
}

void TCPSender::send(const TCPSegment& segment) {
    // 刷新下一个index
    _next_seqno += segment.length_in_sequence_space();
    _segments_out.push(segment);
    if (segment.length_in_sequence_space() > 0) {
        // 发 segment
        _outstanding.push(segment);
        if (!_alarm.isStarted()) {
            // 启动闹钟
            _alarm.start(_RTO); // 重传超时时间
        }
    }
}

// 第一次握手
void TCPSender::send_SYN() {
    TCPSegment segment;
    segment.header().seqno = next_rela_seqno(); // 下一天(相对序号)
    segment.header().syn = true;
    _synSend = true;
    send(segment);
}

// 往接收方的"水桶"里倒水的过程
void TCPSender::fill_window() {
 // 第一步:确定remainWindowSize,即水桶里的空间还有多少
    if (_finSend) {
        return;
    }
    if (_next_seqno == 0 && !_synSend) {
        // 如果还从未发过 SYN
        send_SYN();
    }
    int remainWindowSize = 0;
    if (_rwindow >= next_abs_seqno()) {
        // 窗口上限大于下一个要发的序号,说明还有空间可以发送
        remainWindowSize = _rwindow - next_abs_seqno();
    } else {
        // 对方的窗口已经满了,需要等待
        return;
    }

    // 特殊情况:即使对方说水桶已经满了,也尝试倒一滴水
    if (_recx_windowsize == 0 && remainWindowSize == 0) {
        remainWindowSize = 1;
    }

// 第二步:把剩余的空间到满水
    while (remainWindowSize > 0) {
        TCPSegment segment;
        segment.header().seqno = next_rela_seqno();
        // 找到三者中的最短板
        size_t len_can_read = min(int(TCPConfig::MAX_PAYLOAD_SIZE)
                                 , remainWindowSize);
        len_can_read = min(_stream.buffer_size(), len_can_read);
        remainWindowSize -= len_can_read;
        Buffer buffer(_stream.read(len_can_read));
        segment.payload() = buffer;

        //
        if (_stream.eof() && !_finSend && remainWindowSize > 0) {
            remainWindowSize--;
            segment.header().fin = true;
            _finSend = true;
        }

        if (segment.length_in_sequence_space() > 0) {
            send(segment);
        } else {
            // buffer 里没东西了就结束
            break;
        }
    }
}

void TCPSender::remove_ack(const uint64_t ackno) {
    while (!_outstanding.empty()) {
        TCPSegment earliest = _outstanding.front();
        uint64_t absseq = unwrap(earliest.header().seqno, _isn, _acknos);
        if (ackno>=absseq + earliest.length_in_sequence_space()) {
            // 如果这个段已经被完全确认,      起始序号+长度
            _outstanding.pop();
        }else {
            break;
        }
        if (_outstanding.empty()) {
            // 没有需要等待确认的数据，那么表也可以停了
            _alarm.stop();
        }
    }
 }

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t absack = unwrap(ackno, _isn, _acknos);
    // 2. 记录一下当前从对方收到的窗口大小（这代表对方还能接收多少字节）。
    _recx_windowsize = window_size;
    // 3. 如果这个绝对序号比我们目前记录的最大已确认序号更大，说明确实有新的数据得到了对方的确认。
    if (absack > _acknos) {
        // 4. 更新本地记录的最大已确认序号
        _acknos = absack;
        // 5. 移除所有已经被 ack 确认的报文段（在发送端内部的“未确认队列”里清除）
        remove_ack(absack);
        // 重置超时重传时间,没回复就再发一次,和我们微信聊天一样
        _RTO = _initial_retransmission_timeout;
        if (!_outstanding.empty()) {
            _alarm.start(_RTO);
        }
        // 连续重传次数
        _conse_retrans = 0;
    }
    // 更新发送端所“看到”的右边界（_rwindow）
    if (_rwindow < absack + window_size) {
        _rwindow = absack + window_size;
        if (_rwindow > next_abs_seqno()) {
        //如果新的右边界超过了当前的发送序号(next_abs_seqno)，说明又可以装填新的数据了
            fill_window();
        }
    }
 }

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method

void TCPSender::tick(const size_t ms_since_last_tick) {
    _alarm.tick(ms_since_last_tick);
    if (_alarm.isExpired()) {
        // 超时闹钟响了
        TCPSegment segment = _outstanding.front(); // 已发送但还没被确认
        _segments_out.push(segment);
        if (_recx_windowsize > 0) {
            _conse_retrans++; // 重试次数
            _RTO *= 2;  // 指数回退,避免网络不稳定时的过度重试
        }
        _alarm.start(_RTO);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const {
    return _conse_retrans;
}

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_rela_seqno();
    _segments_out.push(segment);
}
