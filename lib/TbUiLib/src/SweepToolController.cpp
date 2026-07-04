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

#include "ui/SweepToolController.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/ActiveShader.h"
#include "gl/GlInterface.h"
#include "gl/Shaders.h"
#include "mdl/Grid.h"
#include "mdl/Hit.h"
#include "mdl/HitFilter.h"
#include "render/Circle.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "render/Renderable.h"
#include "ui/HandleDragTracker.h"
#include "ui/InputState.h"
#include "ui/MoveHandleDragTracker.h"
#include "ui/RotateHandle.h"
#include "ui/SweepTool.h"
#include "ui/ToolController.h"

#include "vm/intersection.h"
#include "vm/quat.h"
#include "vm/util.h"
#include "vm/vec.h"

#include <functional>
#include <memory>
#include <sstream>

namespace tb::ui
{
namespace
{

class AngleIndicatorRenderer : public render::DirectRenderable
{
private:
  vm::vec3d m_position;
  render::Circle m_circle;

public:
  AngleIndicatorRenderer(
    const vm::vec3d& position,
    const float radius,
    const vm::axis::type axis,
    const vm::vec3d& startAxis,
    const vm::vec3d& endAxis)
    : m_position{position}
    , m_circle{radius, 24, true, axis, vm::vec3f{startAxis}, vm::vec3f{endAxis}}
  {
  }

  void prepare(gl::Gl& gl, gl::VboManager& vboManager) override
  {
    m_circle.prepare(gl, vboManager);
  }

  void render(render::RenderContext& renderContext) override
  {
    auto& gl = renderContext.gl();

    gl.disable(GL_DEPTH_TEST);

    gl.pushAttrib(GL_POLYGON_BIT);
    gl.disable(GL_CULL_FACE);
    gl.polygonMode(GL_FRONT_AND_BACK, GL_FILL);

    auto translation = render::MultiplyModelMatrix{
      renderContext.transformation(), vm::translation_matrix(vm::vec3f{m_position})};
    auto shader = gl::ActiveShader{
      gl, renderContext.shaderManager(), gl::Shaders::VaryingPUniformCShader};
    shader.set("Color", RgbaF{1.0f, 1.0f, 1.0f, 0.2f});
    m_circle.render(gl, shader.program());

    gl.enable(GL_DEPTH_TEST);
    gl.popAttrib();
  }
};

using RenderHighlight = std::function<void(
  const InputState&,
  render::RenderContext&,
  render::RenderBatch&,
  RotateHandle::HitArea)>;

class SweepDragDelegate : public HandleDragTrackerDelegate
{
private:
  SweepTool& m_tool;
  RotateHandle::HitArea m_area;
  RenderHighlight m_renderHighlight;
  vm::quatd m_initialRotation;
  double m_angle = 0.0;

public:
  SweepDragDelegate(
    SweepTool& tool, const RotateHandle::HitArea area, RenderHighlight renderHighlight)
    : m_tool{tool}
    , m_area{area}
    , m_renderHighlight{std::move(renderHighlight)}
    , m_initialRotation{tool.rotation()}
  {
  }

  HandlePositionProposer start(
    const InputState& inputState,
    const vm::vec3d& /* initialHandlePosition */,
    const vm::vec3d& handleOffset) override
  {
    const auto center = m_tool.destinationCenter();
    const auto axis = m_tool.rotationAxis(m_area);
    const auto radius = m_tool.majorHandleRadius(inputState.camera());

    return makeHandlePositionProposer(
      makeCircleHandlePicker(center, axis, radius, handleOffset),
      makeCircleHandleSnapper(
        m_tool.grid(), m_tool.grid().angle(), center, axis, radius));
  }

  DragStatus update(
    const InputState&,
    const DragState& dragState,
    const vm::vec3d& proposedHandlePosition) override
  {
    const auto center = m_tool.destinationCenter();
    const auto axis = m_tool.rotationAxis(m_area);
    const auto ref = vm::normalize(dragState.initialHandlePosition - center);
    const auto vec = vm::normalize(proposedHandlePosition - center);
    m_angle = vm::measure_angle(vec, ref, axis);
    // compose this drag's spin onto the rotation from the drag start
    m_tool.setRotation(vm::quatd{vm::normalize(axis), m_angle} * m_initialRotation);

    return DragStatus::Continue;
  }

