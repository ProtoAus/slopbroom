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

#include <QComboBox>

#include "NotifierConnection.h"

#include <filesystem>

namespace tb::ui
{

/**
 * Editable combo for the `Preferences::DefaultTextureScale` user preference, for inline use
 * in toolbars (e.g. the Draw Shape tool page). Unlike the copy in ViewPreferencePane (which
 * is part of the modal Preferences dialog and edits the *pending* value), this binds to the
 * *committed* preference via pref()/setPref() so an edit takes effect immediately for the
 * next created brush, and it stays two-way in sync with the Preferences dialog through
 * preferenceDidChangeNotifier. A value of 0 (the "Game default" item) means "no override".
 */
class DefaultTextureScaleComboBox : public QComboBox
{
  Q_OBJECT
private:
  NotifierConnection m_notifierConnection;

public:
  explicit DefaultTextureScaleComboBox(QWidget* parent = nullptr);

private:
  void valueChanged();
  void preferenceDidChange(const std::filesystem::path& path);
  void updateFromPreference();
};

} // namespace tb::ui
