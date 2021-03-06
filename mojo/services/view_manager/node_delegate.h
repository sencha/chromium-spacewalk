// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_VIEW_MANAGER_NODE_DELEGATE_H_
#define MOJO_SERVICES_VIEW_MANAGER_NODE_DELEGATE_H_

#include "mojo/services/view_manager/view_manager_export.h"

namespace gfx {
class Rect;
}

namespace mojo {
namespace service {

class Node;

class MOJO_VIEW_MANAGER_EXPORT NodeDelegate {
 public:
  // Invoked at the end of the Node's destructor (after it has been removed from
  // the hierarchy and its active view has been reset).
  virtual void OnNodeDestroyed(const Node* node) = 0;

  // Invoked when the hierarchy has changed.
  virtual void OnNodeHierarchyChanged(const Node* node,
                                      const Node* new_parent,
                                      const Node* old_parent) = 0;

  virtual void OnNodeBoundsChanged(const Node* node,
                                   const gfx::Rect& old_bounds,
                                   const gfx::Rect& new_bounds) = 0;

 protected:
  virtual ~NodeDelegate() {}
};

}  // namespace service
}  // namespace mojo

#endif  // MOJO_SERVICES_VIEW_MANAGER_NODE_DELEGATE_H_
