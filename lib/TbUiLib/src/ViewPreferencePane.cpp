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
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/ViewPreferencePane.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>
#include <QtGlobal>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/MiniGl.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/QStyleUtils.h"
#include "ui/SliderWithLabel.h"
#include "ui/ViewConstants.h"

#include "kd/contracts.h"
#include "kd/range_utils.h"

#include "vm/scalar.h"

#include <array>
#include <string>

namespace tb::ui
{
namespace
{
struct FilterMode
{
  int minFilter;
  int magFilter;
  std::string name;
};

// This combo only picks the CLOSE-UP look (the mag filter): Nearest = sharp texels,
// Bilinear = smoothed. Minification is always trilinear so distance stays clean — using
// nearest for minification is what makes distant surfaces read blocky AND shimmery, since
// it point-samples one texel out of a shrinking footprint. Sharpness at distance is then
// tuned with anisotropic filtering and the LOD bias, not by degrading the min filter.
const auto FilterModes = std::array<FilterMode, 2>{
  FilterMode{GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST, "Nearest"},
  FilterMode{GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, "Bilinear"},
};

std::optional<size_t> findFilterMode(const int minFilter, const int magFilter)
{
  // match on the mag filter (the crisp-vs-smooth choice); tolerates any stored min value
  // from an older 6-preset config.
  return kdl::index_of(FilterModes, [&](const FilterMode& filterMode) {
    return filterMode.magFilter == magFilter;
  });
}

constexpr int brightnessToUI(const float value)
{
  return int(vm::round(100.0f * (value - 1.0f)));
}

constexpr float brightnessFromUI(const int value)
{
  return (float(value) / 100.0f) + 1.0f;
}

static_assert(0 == brightnessToUI(brightnessFromUI(0)));

// LOD bias slider: UI -100..100 maps to a bias of -2..+2 mip levels.
constexpr int lodBiasToUI(const float value)
{
  return int(vm::round(50.0f * value));
}

constexpr float lodBiasFromUI(const int value)
{
  return float(value) / 50.0f;
}

static_assert(0 == lodBiasToUI(lodBiasFromUI(0)));

} // namespace

ViewPreferencePane::ViewPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
  bindEvents();
}

void ViewPreferencePane::createGui()
{
  auto* viewPreferences = createViewPreferences();

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->setSpacing(0);
  layout->addSpacing(LayoutConstants::NarrowVMargin);
  layout->addWidget(viewPreferences, 1);
  layout->addSpacing(LayoutConstants::MediumVMargin);

  createScrollableContent(layout);
}

