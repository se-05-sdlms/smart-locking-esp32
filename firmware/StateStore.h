#pragma once
#include "LockerCore.h"
#include <Preferences.h>
namespace locker {
class StateStore {
public:
  LoadResult load(State &state);
  bool save(const State &state);

private:
  Preferences preferences_;
  bool opened_ = false;
};
} // namespace locker
