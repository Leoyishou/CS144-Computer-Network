#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity):
unass_base(0), unass_size(0), _eof(0), buffer(capacity, '\0'),
bitmap(capacity, false), _output(capacity), _capacity(capacity) {}

//! \details This functions calls just after pushing a substring into the
//! _output stream. It aims to check if there exists any contiguous substrings
//! recorded earlier can be push into the stream.
void StreamReassembler::check_contiguous() {
    string tmp = "";
    while (bitmap.front()) {   // 如果数据一直有效
        tmp += buffer.front();  // 就从 buffer 导出到 tmp 中
        buffer.pop_front();
        bitmap.pop_front();
        buffer.push_back('\0');
        bitmap.push_back(false);
    }
    if (tmp.length() > 0) {
        cout << "push one contiguous substring with length " << tmp.length() << endl;
        _output.write(tmp);
        unass_base += tmp.length();  // 无序的起点向后移动了
        unass_size -= tmp.length();  // 无序的长度减少了
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (eof) {
        _eof = true;
    }
    size_t len = data.length();
    /*
    假设收到新数据："HELLO" (len=5)，从位置8开始 (offset=2)：
    |<-------------- capacity=10 ------------>|
    +---+---+---+---+---+---+---+---+---+---+
    | A | B | C | D | E | F |   |   | H | E |  新数据位置示意
    +---+---+---+---+---+---+---+---+---+---+
    |<--- buffer_size=6 --->|
                            |offset|
                                   |-可用空间-|
    */
    if (index >= unass_base) {
        // 如果data 的起点在无序起点的后面，则需要计算偏移量
        int offset = index - unass_base;
        size_t real_len = min(len, _capacity-_output.buffer_size()-offset);
        if (real_len < len) {
            _eof = false;
        }
        for (size_t i = 0; i < real_len; i++) {
            if (bitmap[i + offset]) {
                continue;
            }
            buffer[i + offset] = data[i];
            bitmap[i + offset] = true;
            unass_size++;
        }
    /*
    index=3
                ↓
    +---+---+---+---+---+---+---+---+---+---+
    | A | B | C | W | O | R | L | D |   |   |  新数据
    +---+---+---+---+---+---+---+---+---+---+
                |<----- data="WORLD" ----->|
                        ↑
                   unass_base=5
    */
    } else if (index + len > unass_base) {
        // 如果 data 的尾巴和无序的起点重叠，则需要计算重叠的长度
        int offset = unass_base - index;
        // 防止新来的数据超出了 capacity
        size_t real_len = min(len - offset, _capacity - _output.buffer_size());
        if (real_len < len - offset) {
            // 如果数据因为容量限制被截断了，就意味着还有数据没有被处理
            _eof = false;
        }
        for (size_t i = 0; i < real_len; i++) {
            if (bitmap[i]) {
                continue;
            }
            buffer[i] = data[i + offset];
            bitmap[i] = true;
            unass_size++;
        }
    }
    check_contiguous();
    if (_eof && unass_size == 0) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    return unass_size;
}

bool StreamReassembler::empty() const {
    return unass_size == 0;
}