  void end(const InputState&, const DragState&) override {}

  void cancel(const DragState&) override { m_tool.setRotation(m_initialRotation); }

  void setRenderOptions(
    const InputState&, render::RenderContext& renderContext) const override
  {
    renderContext.setForceShowSelectionGuide();
  }

  void render(
    const InputState& inputState,
    const DragState& dragState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override
  {
    m_renderHighlight(inputState, renderContext, renderBatch, m_area);
    renderAngleIndicator(renderContext, renderBatch, dragState.initialHandlePosition);
    renderAngleText(renderContext, renderBatch);
  }

private:
  void renderAngleIndicator(
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    const vm::vec3d& initialHandlePosition) const
  {
    if (const auto handleRadius =
          static_cast<float>(m_tool.majorHandleRadius(renderContext.camera()));
        handleRadius > 0.0f)
    {
      const auto center = m_tool.destinationCenter();
      const auto axis = m_tool.rotationAxis(m_area);
      const auto startAxis = vm::normalize(initialHandlePosition - center);
      const auto endAxis = vm::quatd{axis, m_angle} * startAxis;

      renderBatch.addOneShot(new AngleIndicatorRenderer{
        center, handleRadius, vm::find_abs_max_component(axis), startAxis, endAxis});
    }
  }

  void renderAngleText(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
  {
    const auto center = m_tool.destinationCenter();

    auto renderService = render::RenderService{renderContext, renderBatch};

    renderService.setForegroundColor(pref(Preferences::SelectedInfoOverlayTextColor));
    renderService.setBackgroundColor(
      pref(Preferences::SelectedInfoOverlayBackgroundColor));
    // show the shortest equivalent angle, e.g. -5 instead of 355, to match the sweep
    const auto degrees = vm::to_degrees(m_angle);
    renderService.renderString(
      angleString(degrees > 180.0 ? degrees - 360.0 : degrees), vm::vec3f{center});
  }

  std::string angleString(const double angle) const
  {
    auto str = std::stringstream{};
    str.precision(2);
    str.setf(std::ios::fixed);
    str << angle;
    return str.str();
  }
};

class SweepPartBase : public ToolController
{
protected:
  SweepTool& m_tool;

protected:
  explicit SweepPartBase(SweepTool& tool)
    : m_tool(tool)
  {
  }

private:
  Tool& tool() override { return m_tool; }

  const Tool& tool() const override { return m_tool; }

  bool mouseClick(const InputState& inputState) override
  {
    using namespace mdl::HitFilters;

    if (!inputState.mouseButtonsPressed(MouseButtons::Left))
    {
      return false;
    }

    const auto& hit = inputState.pickResult().first(type(RotateHandle::HandleHitType));
    if (!hit.isMatch())
    {
      return false;
    }

    const auto area = hit.target<RotateHandle::HitArea>();
    if (area == RotateHandle::HitArea::Center)
    {
      return false;
    }

    // consume clicks on a rotate ring so they don't fall through to the selection tools
    return true;
  }

  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override
  {
    using namespace mdl::HitFilters;

    if (
      inputState.mouseButtons() != MouseButtons::Left
      || inputState.modifierKeys() != ModifierKeys::None)
    {
      return nullptr;
    }

    const auto& hit = inputState.pickResult().first(type(RotateHandle::HandleHitType));
    if (!hit.isMatch())
    {
      return nullptr;
    }

    const auto area = hit.target<RotateHandle::HitArea>();
    if (area == RotateHandle::HitArea::Center)
    {
      return nullptr;
    }

    // We cannot use the hit's hitpoint because it is on the surface of the handle torus,
    // whereas our drag snapper expects it to be on the plane defined by the sweep handle
    // center and the sweep axis.
    const auto center = m_tool.destinationCenter();
    const auto axis = m_tool.rotationAxis(area);

    if (
      const auto distance =
        vm::intersect_ray_plane(inputState.pickRay(), vm::plane3d{center, axis}))
    {
      const auto initialHandlePosition =
        vm::point_at_distance(inputState.pickRay(), *distance);
      auto renderHighlight = [this](
                               const auto& inputState_,
                               auto& renderContext,
                               auto& renderBatch,
                               const auto area_) {
        doRenderHighlight(inputState_, renderContext, renderBatch, area_);
      };

      return createHandleDragTracker(
        SweepDragDelegate{m_tool, area, std::move(renderHighlight)},
        inputState,
        initialHandlePosition,
        initialHandlePosition);
    }

    return nullptr;
  }

  void render(
    const InputState& inputState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) override
  {
    using namespace mdl::HitFilters;

    if (!inputState.anyToolDragging())
    {
      const auto& hit = inputState.pickResult().first(type(RotateHandle::HandleHitType));
      if (hit.isMatch())
      {
        const auto area = hit.target<RotateHandle::HitArea>();
        if (area != RotateHandle::HitArea::Center)
        {
          doRenderHighlight(
            inputState, renderContext, renderBatch, hit.target<RotateHandle::HitArea>());
        }
      }
    }
  }

  bool cancel() override { return false; }

private:
  virtual void doRenderHighlight(
    const InputState& inputState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    RotateHandle::HitArea area) = 0;
};

class MoveSweepCenterDragDelegate : public MoveHandleDragTrackerDelegate
{
private:
  SweepTool& m_tool;
  RenderHighlight m_renderHighlight;

public:
  MoveSweepCenterDragDelegate(SweepTool& tool, RenderHighlight renderHighlight)
    : m_tool{tool}
    , m_renderHighlight{std::move(renderHighlight)}
  {
  }

  DragStatus move(
    const InputState&, const DragState&, const vm::vec3d& currentHandlePosition) override
  {
    m_tool.setDestinationCenter(currentHandlePosition);
    return DragStatus::Continue;
  }

  void end(const InputState&, const DragState&) override {}

  void cancel(const DragState& dragState) override
  {
    m_tool.setDestinationCenter(dragState.initialHandlePosition);
  }

  void render(
    const InputState& inputState,
    const DragState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override
  {
    m_renderHighlight(
      inputState, renderContext, renderBatch, RotateHandle::HitArea::Center);
  }

  DragHandleSnapper makeDragHandleSnapper(
    const InputState&, const SnapMode snapMode) const override
  {
    return makeDragHandleSnapperFromSnapMode(m_tool.grid(), snapMode);
  }
};

class MoveCenterBase : public ToolController
{
protected:
  SweepTool& m_tool;

protected:
  explicit MoveCenterBase(SweepTool& tool)
    : m_tool{tool}
  {
  }

  Tool& tool() override { return m_tool; }

  const Tool& tool() const override { return m_tool; }

  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override
  {
    using namespace mdl::HitFilters;

    if (
      !inputState.mouseButtonsPressed(MouseButtons::Left)
      || !inputState.checkModifierKeys(
        ModifierKeyPressed::No, ModifierKeyPressed::DontCare, ModifierKeyPressed::No))
    {
      return nullptr;
    }

    const auto& hit = inputState.pickResult().first(type(RotateHandle::HandleHitType));
    if (!hit.isMatch())
    {
      return nullptr;
    }

    if (hit.target<RotateHandle::HitArea>() != RotateHandle::HitArea::Center)
    {
      return nullptr;
    }

    auto renderHighlight = [this](
                             const auto& inputState_,
                             auto& renderContext,
                             auto& renderBatch,
                             const auto area_) {
      doRenderHighlight(inputState_, renderContext, renderBatch, area_);
    };

    return createMoveHandleDragTracker(
      MoveSweepCenterDragDelegate{m_tool, std::move(renderHighlight)},
      inputState,
      m_tool.destinationCenter(),
      hit.hitPoint());
  }

  void render(
    const InputState& inputState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) override
  {
    using namespace mdl::HitFilters;

    if (!inputState.anyToolDragging())
    {
      const auto& hit = inputState.pickResult().first(type(RotateHandle::HandleHitType));
      if (
        hit.isMatch()
        && hit.target<RotateHandle::HitArea>() == RotateHandle::HitArea::Center)
      {
        doRenderHighlight(
          inputState, renderContext, renderBatch, RotateHandle::HitArea::Center);
      }
    }
  }

  bool cancel() override { return false; }

private:
  virtual void doRenderHighlight(
    const InputState& inputState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    RotateHandle::HitArea area) = 0;
};

class MoveCenterPart2D : public MoveCenterBase
{
public:
  explicit MoveCenterPart2D(SweepTool& tool)
    : MoveCenterBase{tool}
  {
  }

private:
  void doRenderHighlight(
    const InputState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    RotateHandle::HitArea area) override
  {
    m_tool.renderHighlight2D(renderContext, renderBatch, area);
  }
};

class SweepPart2D : public SweepPartBase
{
public:
  explicit SweepPart2D(SweepTool& tool)
    : SweepPartBase{tool}
  {
  }

private:
  void doRenderHighlight(
    const InputState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    RotateHandle::HitArea area) override
  {
    m_tool.renderHighlight2D(renderContext, renderBatch, area);
  }
};

class MoveCenterPart3D : public MoveCenterBase
{
public:
  explicit MoveCenterPart3D(SweepTool& tool)
    : MoveCenterBase{tool}
  {
  }

private:
  void doRenderHighlight(
    const InputState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    RotateHandle::HitArea area) override
  {
    m_tool.renderHighlight3D(renderContext, renderBatch, area);
  }
};

class SweepPart3D : public SweepPartBase
{
public:
  explicit SweepPart3D(SweepTool& tool)
    : SweepPartBase{tool}
  {
  }

private:
  void doRenderHighlight(
    const InputState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    RotateHandle::HitArea area) override
  {
    m_tool.renderHighlight3D(renderContext, renderBatch, area);
  }
};

class PointHandleDragDelegate : public MoveHandleDragTrackerDelegate
{
public:
  using ApplyFn = std::function<void(const vm::vec3d&)>;
  using RenderHighlightFn =
    std::function<void(render::RenderContext&, render::RenderBatch&)>;

private:
  SweepTool& m_tool;
  ApplyFn m_apply;
  RenderHighlightFn m_renderHighlight;
  vm::vec3d m_initialPosition;

public:
  PointHandleDragDelegate(
    SweepTool& tool,
    ApplyFn apply,
    RenderHighlightFn renderHighlight,
    const vm::vec3d& initialPosition)
    : m_tool{tool}
    , m_apply{std::move(apply)}
    , m_renderHighlight{std::move(renderHighlight)}
    , m_initialPosition{initialPosition}
  {
  }

  DragStatus move(
    const InputState&,
    const DragState&,
    const vm::vec3d& proposedHandlePosition) override
  {
    m_apply(proposedHandlePosition);
    return DragStatus::Continue;
  }

  void end(const InputState&, const DragState&) override {}

  void cancel(const DragState&) override { m_apply(m_initialPosition); }

  DragHandleSnapper makeDragHandleSnapper(
    const InputState&, const SnapMode snapMode) const override
  {
    return makeDragHandleSnapperFromSnapMode(m_tool.grid(), snapMode);
  }

  void render(
    const InputState&,
    const DragState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override
  {
    m_renderHighlight(renderContext, renderBatch);
  }
};

// one part serves both the 2D and 3D controllers since the handle is a sphere
class PointHandlePart : public ToolController
{
public:
  using PositionFn = std::function<vm::vec3d()>;
  using ApplyFn = std::function<void(const vm::vec3d&)>;
  using RenderHighlightFn =
    std::function<void(render::RenderContext&, render::RenderBatch&)>;

private:
  SweepTool& m_tool;
  mdl::HitType::Type m_hitType;
  PositionFn m_position;
  ApplyFn m_apply;
  RenderHighlightFn m_renderHighlight;

public:
  PointHandlePart(
    SweepTool& tool,
    const mdl::HitType::Type hitType,
    PositionFn position,
    ApplyFn apply,
    RenderHighlightFn renderHighlight)
    : m_tool{tool}
    , m_hitType{hitType}
    , m_position{std::move(position)}
    , m_apply{std::move(apply)}
    , m_renderHighlight{std::move(renderHighlight)}
  {
  }

private:
  Tool& tool() override { return m_tool; }

  const Tool& tool() const override { return m_tool; }

  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override
  {
    using namespace mdl::HitFilters;

    if (
      inputState.mouseButtons() != MouseButtons::Left
      || inputState.modifierKeys() != ModifierKeys::None)
    {
      return nullptr;
    }

    const auto& hit = inputState.pickResult().first(type(m_hitType));
    if (!hit.isMatch())
    {
      return nullptr;
    }

    const auto position = m_position();
    return createMoveHandleDragTracker(
      PointHandleDragDelegate{m_tool, m_apply, m_renderHighlight, position},
      inputState,
      position,
      hit.hitPoint());
  }

  void render(
    const InputState& inputState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) override
  {
    using namespace mdl::HitFilters;

    if (
      !inputState.anyToolDragging()
      && inputState.pickResult().first(type(m_hitType)).isMatch())
    {
      m_renderHighlight(renderContext, renderBatch);
    }
  }

  bool cancel() override { return false; }
};

std::unique_ptr<PointHandlePart> makeScaleHandlePart(SweepTool& tool)
{
  return std::make_unique<PointHandlePart>(
    tool,
    SweepTool::ScaleHitType,
    [&tool]() { return tool.scaleHandlePosition(); },
    [&tool](const vm::vec3d& position) { tool.dragScaleHandleTo(position); },
    [&tool](render::RenderContext& renderContext, render::RenderBatch& renderBatch) {
      tool.renderScaleHighlight(renderContext, renderBatch);
    });
}

} // namespace

SweepToolController::SweepToolController(SweepTool& tool)
  : m_tool{tool}
{
}

SweepToolController::~SweepToolController() = default;

Tool& SweepToolController::tool()
{
  return m_tool;
}

const Tool& SweepToolController::tool() const
{
  return m_tool;
}

void SweepToolController::pick(const InputState& inputState, mdl::PickResult& pickResult)
{
  if (const auto hit = doPick(inputState); hit.isMatch())
  {
    pickResult.addHit(hit);
  }
  // the scale handle is view independent, so it is picked here in the shared base
  if (
    const auto hit = m_tool.pickScaleHandle(inputState.pickRay(), inputState.camera());
    hit.isMatch())
  {
    pickResult.addHit(hit);
  }
}

void SweepToolController::setRenderOptions(
  const InputState& inputState, render::RenderContext& renderContext) const
{
  using namespace mdl::HitFilters;
  if (inputState.pickResult().first(type(RotateHandle::HandleHitType)).isMatch())
  {
    renderContext.setForceShowSelectionGuide();
  }
}

void SweepToolController::render(
  const InputState& inputState,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  m_tool.renderPreview(renderContext, renderBatch);
  doRenderHandle(renderContext, renderBatch);
  m_tool.renderDestinationGhost(renderContext, renderBatch);
  m_tool.renderScaleHandle(renderContext, renderBatch);
  ToolControllerGroup::render(inputState, renderContext, renderBatch);
}

bool SweepToolController::cancel()
{
  return false;
}

SweepToolController2D::SweepToolController2D(SweepTool& tool)
  : SweepToolController{tool}
{
  addController(std::make_unique<MoveCenterPart2D>(tool));
  addController(makeScaleHandlePart(tool));
  addController(std::make_unique<SweepPart2D>(tool));
}

mdl::Hit SweepToolController2D::doPick(const InputState& inputState)
{
  return m_tool.pick2D(inputState.pickRay(), inputState.camera());
}

void SweepToolController2D::doRenderHandle(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch)
{
  m_tool.renderHandle2D(renderContext, renderBatch);
}

SweepToolController3D::SweepToolController3D(SweepTool& tool)
  : SweepToolController{tool}
{
  addController(std::make_unique<MoveCenterPart3D>(tool));
  addController(makeScaleHandlePart(tool));
  addController(std::make_unique<SweepPart3D>(tool));
}

mdl::Hit SweepToolController3D::doPick(const InputState& inputState)
{
  return m_tool.pick3D(inputState.pickRay(), inputState.camera());
}

void SweepToolController3D::doRenderHandle(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch)
{
  m_tool.renderHandle3D(renderContext, renderBatch);
}

} // namespace tb::ui
