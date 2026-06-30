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

#include "ui/SmartModelEditor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "fs/FileSystem.h"
#include "fs/PathInfo.h"
#include "fs/PathMatcher.h"
#include "fs/TraversalMode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "ui/MapDocument.h"
#include "ui/ModelPickerView.h"
#include "ui/SearchBox.h"
#include "ui/ViewConstants.h"

#include "kd/result.h"
#include "kd/vector_utils.h"

namespace tb::ui
{

SmartModelEditor::SmartModelEditor(
  AppController& appController,
  MapDocument& document,
  std::vector<std::filesystem::path> roots,
  std::vector<std::filesystem::path> extensions,
  QWidget* parent)
  : SmartPropertyEditor{document, parent}
  , m_roots{std::move(roots)}
  , m_extensions{std::move(extensions)}
{
  createGui(appController);
}

void SmartModelEditor::createGui(AppController& appController)
{
  m_scrollBar = new QScrollBar{Qt::Vertical};
  m_view = new ModelPickerView{appController, m_scrollBar, document(), m_extensions};
  connect(
    m_view, &ModelPickerView::modelSelected, this, &SmartModelEditor::onModelSelected);

  auto* browserPanelSizer = new QHBoxLayout{};
  browserPanelSizer->setContentsMargins(0, 0, 0, 0);
  browserPanelSizer->setSpacing(0);
  browserPanelSizer->addWidget(m_view, 1);
  browserPanelSizer->addWidget(m_scrollBar, 0);

  auto* browserPanel = new QWidget{};
  browserPanel->setLayout(browserPanelSizer);

  m_folderChoice = new QComboBox{};
  m_folderChoice->setToolTip(tr("Model folder (subfolders of models/)"));
  connect(m_folderChoice, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
    m_view->setFolder(m_folderChoice->currentData().toString().toStdString());
  });

  m_extensionChoice = new QComboBox{};
  m_extensionChoice->setToolTip(tr("Filter by model type"));
  m_extensionChoice->addItem(tr("All types"), QString{});
  for (const auto& ext : m_extensions)
  {
    const auto extStr = QString::fromStdString(ext.generic_string());
    m_extensionChoice->addItem(extStr, extStr);
  }
  connect(m_extensionChoice, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
    m_view->setExtensionFilter(
      std::filesystem::path{m_extensionChoice->currentData().toString().toStdString()});
  });

  m_filterBox = createSearchBox();
  connect(m_filterBox, &QLineEdit::textEdited, this, [this]() {
    m_view->setFilterText(m_filterBox->text().toStdString());
  });

  auto* controlSizer = new QHBoxLayout{};
  controlSizer->setContentsMargins(
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin,
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin);
  controlSizer->setSpacing(LayoutConstants::NarrowHMargin);
  controlSizer->addWidget(m_folderChoice, 0);
  controlSizer->addWidget(m_extensionChoice, 0);
  controlSizer->addWidget(m_filterBox, 1);

  auto* outerSizer = new QVBoxLayout{};
  outerSizer->setContentsMargins(0, 0, 0, 0);
  outerSizer->setSpacing(0);
  outerSizer->addWidget(browserPanel, 1);
  outerSizer->addLayout(controlSizer, 0);

  setLayout(outerSizer);

  // Rebuild the folder list whenever a document is (re)loaded, so switching games/maps
  // within one window (single-window mode) repopulates from the new game's filesystem.
  m_notifierConnection +=
    document().documentWasLoadedNotifier.connect([this]() { reloadFolders(); });

  reloadFolders();
}

void SmartModelEditor::reloadFolders()
{
  const auto previous =
    m_folderChoice->count() > 0 ? m_folderChoice->currentData().toString() : QString{};

  {
    const auto blocker = QSignalBlocker{m_folderChoice};
    m_folderChoice->clear();

    // Discover every folder under the roots that actually contains a runtime model: one
    // recursive find per root (enumeration only — never loads a model, so it can't crash),
    // then group the matches by parent directory. Decompile/source-only folders (e.g. a
    // folder of .smd) contain no runtime model and so don't appear.
    const auto& gameFs = document().map().gameFileSystem();
    auto folders = std::vector<std::filesystem::path>{};
    for (const auto& root : m_roots)
    {
      const auto modelPaths =
        gameFs.find(
          root,
          fs::TraversalMode::Recursive,
          fs::makeExtensionPathMatcher(m_extensions))
        | kdl::value_or(std::vector<std::filesystem::path>{});
      for (const auto& modelPath : modelPaths)
      {
        folders.push_back(modelPath.parent_path());
      }
    }
    folders = kdl::vec_sort_and_remove_duplicates(std::move(folders));

    for (const auto& dir : folders)
    {
      const auto label = QString::fromStdString(dir.generic_string());
      m_folderChoice->addItem(label, label);
    }

    if (!previous.isEmpty())
    {
      if (const auto idx = m_folderChoice->findData(previous); idx >= 0)
      {
        m_folderChoice->setCurrentIndex(idx);
      }
    }
  }

  if (m_folderChoice->count() > 0)
  {
    m_view->setFolder(m_folderChoice->currentData().toString().toStdString());
  }
}

void SmartModelEditor::onModelSelected(const std::filesystem::path& path)
{
  setEntityProperty(document().map(), propertyKey(), path.generic_string());
}

void SmartModelEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes)
{
  // The folder list is loaded lazily here too, since the game filesystem may not be ready
  // when the editor is first constructed.
  if (m_folderChoice->count() == 0)
  {
    reloadFolders();
  }

  // Open the picker on the folder of the prop's current model value, if any. If that
  // folder isn't listed (model outside models/, or a renamed folder), fall back to the
  // first folder so the grid shows a valid folder rather than a stale, unrelated one.
  const auto value = mdl::selectPropertyValue(propertyKey(), nodes);
  if (!value.empty() && m_folderChoice->count() > 0)
  {
    const auto folder = std::filesystem::path{value}.parent_path().generic_string();
    auto idx = m_folderChoice->findData(QString::fromStdString(folder));
    if (idx < 0)
    {
      idx = 0;
    }

    const auto blocker = QSignalBlocker{m_folderChoice};
    m_folderChoice->setCurrentIndex(idx);
    m_view->setFolder(m_folderChoice->currentData().toString().toStdString());
  }
}

} // namespace tb::ui