QWidget* ViewPreferencePane::createViewPreferences()
{
  auto* viewBox = new QWidget{this};

  auto* viewPrefsHeader = new QLabel{"Map Views"};
  setEmphasizedStyle(viewPrefsHeader);

  m_themeCombo = new QComboBox{};
  m_themeCombo->addItems({
    QString::fromStdString(Preferences::SystemTheme),
    QString::fromStdString(Preferences::DarkTheme),
  });
  auto* themeInfo = new QLabel{};
  themeInfo->setText(tr("Requires restart after changing"));
  setInfoStyle(themeInfo);
  auto* themeLayout = new QHBoxLayout{};
  themeLayout->addWidget(m_themeCombo);
  themeLayout->addSpacing(LayoutConstants::NarrowHMargin);
  themeLayout->addWidget(themeInfo);
  themeLayout->setContentsMargins(0, 0, 0, 0);

  m_layoutCombo = new QComboBox{};
  m_layoutCombo->setToolTip("Sets the layout of the editing views.");
  m_layoutCombo->addItem("One Pane");
  m_layoutCombo->addItem("Two Panes");
  m_layoutCombo->addItem("Three Panes");
  m_layoutCombo->addItem("Four Panes");

  m_link2dCameras = new QCheckBox{"Sync 2D views"};
  m_link2dCameras->setToolTip("All 2D views pan and zoom together.");

  auto* viewLayoutLayout = new QHBoxLayout{};
  viewLayoutLayout->addWidget(m_layoutCombo);
  viewLayoutLayout->addSpacing(LayoutConstants::NarrowHMargin);
  viewLayoutLayout->addWidget(m_link2dCameras);
  viewLayoutLayout->setContentsMargins(0, 0, 0, 0);

  m_brightnessSlider = new SliderWithLabel{brightnessToUI(0.0f), brightnessToUI(2.0f)};
  m_brightnessSlider->setMaximumWidth(400);
  m_brightnessSlider->setToolTip(
    "Sets the brightness for materials and model skins in the 3D editing view.");
  m_gridAlphaSlider = new SliderWithLabel{0, 100};
  m_gridAlphaSlider->setMaximumWidth(400);
  m_gridAlphaSlider->setToolTip(
    "Sets the visibility of the grid lines in the 3D editing view.");
  m_fovSlider = new SliderWithLabel{50, 150};
  m_fovSlider->setMaximumWidth(400);
  m_fovSlider->setToolTip("Sets the field of vision in the 3D editing view.");

  m_showAxes = new QCheckBox{};
  m_showAxes->setToolTip(
    "Toggle showing the coordinate system axes in the 3D editing view.");

  m_filterModeCombo = new QComboBox{};
  m_filterModeCombo->setToolTip(
    "Texture look in the editing views: Nearest = crisp pixels, Bilinear = smoothed.");
  for (const auto& filterMode : FilterModes)
  {
    m_filterModeCombo->addItem(QString::fromStdString(filterMode.name));
  }

  m_anisotropy = new QCheckBox{};
  m_anisotropy->setToolTip(
    "Anisotropic filtering (16x) sharpens distant and steeply-angled surfaces.");

  m_lodBiasSlider = new SliderWithLabel{lodBiasToUI(-2.0f), lodBiasToUI(2.0f)};
  m_lodBiasSlider->setMaximumWidth(400);
  m_lodBiasSlider->setToolTip(
    "Mipmap LOD bias: how eagerly distant surfaces drop to a blurrier mip level. "
    "Negative keeps a sharper level in use further out (too far negative reintroduces "
    "shimmer), positive blurs sooner. 0 = driver default.");

  m_enableMsaa = new QCheckBox{};
  m_enableMsaa->setToolTip("Enable multisampling");

  m_materialBrowserIconSizeCombo = new QComboBox{};
  m_materialBrowserIconSizeCombo->addItem("25%");
  m_materialBrowserIconSizeCombo->addItem("50%");
  m_materialBrowserIconSizeCombo->addItem("100%");
  m_materialBrowserIconSizeCombo->addItem("150%");
  m_materialBrowserIconSizeCombo->addItem("200%");
  m_materialBrowserIconSizeCombo->addItem("250%");
  m_materialBrowserIconSizeCombo->addItem("300%");
  m_materialBrowserIconSizeCombo->setToolTip(
    "Sets the icon size in the material browser.");

  m_rendererFontSizeCombo = new QComboBox{};
  m_rendererFontSizeCombo->setEditable(true);
  m_rendererFontSizeCombo->setToolTip(
    "Sets the font size for various labels in the editing views.");
  m_rendererFontSizeCombo->addItems({"8",  "9",  "10", "11", "12", "13", "14", "15",
                                     "16", "17", "18", "19", "20", "22", "24", "26",
                                     "28", "32", "36", "40", "48", "56", "64", "72"});
  m_rendererFontSizeCombo->setValidator(new QIntValidator{1, 96});

  m_defaultTextureScaleCombo = new QComboBox{};
  m_defaultTextureScaleCombo->setEditable(true);
  m_defaultTextureScaleCombo->setToolTip(
    "Texture scale for newly created and reset brush faces. "
    "'Game default' uses the scale configured by the current game.");
  m_defaultTextureScaleCombo->addItems(
    {"Game default", "0.0625", "0.125", "0.25", "0.5", "1", "2", "4"});
  // The validator only constrains typed input to positive numbers; the non-numeric
  // "Game default" item is chosen from the list, which bypasses validation. Force the C
  // locale so typed validation uses '.' as the decimal separator, matching the dotted
  // preset strings and the toDouble()/QString::number() parsing/formatting below
  // (otherwise comma-decimal locales would accept "0,125" that toDouble() then drops).
  auto* scaleValidator = new QDoubleValidator{0.0, 1.0e6, 6, m_defaultTextureScaleCombo};
  scaleValidator->setLocale(QLocale::c());
  m_defaultTextureScaleCombo->setValidator(scaleValidator);

  auto* layout = new FormWithSectionsLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setVerticalSpacing(LayoutConstants::WideVMargin);
  // override the default to make the sliders take up maximum width
  layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

  layout->addSection("User Interface");
  layout->addRow("Theme", themeLayout);

  layout->addSection("Map Views");
  layout->addRow("Layout", viewLayoutLayout);
  layout->addRow("Brightness", m_brightnessSlider);
  layout->addRow("Grid", m_gridAlphaSlider);
  layout->addRow("FOV", m_fovSlider);
  layout->addRow("Show axes", m_showAxes);
  layout->addRow("Texture filter", m_filterModeCombo);
  layout->addRow("Anisotropic filtering", m_anisotropy);
  layout->addRow("Mip LOD bias", m_lodBiasSlider);
  layout->addRow("Enable multisampling", m_enableMsaa);

  layout->addSection("Material Browser");
  layout->addRow("Icon size", m_materialBrowserIconSizeCombo);

  layout->addSection("Fonts");
  layout->addRow("Renderer Font Size", m_rendererFontSizeCombo);

  layout->addSection("Brush Defaults");
  layout->addRow("Default texture scale", m_defaultTextureScaleCombo);

  viewBox->setLayout(layout);

  return viewBox;
}

