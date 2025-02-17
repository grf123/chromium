// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VR_ELEMENTS_CONTROLLER_H_
#define CHROME_BROWSER_VR_ELEMENTS_CONTROLLER_H_

#include <vector>

#include "base/macros.h"
#include "chrome/browser/vr/elements/ui_element.h"
#include "chrome/browser/vr/renderers/base_renderer.h"

namespace vr {

// This represents a procedurally generated Daydream controller.
class Controller : public UiElement {
 public:
  Controller();
  ~Controller() override;

  void set_local_transform(const gfx::Transform& transform) {
    local_transform_ = transform;
  }

  class Renderer : public BaseRenderer {
   public:
    Renderer();
    ~Renderer() override;

    void Draw(float opacity, const gfx::Transform& view_proj_matrix);

   private:
    GLuint model_view_proj_matrix_handle_ = 0;
    GLuint color_handle_ = 0;
    GLuint opacity_handle_ = 0;
    std::vector<float> vertices_;
    std::vector<GLushort> indices_;
    std::vector<float> colors_;
    GLuint vertex_buffer_ = 0;
    GLuint color_buffer_ = 0;
    GLuint index_buffer_ = 0;

    DISALLOW_COPY_AND_ASSIGN(Renderer);
  };

 private:
  void Render(UiElementRenderer* renderer,
              const CameraModel& model) const final;

  gfx::Transform LocalTransform() const override;

  gfx::Transform local_transform_;

  DISALLOW_COPY_AND_ASSIGN(Controller);
};

}  // namespace vr

#endif  // CHROME_BROWSER_VR_ELEMENTS_CONTROLLER_H_
