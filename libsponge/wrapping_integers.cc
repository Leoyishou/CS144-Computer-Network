#include "wrapping_integers.hh"
#include <iostream>
// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;


// wrap：做的事就像“模运算 + 初始偏移量”，把一个大数折叠进小小的环状空间
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    return isn + uint32_t(n);
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
// 为了知道当前真正的数据在整条传输过程中的绝对位置，
// 并且要选取最合理的那一个圈数，尽量贴近我们已有的 checkpoint
//  就像当我们看到时钟显示"9点"，并且知道现在是"下午2点"时，我们更可能认为这
///   "9点"是指同一天的上午，而不是第二天的上午。
// n：表示取模后的“相对几点”——比方说“现在是 3 点”。
// isn：初始参考时间（相当于你最初的“0点”起算时间）。
// checkpoint：最近一次我们确认的绝对时间（比如“我们之前确认过，今天已经是周
// 的 10:00，总计已经过了 58 小时” 之类）——用一个 64 位的数字来表示“绝对时间
// 是第几小时。
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint64_t tmp = 0;
    uint64_t tmp1 = 0;
    if (n - isn < 0) {
    //相对几点(n)”比“初始参考时间(isn)”要小，那么就补一圈
        tmp = uint64_t(n - isn + (1l<<32));
    } else {
        tmp = uint64_t(n - isn);
    }
    // 算出来的 tmp 已经比最近确认过的“绝对小时数”还大，那说明不用再去“加天数”了
    if (tmp >= checkpoint) return tmp;
    // “加天数”,把“天数”信息先对齐到跟 checkpoint 相同的天数区间上去
    // 假设 temp是现在是3点, checkpoint 是第3天的下午2点
    // 1.得到第几天
    //       得到高32位的值 checkpoint >> 32
    //       值放到高32位的位置 << 32
    // 2."现在是3点" + "第3天"
    //       tmp |= 3
    tmp |= ((checkpoint >> 32) << 32);
    while (tmp <= checkpoint) {
        tmp += (1ll << 32);
    }
    // checkpoint 是"第3天下午2点"
    // tmp 是"第4天上午9点"
    // tmp1 = tmp - 一天 = "第3天上午9点"
    tmp1 = tmp - (1ll << 32);
    if (checkpoint - tmp1 < tmp - checkpoint) {
        // tmp1 ----5小时---> checkpoint ----19小时---> tmp
        return tmp1;
    }else {
        // 否则返回天数
        return tmp;
    }
}
