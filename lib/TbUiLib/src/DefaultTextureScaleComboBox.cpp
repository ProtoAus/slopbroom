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

#include "ui/DefaultTextureScaleComboBox.h"

#include <QDoubleValidator>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>

#include "PreferenceManager.h"
#include "Preferences.h"

namespace tb::ui
{

DefaultTextureScaleComboBox::DefaultTextureScaleComboBox(QWidget* parent)
  : QComboBox{parent}
{
  setEditable(true);
  setToolTip(tr(
    "Default texture scale for newly created brushes. "
    "'Game default' uses the scale configured by the current game."));
  addItems({"Game default", "0.0625", "0.125", "0.25", "0.5", "1", "2", "4"});

  // Force the C locale so typed validation uses '.' as the decimal separator, matching the
  // dotted preset strings and the toDouble()/QString::number() parsing/formatting below.
  auto* validator = new QDoubleValidator{0.0, 1.0e6, 6, this};
  validator->setLocale(QLocale::c());
  setValidator(validator);

  // Commit on dropdown selection or when a typed value is finished (Enter / focus-out).
  connect(
    this,
    QOverload<int>::of(&QComboBox::activated),
    this,
    &DefaultTextureScaleComboBox::valueChanged);
  connect(
    lineEdit(),
    &QLineEdit::editingFinished,
    this,
    &DefaultTextureScaleComboBox::valueChanged);

  m_notifierConnection +=
    PreferenceManager::instance().preferenceDidChangeNotifier.connect(
      this, &DefaultTextureScaleComboBox::preferenceDidChange);

  updateFromPreference();
}

void DefaultTextureScaleComboBox::valueChanged()
{
  const auto text = currentText();
  bool ok = false;
  const auto value = text.toDouble(&ok);
  if (ok && value > 0.0)
  {
    setPref(Preferences::DefaultTextureScale, float(value));
  }
  else if (text == "Game default" || (ok && value <= 0.0))
  {
    // "Game default", "0", or any non-positive number all mean "no override".
    setPref(Preferences::DefaultTextureScale, 0.0f);
  }
  // else: empty or non-numeric typed text -> leave the stored value untouched.

  updateFromPreference();
}

void DefaultTextureScaleComboBox::preferenceDidChange(const std::filesystem::path& path)
{
  if (path == Preferences::DefaultTextureScale.path)
  {
    updateFromPreference();
  }
}

void DefaultTextureScaleComboBox::updateFromPreference()
{
  const auto stored = pref(Preferences::DefaultTextureScale);
  const auto blocker = QSignalBlocker{this};
  setCurrentText(
    stored > 0.0f ? QString::number(stored) : QStringLiteral("Game default"));
}

} // namespace tb::ui