void ViewPreferencePane::bindEvents()
{
  connect(
    m_layoutCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &ViewPreferencePane::layoutChanged);
  connect(
    m_link2dCameras,
    &QCheckBox::checkStateChanged,
    this,
    &ViewPreferencePane::link2dCamerasChanged);
  connect(
    m_brightnessSlider,
    &SliderWithLabel::valueChanged,
    this,
    &ViewPreferencePane::brightnessChanged);
  connect(
    m_gridAlphaSlider,
    &SliderWithLabel::valueChanged,
    this,
    &ViewPreferencePane::gridAlphaChanged);
  connect(
    m_fovSlider, &SliderWithLabel::valueChanged, this, &ViewPreferencePane::fovChanged);
  connect(
    m_showAxes,
    &QCheckBox::checkStateChanged,
    this,
    &ViewPreferencePane::showAxesChanged);
  connect(
    m_anisotropy,
    &QCheckBox::checkStateChanged,
    this,
    &ViewPreferencePane::anisotropyChanged);
  connect(
    m_lodBiasSlider,
    &SliderWithLabel::valueChanged,
    this,
    &ViewPreferencePane::lodBiasChanged);
  connect(
    m_enableMsaa,
    &QCheckBox::checkStateChanged,
    this,
    &ViewPreferencePane::enableMsaaChanged);
  connect(
    m_themeCombo,
    QOverload<int>::of(&QComboBox::activated),
    this,
    &ViewPreferencePane::themeChanged);
  connect(
    m_filterModeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &ViewPreferencePane::filterModeChanged);
  connect(
    m_materialBrowserIconSizeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &ViewPreferencePane::materialBrowserIconSizeChanged);
  connect(
    m_rendererFontSizeCombo,
    &QComboBox::currentTextChanged,
    this,
    &ViewPreferencePane::rendererFontSizeChanged);
  // Commit the scale on dropdown selection or when a typed value is finished (Enter /
  // focus-out) rather than on every keystroke.
  connect(
    m_defaultTextureScaleCombo,
    QOverload<int>::of(&QComboBox::activated),
    this,
    &ViewPreferencePane::defaultTextureScaleChanged);
  connect(
    m_defaultTextureScaleCombo->lineEdit(),
    &QLineEdit::editingFinished,
    this,
    &ViewPreferencePane::defaultTextureScaleChanged);
}

bool ViewPreferencePane::canResetToDefaults()
{
  return true;
}

void ViewPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::MapViewLayout);
  prefs.resetToDefault(Preferences::Link2DCameras);
  prefs.resetToDefault(Preferences::Brightness);
  prefs.resetToDefault(Preferences::GridAlpha);
  prefs.resetToDefault(Preferences::CameraFov);
  prefs.resetToDefault(Preferences::ShowAxes);
  prefs.resetToDefault(Preferences::EnableMSAA);
  prefs.resetToDefault(Preferences::TextureMinFilter);
  prefs.resetToDefault(Preferences::TextureMagFilter);
  prefs.resetToDefault(Preferences::TextureAnisotropy);
  prefs.resetToDefault(Preferences::TextureLodBias);
  prefs.resetToDefault(Preferences::Theme);
  prefs.resetToDefault(Preferences::MaterialBrowserIconSize);
  prefs.resetToDefault(Preferences::RendererFontSize);
  prefs.resetToDefault(Preferences::DefaultTextureScale);
}

