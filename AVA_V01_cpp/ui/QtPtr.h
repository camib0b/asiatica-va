#pragma once

#include <QObject>

#include <memory>
#include <utility>

struct QtParentDeleter {
  void operator()(QObject* object) const noexcept {
    if (object != nullptr && object->parent() == nullptr) {
      delete object;
    }
  }
};

template<typename Object, typename... Args>
std::unique_ptr<Object, QtParentDeleter> makeQtPtr(Args&&... args) {
  return std::unique_ptr<Object, QtParentDeleter>(
      std::make_unique<Object>(std::forward<Args>(args)...).release());
}

// Destroy a QObject that currently has a Qt parent. Unparent first so Qt is no
// longer the owner, then delete as the unique C++ owner. Do not call this on an
// object still managed by unique_ptr.
inline void unparentAndDelete(QObject* object) {
  if (object == nullptr) {
    return;
  }
  object->setParent(nullptr);
  delete object;
}
