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

#include "ui/SweepTool.h"

#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/Camera.h"
#include "gl/MaterialManager.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceAttributes.h"
#include "mdl/BrushNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/Hit.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "render/BrushRenderer.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "ui/MapDocument.h"
#include "ui/SweepToolPage.h"

#include "kd/map_utils.h"
#include "kd/overload.h"
#include "kd/result.h"
#include "kd/vector_utils.h"

#include "vm/bbox.h"
#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/quat.h"
#include "vm/scalar.h"
#include "vm/vec.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

namespace tb::ui
{
namespace
{
vm::vec3d clampScale(const vm::vec3d& factors)
{
  // a factor at zero collapses the profile, a negative one turns it inside-out
  return vm::vec3d{
    std::max(factors.x(), SweepTool::MinScaleFactor),
    std::max(factors.y(), SweepTool::MinScaleFactor),
    std::max(factors.z(), SweepTool::MinScaleFactor)};
}
} // namespace

const mdl::HitType::Type SweepTool::ScaleHitType = mdl::HitType::freeType();

SweepTool::SweepTool(MapDocument& document)
  : Tool{false}
  , m_document{document}
  , m_previewRenderer{std::make_unique<render::BrushRenderer>()}
{
}

SweepTool::~SweepTool()
{
  m_previewRenderer->clear();
  kdl::map_clear_and_delete(m_previewBrushes);
}

bool SweepTool::doActivate()
{
  auto& map = m_document.map();
  const auto& faces = map.selection().brushFaces;
  if (faces.empty())
  {
    return false;
  }

  m_sources.clear();
  auto bounds = std::optional<vm::bbox3d>{};
  auto normalSum = vm::vec3d{0, 0, 0};
  for (const auto& faceHandle : faces)
  {
    const auto& face = faceHandle.face();
    const auto polygon = face.polygon();
    m_sources.push_back(
      {polygon, face.attributes().materialName(), faceHandle.node()->parent()});
    normalSum = normalSum + face.normal();

    for (const auto& v : polygon.vertices())
    {
      bounds = bounds ? vm::merge(*bounds, v) : vm::bbox3d{v, v};
    }
  }
  m_sourceCenter = bounds ? bounds->center() : vm::vec3d{0, 0, 0};
  // cancelling normals leave no forward direction; S-bend falls back to straight
  m_sourceNormal = vm::squared_length(normalSum) > vm::Cd::almost_zero()
                     ? vm::normalize(normalSum)
                     : vm::vec3d{0, 0, 0};

  connectObservers();
  reset();
  return true;
}

bool SweepTool::doDeactivate()
{
  m_notifierConnection.disconnect();
  clearPreview();
  m_sources.clear();
  return true;
}

void SweepTool::connectObservers()
{
  m_notifierConnection += m_document.nodesWereRemovedNotifier.connect(
    [&](const auto& nodes) { nodesWereRemoved(nodes); });
}

void SweepTool::nodesWereRemoved(const std::vector<mdl::Node*>& nodes)
{
  // a source's parent may be deleted while the tool is active; null it so the commit
  // falls back to the default parent
  auto mustRebuild = false;
  for (auto& source : m_sources)
  {
    if (!source.parent)
    {
      // the fallback parent may have been removed too
      mustRebuild = true;
    }
    else if (
      kdl::vec_contains(nodes, source.parent) || source.parent->isDescendantOf(nodes))
    {
      source.parent = nullptr;
      mustRebuild = true;
    }
  }
  if (mustRebuild)
  {
    updatePreview();
  }
}

const mdl::Grid& SweepTool::grid() const
{
  return m_document.map().grid();
}

bool SweepTool::applies() const
{
  return m_document.map().selection().hasBrushFaces();
}

const vm::quatd& SweepTool::rotation() const
{
  return m_rotation;
}

void SweepTool::setRotation(const vm::quatd& rotation)
{
  m_rotation = rotation;
  updatePreview();
}

size_t SweepTool::segments() const
{
  return m_segments;
}

void SweepTool::setSegments(const size_t segments)
{
  m_segments = std::max<size_t>(1, segments);
  updatePreview();
}

size_t SweepTool::iterations() const
{
  return m_iterations;
}

void SweepTool::setIterations(const size_t iterations)
{
  m_iterations = std::max<size_t>(1, iterations);
  updatePreview();
}

SweepTool::PathMode SweepTool::pathMode() const
{
  return m_pathMode;
}

void SweepTool::setPathMode(const PathMode pathMode)
{
  m_pathMode = pathMode;
  updatePreview();
}

SweepTool::Alignment SweepTool::alignment() const
{
  return m_alignment;
}

void SweepTool::setAlignment(const Alignment alignment)
{
  m_alignment = alignment;
  updatePreview();
}

vm::vec3d SweepTool::destinationCenter() const
{
  return m_sourceCenter + m_translation;
}

void SweepTool::setDestinationCenter(const vm::vec3d& position)
{
  m_translation = position - m_sourceCenter;
  m_handle.setPosition(position);
  updatePreview();
  refreshViews();
}

void SweepTool::reset()
{
  m_translation = vm::vec3d{0, 0, 0};
  m_rotation = vm::quatd{vm::vec3d{0, 0, 1}, 0.0};
  m_scale = vm::vec3d{1, 1, 1};
  m_handle.setPosition(m_sourceCenter);
  updatePreview();
  refreshViews();
}

double SweepTool::majorHandleRadius(const gl::Camera& camera) const
{
  return m_handle.majorHandleRadius(camera);
}

double SweepTool::minorHandleRadius(const gl::Camera& camera) const
{
  return m_handle.minorHandleRadius(camera);
}

vm::vec3d SweepTool::rotationAxis(const RotateHandle::HitArea area) const
{
  return m_handle.rotationAxis(area);
}

std::optional<vm::vec3d> SweepTool::arcPivot(const vm::quatd& rotation) const
{
  const auto theta = rotation.angle();
  if (std::abs(theta) < vm::Cd::almost_zero())
  {
    return std::nullopt;
  }

  const auto axis = vm::normalize(rotation.axis());
  const auto c0 = m_sourceCenter;
  const auto c1 = destinationCenter();

  // work in the plane perpendicular to the axis; the rise becomes a helix lift
  const auto rise = vm::dot(c1 - c0, axis);
  const auto u1 = c1 - axis * rise;
  const auto chord = u1 - c0;
  const auto chordLength = vm::length(chord);
  if (chordLength < vm::Cd::almost_zero())
  {
    // no lateral travel, so there is no circle to fit
    return std::nullopt;
  }

  auto inPlanePerp = vm::cross(axis, chord);
  const auto perpLength = vm::length(inPlanePerp);
  if (perpLength < vm::Cd::almost_zero())
  {
    return std::nullopt;
  }
  inPlanePerp = inPlanePerp / perpLength;

  const auto mid = (c0 + u1) * 0.5;
  const auto distanceMidToCenter = (chordLength * 0.5) / std::tan(theta * 0.5);

  // pick the candidate center whose rotation by theta maps c0 onto u1
  const auto turn = vm::quatd{axis, theta};
  auto bestPivot = mid;
  auto bestError = std::numeric_limits<double>::max();
  for (const auto sign : {1.0, -1.0})
  {
    const auto pivot = mid + inPlanePerp * (distanceMidToCenter * sign);
    const auto mapped = pivot + turn * (c0 - pivot);
    if (const auto error = vm::squared_length(mapped - u1); error < bestError)
    {
      bestError = error;
      bestPivot = pivot;
    }
  }
  return bestPivot;
}

vm::mat4x4d SweepTool::stationTransform(const double t, const vm::quatd& rotationFull) const
{
  const auto c0 = m_sourceCenter;
  const auto angle = rotationFull.angle();
  const auto hasRotation = std::abs(angle) > vm::Cd::almost_zero();
  // axis() is degenerate at ~0 rotation, so pick a safe default
  const auto axis = hasRotation ? vm::normalize(rotationFull.axis()) : vm::vec3d{0, 0, 1};
  const auto rotation = vm::quatd{axis, angle * t};
  const auto factors = vm::vec3d{1, 1, 1} + (m_scale - vm::vec3d{1, 1, 1}) * t;

  if (m_pathMode == PathMode::Arc)
  {
    if (const auto pivot = arcPivot(rotationFull))
    {
      const auto rise = vm::dot(m_translation, axis);
      auto m = vm::translation_matrix(-c0);
      m = vm::scaling_matrix(factors) * m;
      m = vm::translation_matrix(c0) * m;                    // scale about c0
      m = vm::translation_matrix(-*pivot) * m;
      m = vm::rotation_matrix(rotation) * m;                 // revolve about the pivot
      m = vm::translation_matrix(*pivot + axis * (rise * t)) * m; // helix lift
      return m;
    }
    // no usable pivot, fall through to the straight path
  }

  const auto sBend =
    m_pathMode == PathMode::SBend && !vm::is_zero(m_sourceNormal, vm::Cd::almost_zero());
  auto m = vm::translation_matrix(-c0);
  m = vm::scaling_matrix(factors) * m;
  m = vm::rotation_matrix(rotation) * m;
  m = vm::translation_matrix(c0) * m;
  m = vm::translation_matrix(sBend ? sBendOffset(t, rotationFull) : m_translation * t) * m;
  return m;
}

vm::vec3d SweepTool::sBendOffset(const double t, const vm::quatd& rotation) const
{
  // cubic Hermite with end tangents along the source normal and the rotated cap normal
  const auto chordLength = vm::length(m_translation);
  const auto startTangent = m_sourceNormal * chordLength;
  const auto endTangent = (rotation * m_sourceNormal) * chordLength;

  const auto t2 = t * t;
  const auto t3 = t2 * t;
  return startTangent * (t3 - 2.0 * t2 + t) + m_translation * (-2.0 * t3 + 3.0 * t2)
         + endTangent * (t3 - t2);
}

vm::quatd SweepTool::effectiveRotation() const
{
  // quat::angle() runs 0..2pi, so a ring dragged 330 degrees would sweep all 330; above a
  // half-turn, use the smaller turn about the opposite axis instead
  const auto theta = m_rotation.angle();
  if (theta <= vm::Cd::pi() + vm::Cd::almost_zero())
  {
    return m_rotation;
  }
  const auto shortAngle = vm::Cd::two_pi() - theta;
  if (shortAngle < vm::Cd::almost_zero())
  {
    // a full turn is the identity, and axis() is degenerate here
    return vm::quatd{vm::vec3d{0, 0, 1}, 0.0};
  }
  return vm::quatd{-vm::normalize(m_rotation.axis()), shortAngle};
}

bool SweepTool::transformIsNoOp() const
{
  return vm::is_zero(m_translation, vm::Cd::almost_zero())
         && effectiveRotation().angle() < vm::Cd::almost_zero()
         && vm::is_equal(m_scale, vm::vec3d{1, 1, 1}, vm::Cd::almost_zero());
}

std::map<mdl::Node*, std::vector<mdl::Node*>> SweepTool::generateSweepBrushes() const
{
  auto& map = m_document.map();

  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  const auto snapToInteger = m_alignment == Alignment::Integer;
  const auto N = m_segments;
  const auto rotation = effectiveRotation();

  // iteration r continues from the previous cap, i.e. applies the cap transform r times
  auto powers = std::vector<vm::mat4x4d>{};
  powers.reserve(m_iterations);
  const auto capTransform = stationTransform(1.0, rotation);
  auto power = vm::mat4x4d::identity();
  for (size_t r = 0; r < m_iterations; ++r)
  {
    powers.push_back(power);
    power = power * capTransform;
  }

  // a station vertex depends only on (r, s), so adjacent segments compute their shared
  // cap vertices identically and the mesh stays watertight even when snapping
  const auto station = [&](const vm::vec3d& v, const size_t r, const size_t s) {
    const auto t = double(s) / double(N);
    const auto p = powers[r] * stationTransform(t, rotation) * v;
    // station 0 of the first iteration is the source face itself and must stay exact
    return snapToInteger && (s > 0 || r > 0) ? vm::round(p) : p;
  };

  // each source face produces its own run of brushes, grouped under its original parent
  auto result = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
  for (const auto& source : m_sources)
  {
    // resolve the material now so the preview renders textured
    auto* material = map.materialManager().material(source.material);

    // fall back to the default insertion parent if the captured parent has been deleted
    auto* parent = source.parent ? source.parent : parentForNodes(map);

    const auto& sourceVertices = source.polygon.vertices();
    for (size_t r = 0; r < m_iterations; ++r)
    {
      for (size_t i = 0; i < N; ++i)
      {
        auto points = std::vector<vm::vec3d>{};
        points.reserve(sourceVertices.size() * 2);
        for (const auto& v : sourceVertices)
        {
          points.push_back(station(v, r, i));
        }
        for (const auto& v : sourceVertices)
        {
          points.push_back(station(v, r, i + 1));
        }

        builder.createBrush(points, source.material)
          | kdl::transform([&](auto brush) {
              auto* brushNode = new mdl::BrushNode{std::move(brush)};
              for (size_t faceIndex = 0; faceIndex < brushNode->brush().faceCount();
                   ++faceIndex)
              {
                brushNode->setFaceMaterial(faceIndex, material);
              }
              result[parent].push_back(brushNode);
            })
          | kdl::transform_error([&](auto e) {
              // a degenerate segment cannot form a brush; skip it
              map.logger().debug() << "Sweep: could not create segment brush: " << e.msg;
            });
      }
    }
  }

  return result;
}

void SweepTool::updatePreview()
{
  // the renderer must be cleared before the preview nodes are deleted
  m_previewRenderer->clear();
  kdl::map_clear_and_delete(m_previewBrushes);

  if (!m_sources.empty() && m_segments > 0 && !transformIsNoOp())
  {
    m_previewBrushes = generateSweepBrushes();
    addPreviewBrushesToRenderer();
  }

  refreshViews();
}

bool SweepTool::previewIsDegenerate() const
{
  return !m_sources.empty() && m_segments > 0 && !transformIsNoOp()
         && m_previewBrushes.empty();
}

void SweepTool::clearPreview()
{
  m_previewRenderer->clear();
  kdl::map_clear_and_delete(m_previewBrushes);
  refreshViews();
}

void SweepTool::addPreviewBrushesToRenderer()
{
  for (const auto& [parent, nodes] : m_previewBrushes)
  {
    for (auto* node : nodes)
    {
      node->accept(kdl::overload(
        [](const mdl::WorldNode&) {},
        [](const mdl::LayerNode&) {},
        [](const mdl::GroupNode&) {},
        [](const mdl::EntityNode&) {},
        [&](mdl::BrushNode& brushNode) { m_previewRenderer->addBrush(brushNode); },
        [](mdl::PatchNode&) {}));
    }
  }
}

void SweepTool::commitSweep()
{
  if (m_previewBrushes.empty())
  {
    return;
  }

  auto& map = m_document.map();
  auto transaction = mdl::Transaction{map, "Sweep"};
  const auto addedNodes = addNodes(map, m_previewBrushes);
  // ownership of the preview brushes has passed to the document
  m_previewBrushes.clear();
  m_previewRenderer->clear();
  deselectAll(map);
  selectNodes(map, addedNodes);
  transaction.commit();

  refreshViews();
}

void SweepTool::cancelSweep()
{
  clearPreview();
}

mdl::Hit SweepTool::pick2D(const vm::ray3d& pickRay, const gl::Camera& camera)
{
  return m_handle.pick2D(pickRay, camera);
}

mdl::Hit SweepTool::pick3D(const vm::ray3d& pickRay, const gl::Camera& camera)
{
  return m_handle.pick3D(pickRay, camera);
}

void SweepTool::renderHandle2D(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch)
{
  m_handle.renderHandle2D(renderContext, renderBatch);
}

void SweepTool::renderHandle3D(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch)
{
  m_handle.renderHandle3D(renderContext, renderBatch);
}

void SweepTool::renderHighlight2D(
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch,
  const RotateHandle::HitArea area)
{
  m_handle.renderHighlight2D(renderContext, renderBatch, area);
}

void SweepTool::renderHighlight3D(
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch,
  const RotateHandle::HitArea area)
{
  m_handle.renderHighlight3D(renderContext, renderBatch, area);
}

void SweepTool::renderPreview(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch)
{
  // styled like the Clip tool's kept-brush preview
  m_previewRenderer->setFaceColor(pref(Preferences::FaceColor));
  m_previewRenderer->setEdgeColor(pref(Preferences::SelectedEdgeColor));
  m_previewRenderer->setShowEdges(true);
  m_previewRenderer->setShowOccludedEdges(true);
  m_previewRenderer->setOccludedEdgeColor(RgbaF{
    pref(Preferences::SelectedEdgeColor).to<RgbF>(),
    pref(Preferences::OccludedSelectedEdgeAlpha)});
  m_previewRenderer->setTint(true);
  m_previewRenderer->setTintColor(pref(Preferences::SelectedFaceColor));
  m_previewRenderer->render(renderContext, renderBatch);
}

void SweepTool::renderDestinationGhost(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  if (m_sources.empty())
  {
    return;
  }

  const auto capTransform = stationTransform(1.0, effectiveRotation());

  auto renderService = render::RenderService{renderContext, renderBatch};
  renderService.setLineWidth(2.0f);

  const auto renderCaps = [&](const vm::mat4x4d& transform) {
    for (const auto& source : m_sources)
    {
      const auto& vertices = source.polygon.vertices();
      if (vertices.size() < 2)
      {
        continue;
      }
      auto loop = std::vector<vm::vec3f>{};
      loop.reserve(vertices.size() + 1);
      for (const auto& v : vertices)
      {
        loop.push_back(vm::vec3f{transform * v});
      }
      loop.push_back(loop.front());
      renderService.renderLineStrip(loop);
    }
  };

  // later iterations' caps are drawn as fainter echoes
  auto transform = capTransform;
  for (size_t r = 0; r < m_iterations; ++r)
  {
    renderService.setForegroundColor(
      r == 0 ? pref(Preferences::HandleColor)
             : RgbaF{pref(Preferences::HandleColor).to<RgbF>(), 0.35f});
    renderCaps(transform);
    transform = transform * capTransform;
  }
}

vm::vec3d SweepTool::scaleBaseVector() const
{
  auto best = vm::vec3d{0, 0, 0};
  auto bestLengthSquared = 0.0;
  for (const auto& source : m_sources)
  {
    for (const auto& v : source.polygon.vertices())
    {
      const auto arm = v - m_sourceCenter;
      if (const auto lengthSquared = vm::squared_length(arm);
          lengthSquared > bestLengthSquared)
      {
        bestLengthSquared = lengthSquared;
        best = arm;
      }
    }
  }
  return best;
}

vm::vec3d SweepTool::scaleArmAtCap() const
{
  return effectiveRotation() * scaleBaseVector();
}

bool SweepTool::hasScaleHandle() const
{
  return !m_sources.empty()
         && vm::squared_length(scaleBaseVector()) > vm::Cd::almost_zero();
}

vm::vec3d SweepTool::scaleHandlePosition() const
{
  const auto scaledArm = effectiveRotation() * (m_scale * scaleBaseVector());
  return destinationCenter() + scaledArm;
}

void SweepTool::dragScaleHandleTo(const vm::vec3d& position)
{
  const auto arm = scaleArmAtCap();
  const auto armLengthSquared = vm::squared_length(arm);
  if (armLengthSquared < vm::Cd::almost_zero())
  {
    return;
  }

  // project the dragged position onto the arm to read off a uniform factor
  const auto factor =
    vm::dot(position - destinationCenter(), arm) / armLengthSquared;
  m_scale = clampScale(vm::vec3d{factor, factor, factor});
  updatePreview();
  refreshViews();
}

mdl::Hit SweepTool::pickScaleHandle(
  const vm::ray3d& pickRay, const gl::Camera& camera) const
{
  if (!hasScaleHandle())
  {
    return mdl::Hit::NoHit;
  }
  const auto position = scaleHandlePosition();
  if (const auto distance = camera.pickPointHandle(
        pickRay, position, double(pref(Preferences::HandleRadius))))
  {
    return {ScaleHitType, *distance, vm::point_at_distance(pickRay, *distance), 0};
  }
  return mdl::Hit::NoHit;
}

void SweepTool::renderScaleHandle(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  if (!hasScaleHandle())
  {
    return;
  }
  const auto center = destinationCenter();
  const auto position = scaleHandlePosition();

  auto renderService = render::RenderService{renderContext, renderBatch};
  // green like the Scale tool's handles
  renderService.setForegroundColor(pref(Preferences::ScaleHandleColor));
  renderService.renderLine(vm::vec3f{center}, vm::vec3f{position});
  renderService.renderHandle(vm::vec3f{position});
}

void SweepTool::renderScaleHighlight(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  if (!hasScaleHandle())
  {
    return;
  }
  auto renderService = render::RenderService{renderContext, renderBatch};
  renderService.setForegroundColor(pref(Preferences::SelectedHandleColor));
  renderService.renderHandleHighlight(vm::vec3f{scaleHandlePosition()});
}

QWidget* SweepTool::doCreatePage(QWidget* parent)
{
  return new SweepToolPage{m_document, *this, parent};
}

} // namespace tb::ui