void ViewPreferencePane::updateControls()
{
  const auto layoutBlocker = QSignalBlocker{m_layoutCombo};
  const auto link2DCamerasBlocker = QSignalBlocker{m_link2dCameras};
  const auto brightnessBlocker = QSignalBlocker{m_brightnessSlider};
  const auto gridAlphaBlocker = QSignalBlocker{m_gridAlphaSlider};
  const auto fovBlocker = QSignalBlocker{m_fovSlider};

  const auto filterModeBlocker = QSignalBlocker{m_filterModeCombo};

  const auto showAxesBlocker = QSignalBlocker{m_showAxes};
  const auto enableMsaaBlocker = QSignalBlocker{m_enableMsaa};
  const auto themeBlocker = QSignalBlocker{m_themeCombo};

  const auto materialBrowserIconSizeBlocker =
    QSignalBlocker{m_materialBrowserIconSizeCombo};

  const auto rendererFontSizeBlocker = QSignalBlocker{m_rendererFontSizeCombo};
  const auto defaultTextureScaleBlocker = QSignalBlocker{m_defaultTextureScaleCombo};

  auto& prefs = PreferenceManager::instance();

  m_layoutCombo->setCurrentIndex(prefs.getPendingValue(Preferences::MapViewLayout));
  m_link2dCameras->setChecked(prefs.getPendingValue(Preferences::Link2DCameras));
  m_brightnessSlider->setValue(
    brightnessToUI(prefs.getPendingValue(Preferences::Brightness)));
  m_gridAlphaSlider->setRatio(prefs.getPendingValue(Preferences::GridAlpha));
  m_fovSlider->setValue(int(prefs.getPendingValue(Preferences::CameraFov)));

  if (
    const auto filterModeIndex = findFilterMode(
      prefs.getPendingValue(Preferences::TextureMinFilter),
      prefs.getPendingValue(Preferences::TextureMagFilter)))
  {
    m_filterModeCombo->setCurrentIndex(int(*filterModeIndex));
  }

  m_showAxes->setChecked(prefs.getPendingValue(Preferences::ShowAxes));
  m_anisotropy->setChecked(prefs.getPendingValue(Preferences::TextureAnisotropy));
  m_lodBiasSlider->setValue(
    lodBiasToUI(prefs.getPendingValue(Preferences::TextureLodBias)));
  m_enableMsaa->setChecked(prefs.getPendingValue(Preferences::EnableMSAA));
  m_themeCombo->setCurrentIndex(
    findThemeIndex(QString::fromStdString(prefs.getPendingValue(Preferences::Theme))));

  const auto materialBrowserIconSize =
    prefs.getPendingValue(Preferences::MaterialBrowserIconSize);
  if (materialBrowserIconSize == 0.25f)
  {
    m_materialBrowserIconSizeCombo->setCurrentIndex(0);
  }
  else if (materialBrowserIconSize == 0.5f)
  {
    m_materialBrowserIconSizeCombo->setCurrentIndex(1);
  }
  else if (materialBrowserIconSize == 1.5f)
  {
    m_materialBrowserIconSizeCombo->setCurrentIndex(3);
  }
  else if (materialBrowserIconSize == 2.0f)
  {
    m_materialBrowserIconSizeCombo->setCurrentIndex(4);
  }
  else if (materialBrowserIconSize == 2.5f)
  {
    m_materialBrowserIconSizeCombo->setCurrentIndex(5);
  }
  else if (materialBrowserIconSize == 3.0f)
  {
    m_materialBrowserIconSizeCombo->setCurrentIndex(6);
  }
  else
  {
    m_materialBrowserIconSizeCombo->setCurrentIndex(2);
  }

  m_rendererFontSizeCombo->setCurrentText(
    QString::asprintf("%i", prefs.getPendingValue(Preferences::RendererFontSize)));

  const auto defaultTextureScale =
    prefs.getPendingValue(Preferences::DefaultTextureScale);
  m_defaultTextureScaleCombo->setCurrentText(
    defaultTextureScale > 0.0f ? QString::number(defaultTextureScale) : "Game default");
}

