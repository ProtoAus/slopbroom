/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "NotifierConnection.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/Hit.h"
#include "mdl/HitType.h"
#include "ui/RotateHandle.h"
#include "ui/Tool.h"

#include "vm/mat.h"
#include "vm/polygon.h"
#include "vm/quat.h"
#include "vm/ray.h"
#include "vm/scalar.h"
#include "vm/vec.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tb
{
namespace gl
{
class Camera;
}

namespace mdl
{
class Grid;
class Node;
} // namespace mdl

namespace render
{
class BrushRenderer;
class RenderBatch;
class RenderContext;
} // namespace render

namespace ui
{
class MapDocument;

/**
 * Sweeps the selected brush faces into a run of brushes. A ghost copy of the faces (the
 * destination cap) is dragged into place with a gizmo, and the gap is filled with one
 * brush per segment.
 */
class SweepTool : public Tool
{
public:
  enum class Alignment
  {
    Integer,
    Free,
  };

  enum class PathMode
  {
    Arc,
    Straight,
    SBend,
  };

  static constexpr double MinScaleFactor = 0.01;

  static const mdl::HitType::Type ScaleHitType;

private:
  MapDocument& m_document;
  RotateHandle m_handle;

  vm::vec3d m_translation = vm::vec3d{0, 0, 0};
  vm::quatd m_rotation = vm::quatd{vm::vec3d{0, 0, 1}, 0.0};
  vm::vec3d m_scale = vm::vec3d{1, 1, 1};
  size_t m_segments = 8;
  size_t m_iterations = 1;
  PathMode m_pathMode = PathMode::Arc;
  Alignment m_alignment = Alignment::Integer;

  struct SweepSource
  {
    vm::polygon3d polygon;
    std::string material;
    mdl::Node* parent = nullptr;
  };

  std::vector<SweepSource> m_sources;
  vm::vec3d m_sourceCenter;
  vm::vec3d m_sourceNormal;

  std::map<mdl::Node*, std::vector<mdl::Node*>> m_previewBrushes;
  std::unique_ptr<render::BrushRenderer> m_previewRenderer;

  NotifierConnection m_notifierConnection;

public:
  explicit SweepTool(MapDocument& document);
  ~SweepTool() override;

  bool doActivate() override;
  bool doDeactivate() override;

  const mdl::Grid& grid() const;

  bool applies() const;

  const vm::quatd& rotation() const;
  void setRotation(const vm::quatd& rotation);

  size_t segments() const;
  void setSegments(size_t segments);

  size_t iterations() const;
  void setIterations(size_t iterations);

  PathMode pathMode() const;
  void setPathMode(PathMode pathMode);

  Alignment alignment() const;
  void setAlignment(Alignment alignment);

  vm::vec3d destinationCenter() const;
  void setDestinationCenter(const vm::vec3d& position);

  void reset();

  double majorHandleRadius(const gl::Camera& camera) const;
  double minorHandleRadius(const gl::Camera& camera) const;

  vm::vec3d rotationAxis(RotateHandle::HitArea area) const;

  void updatePreview();
  /** True if the transform should produce brushes, but every segment was degenerate. */
  bool previewIsDegenerate() const;
  void commitSweep();
  void cancelSweep();

  mdl::Hit pick2D(const vm::ray3d& pickRay, const gl::Camera& camera);
  mdl::Hit pick3D(const vm::ray3d& pickRay, const gl::Camera& camera);

  void renderHandle2D(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch);
  void renderHandle3D(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch);
  void renderHighlight2D(
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    RotateHandle::HitArea area);
  void renderHighlight3D(
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    RotateHandle::HitArea area);

  void renderPreview(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch);

  void renderDestinationGhost(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch) const;

  bool hasScaleHandle() const;
  vm::vec3d scaleHandlePosition() const;
  void dragScaleHandleTo(const vm::vec3d& position);
  mdl::Hit pickScaleHandle(const vm::ray3d& pickRay, const gl::Camera& camera) const;
  void renderScaleHandle(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch) const;
  void renderScaleHighlight(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch) const;

private:
  /** Transform applied to the swept profile at station parameter t in [0,1]. */
  vm::mat4x4d stationTransform(double t, const vm::quatd& rotation) const;
  vm::vec3d sBendOffset(double t, const vm::quatd& rotation) const;
  std::optional<vm::vec3d> arcPivot(const vm::quatd& rotation) const;
  vm::quatd effectiveRotation() const;
  vm::vec3d scaleBaseVector() const;
  vm::vec3d scaleArmAtCap() const;
  bool transformIsNoOp() const;

  void clearPreview();
  void addPreviewBrushesToRenderer();
  std::map<mdl::Node*, std::vector<mdl::Node*>> generateSweepBrushes() const;

  void connectObservers();
  void nodesWereRemoved(const std::vector<mdl::Node*>& nodes);

  QWidget* doCreatePage(QWidget* parent) override;
};

} // namespace ui
} // namespace tb
