#pragma once
namespace locker {
// Không bỏ bản tin đã lấy khỏi hàng đợi nếu tầng truyền trả lỗi.
// Giới hạn mỗi lượt để tác vụ mạng còn xử lý keepalive và lệnh đến.
template <class Message, class Pop, class Send>
void drainOutgoing(Message &pending, bool &hasPending, Pop pop, Send send) {
  for (unsigned i = 0; i < 4; ++i) {
    if (!hasPending) {
      if (!pop(pending))
        return;
      hasPending = true;
    }
    if (!send(pending))
      return;
    hasPending = false;
  }
}
} // namespace locker