bool ViewPreferencePane::validate()
{
  return true;
}

int ViewPreferencePane::findThemeIndex(const QString& theme) const
{
  return m_themeCombo->findText(theme);
}

void ViewPreferencePane::layoutChanged(const int index)
{
  contract_pre(index >= 0 && index < 4);

  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::MapViewLayout, index);
}

void ViewPreferencePane::link2dCamerasChanged(const int state)
{
  const auto value = state == Qt::Checked;
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::Link2DCameras, value);
}

void ViewPreferencePane::brightnessChanged(const int value)
{
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::Brightness, brightnessFromUI(value));
}

void ViewPreferencePane::gridAlphaChanged(const int /* value */)
{
  const auto ratio = m_gridAlphaSlider->ratio();
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::GridAlpha, ratio);
}

void ViewPreferencePane::fovChanged(const int value)
{
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::CameraFov, float(value));
}

void ViewPreferencePane::showAxesChanged(const int state)
{
  const auto value = state == Qt::Checked;
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::ShowAxes, value);
}

void ViewPreferencePane::enableMsaaChanged(const int state)
{
  const auto value = state == Qt::Checked;
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::EnableMSAA, value);
}

void ViewPreferencePane::anisotropyChanged(const int state)
{
  const auto value = state == Qt::Checked;
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::TextureAnisotropy, value);
}

void ViewPreferencePane::lodBiasChanged(const int value)
{
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::TextureLodBias, lodBiasFromUI(value));
}

void ViewPreferencePane::filterModeChanged(const int value)
{
  contract_assert(value < static_cast<int>(FilterModes.size()));

  const auto index = static_cast<size_t>(value);

  const auto minFilter = FilterModes[index].minFilter;
  const auto magFilter = FilterModes[index].magFilter;

  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::TextureMinFilter, minFilter);
  prefs.set(Preferences::TextureMagFilter, magFilter);
}

void ViewPreferencePane::themeChanged(int /*index*/)
{
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::Theme, m_themeCombo->currentText().toStdString());
}

void ViewPreferencePane::materialBrowserIconSizeChanged(const int index)
{
  auto& prefs = PreferenceManager::instance();

  switch (index)
  {
  case 0:
    prefs.set(Preferences::MaterialBrowserIconSize, 0.25f);
    break;
  case 1:
    prefs.set(Preferences::MaterialBrowserIconSize, 0.5f);
    break;
  case 2:
    prefs.set(Preferences::MaterialBrowserIconSize, 1.0f);
    break;
  case 3:
    prefs.set(Preferences::MaterialBrowserIconSize, 1.5f);
    break;
  case 4:
    prefs.set(Preferences::MaterialBrowserIconSize, 2.0f);
    break;
  case 5:
    prefs.set(Preferences::MaterialBrowserIconSize, 2.5f);
    break;
  case 6:
    prefs.set(Preferences::MaterialBrowserIconSize, 3.0f);
    break;
  }
}

void ViewPreferencePane::rendererFontSizeChanged(const QString& str)
{
  bool ok;
  const auto value = str.toInt(&ok);
  if (ok)
  {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::RendererFontSize, value);
  }
}

void ViewPreferencePane::defaultTextureScaleChanged()
{
  auto& prefs = PreferenceManager::instance();

  const auto text = m_defaultTextureScaleCombo->currentText();
  bool ok = false;
  const auto value = text.toDouble(&ok);
  if (ok && value > 0.0)
  {
    prefs.set(Preferences::DefaultTextureScale, float(value));
  }
  else if (text == "Game default" || (ok && value <= 0.0))
  {
    // "Game default", "0", or any non-positive number all mean "no override".
    prefs.set(Preferences::DefaultTextureScale, 0.0f);
  }
  // else: empty or non-numeric typed text -> leave the stored value untouched.

  // Re-sync the field to the stored value so it always shows what is in effect: snaps a
  // typed "0" to "Game default" and normalizes formatting (e.g. ".5" -> "0.5").
  const auto stored = prefs.getPendingValue(Preferences::DefaultTextureScale);
  const auto blocker = QSignalBlocker{m_defaultTextureScaleCombo};
  m_defaultTextureScaleCombo->setCurrentText(
    stored > 0.0f ? QString::number(stored) : "Game default");
}

} // namespace tb::ui
