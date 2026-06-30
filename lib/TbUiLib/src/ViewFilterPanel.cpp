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

#include "ui/ViewFilterPanel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Tag.h"
#include "mdl/TagType.h"
#include "ui/MapDocument.h"
#include "ui/QStyleUtils.h"
#include "ui/QWidgetUtils.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{

ViewFilterPanel::ViewFilterPanel(MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
{
  connectObservers();
  createGui();
  refresh();
}

void ViewFilterPanel::connectObservers()
{
  // Rebuild the tag list when a different map loads; re-sync the checkbox states whenever
  // the editor context (hidden tags) or a preference (show flags) changes — including when
  // the toolbar "View Options" popup or a keybind toggles the very same state.
  m_notifierConnection += m_document.documentWasLoadedNotifier.connect(
    this, &ViewFilterPanel::documentWasLoaded);
  m_notifierConnection += m_document.editorContextDidChangeNotifier.connect(
    this, &ViewFilterPanel::editorContextDidChange);

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection +=
    prefs.preferenceDidChangeNotifier.connect(this, &ViewFilterPanel::preferenceDidChange);
}

void ViewFilterPanel::documentWasLoaded()
{
  createGui();
  refresh();
}

void ViewFilterPanel::editorContextDidChange()
{
  refresh();
}

void ViewFilterPanel::preferenceDidChange(const std::filesystem::path&)
{
  refresh();
}

void ViewFilterPanel::createGui()
{
  deleteChildWidgetsLaterAndDeleteLayout(this);
  m_tagCheckBoxes.clear();

  m_showBrushesCheckBox = new QCheckBox{tr("Show brushes")};
  m_showWorldBrushesCheckBox = new QCheckBox{tr("Show world brushes")};
  m_showPointEntitiesCheckBox = new QCheckBox{tr("Show point entities")};

  // "World brushes" = structural geometry only; func_*/trigger_* entity brushes stay visible
  // when this is off. Indent it to read as a refinement of "Show brushes".
  m_showWorldBrushesCheckBox->setToolTip(
    tr("Hide only world/structural brushes; keep func_*/trigger_* entity brushes visible"));

  connect(
    m_showBrushesCheckBox,
    &QAbstractButton::clicked,
    this,
    &ViewFilterPanel::showBrushesChanged);
  connect(
    m_showWorldBrushesCheckBox,
    &QAbstractButton::clicked,
    this,
    &ViewFilterPanel::showWorldBrushesChanged);
  connect(
    m_showPointEntitiesCheckBox,
    &QAbstractButton::clicked,
    this,
    &ViewFilterPanel::showPointEntitiesChanged);

  // Top: the show-flags, kept together as one group that always stays at the top.
  auto* topLayout = new QVBoxLayout{};
  topLayout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::NarrowVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::NarrowVMargin);
  topLayout->setSpacing(0);
  topLayout->addWidget(m_showBrushesCheckBox);

  auto* worldBrushesLayout = new QHBoxLayout{};
  worldBrushesLayout->setContentsMargins(0, 0, 0, 0);
  worldBrushesLayout->addSpacing(LayoutConstants::WideHMargin);
  worldBrushesLayout->addWidget(m_showWorldBrushesCheckBox);
  topLayout->addLayout(worldBrushesLayout);

  topLayout->addWidget(m_showPointEntitiesCheckBox);

  auto* topWidget = new QWidget{};
  topWidget->setLayout(topLayout);

  // Tags go in a scroll area below, so the section can be shrunk freely (the tag list scrolls
  // instead of fighting to show all-or-none).
  auto* tagsWidget = new QWidget{};
  auto* tagsLayout = new QVBoxLayout{};
  tagsLayout->setContentsMargins(
    LayoutConstants::WideHMargin, 0, LayoutConstants::WideHMargin,
    LayoutConstants::NarrowVMargin);
  tagsLayout->setSpacing(0);

  // One "Show <tag>" checkbox per smart tag (Detail, Trigger, Liquid, …), if any.
  if (const auto& tags = m_document.map().smartTags(); !tags.empty())
  {
    auto* tagsLabel = new QLabel{tr("Tags")};
    setInfoStyle(tagsLabel);
    tagsLayout->addWidget(tagsLabel);

    for (const auto& tag : tags)
    {
      const auto label =
        QString::fromLatin1("Show %1").arg(QString::fromStdString(tag.name()).toLower());

      auto* checkBox = new QCheckBox{label};
      const auto tagType = tag.type();
      m_tagCheckBoxes.emplace_back(tagType, checkBox);

      tagsLayout->addWidget(checkBox);
      connect(
        checkBox, &QAbstractButton::clicked, this, [this, tagType](const bool checked) {
          showTagChanged(checked, tagType);
        });
    }
  }
  tagsLayout->addStretch(1);
  tagsWidget->setLayout(tagsLayout);

  auto* scroll = new QScrollArea{};
  scroll->setWidget(tagsWidget);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(topWidget, 0);
  layout->addWidget(scroll, 1);
  setLayout(layout);
}

void ViewFilterPanel::refresh()
{
  m_showBrushesCheckBox->setChecked(pref(Preferences::ShowBrushes));
  m_showPointEntitiesCheckBox->setChecked(pref(Preferences::ShowPointEntities));

  const auto& editorContext = m_document.map().editorContext();
  m_showWorldBrushesCheckBox->setChecked(editorContext.showWorldBrushes());

  const auto hiddenTags = editorContext.hiddenTags();
  for (const auto& [tagType, checkBox] : m_tagCheckBoxes)
  {
    // checked == visible; a tag is visible when its bit is NOT set in hiddenTags
    checkBox->setChecked((tagType & hiddenTags) == 0);
  }
}

void ViewFilterPanel::showBrushesChanged(const bool checked)
{
  setPref(Preferences::ShowBrushes, checked);
}

void ViewFilterPanel::showWorldBrushesChanged(const bool checked)
{
  m_document.map().editorContext().setShowWorldBrushes(checked);
}

void ViewFilterPanel::showPointEntitiesChanged(const bool checked)
{
  setPref(Preferences::ShowPointEntities, checked);
}

void ViewFilterPanel::showTagChanged(const bool checked, const mdl::TagType::Type tagType)
{
  auto& editorContext = m_document.map().editorContext();

  auto hiddenTags = editorContext.hiddenTags();
  if (checked)
  {
    hiddenTags &= ~tagType; // unhide
  }
  else
  {
    hiddenTags |= tagType; // hide
  }
  editorContext.setHiddenTags(hiddenTags);
}

} // namespace tb::ui
