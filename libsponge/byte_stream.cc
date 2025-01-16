#include "byte_stream.hh"

#include <algorithm>
// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t cap)
    : buffer(), capacity(cap), end_write(false), end_read(false), written_bytes(0), read_bytes(0) {}

// 让 buffer 入队
size_t ByteStream::write(const string &data) {
    size_t maxCanWrite = capacity - buffer.size();         // 还能装多少水
    size_t realNeedWrite = min(maxCanWrite, data.size());  // 想要倒多少水 和 还能装的水 取最小值
    for (size_t i = 0; i < realNeedWrite; i++) {
        buffer.push_back(data[i]);
    }
    written_bytes += realNeedWrite;  // 最终装了多少水
    return realNeedWrite;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t canPeek = min(len, buffer.size());
    string res = "";
    for (size_t i = 0; i < canPeek; i++) {
        res += buffer[i];
    }
    return res;
}

// 让 buffer 出队
void ByteStream::pop_output(const size_t len) {
    if (len > buffer.size()) {
        set_error();
        return;
    }
    for (size_t i = 0; i < len; i++) {
        buffer.pop_front();
    }
    // read_bytes 是用来跟踪从流中已经读取（弹出）的总字节数的计数器
    read_bytes += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string out = "";
    if (len > buffer.size()) {
        set_error();
        return out;
    }
    for (size_t i = 0; i < len; i++) {
        out += buffer.front();
        buffer.pop_front();
    }
    read_bytes += len;
    return out;
}

void ByteStream::end_input() { end_write = true; }

bool ByteStream::input_ended() const { return end_write; }

size_t ByteStream::buffer_size() const { return buffer.size(); }

bool ByteStream::buffer_empty() const { return buffer.empty(); }

// buffer 中已经倒光,且不会新写入了
bool ByteStream::eof() const { return buffer.empty() && end_write; }

size_t ByteStream::bytes_written() const { return written_bytes; }

size_t ByteStream::bytes_read() const { return read_bytes; }

size_t ByteStream::remaining_capacity() const { return capacity - buffer.size(); }
