#include "StateStore.h"
namespace locker {
LoadResult StateStore::load(State &state) {
  // Namespace mới có phiên bản riêng; không xóa/di chuyển dữ liệu firmware trước đó.
  opened_ = preferences_.begin("locker_core_v1", false);
  if (!opened_)
    return LoadResult::Fault;
  if (!preferences_.isKey("state"))
    return LoadResult::Fresh;
  if (preferences_.getBytesLength("state") != sizeof(State) ||
      preferences_.getBytes("state", &state, sizeof(State)) != sizeof(State))
    return LoadResult::Fault;
  return LockerCore::validState(state) ? LoadResult::Loaded : LoadResult::Fault;
}
bool StateStore::save(const State &state) {
  return opened_ && preferences_.putBytes("state", &state, sizeof(state)) == sizeof(state);
}
} // namespace locker
