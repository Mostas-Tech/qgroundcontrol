/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


/// @file
///     @author Don Gagne <don@thegagnes.com>

#include "QGCPalette.h"
#include "QGCCorePlugin.h"

#include <QtCore/QDebug>

QList<QGCPalette*>   QGCPalette::_paletteObjects;

QGCPalette::Theme QGCPalette::_theme = QGCPalette::Dark;

QMap<int, QMap<int, QMap<QString, QColor>>> QGCPalette::_colorInfoMap;

QStringList QGCPalette::_colors;

QGCPalette::QGCPalette(QObject* parent) :
    QObject(parent),
    _colorGroupEnabled(true)
{
    if (_colorInfoMap.isEmpty()) {
        _buildMap();
    }

    // We have to keep track of all QGCPalette objects in the system so we can signal theme change to all of them
    _paletteObjects += this;
}

QGCPalette::~QGCPalette()
{
    bool fSuccess = _paletteObjects.removeOne(this);
    if (!fSuccess) {
        qWarning() << "Internal error";
    }
}

void QGCPalette::_buildMap()
{
    // ====================================================================
    // AGRI-PRECISION THEME
    // Designed for high-contrast outdoor visibility in agricultural ops
    // ====================================================================

    // --------------------------------------------------------------------
    //                       Light Mode               Dark Mode
    //                   Disabled    Enabled      Disabled    Enabled
    // --------------------------------------------------------------------
    
    // BACKGROUNDS
    // Light: Clean White | Dark: Deep Slate (Cool Tone)
    DECLARE_QGC_COLOR(window,                   "#f5f6f7", "#f5f6f7", "#1e2228", "#1e2228")
    DECLARE_QGC_COLOR(windowTransparent,        "#ccf5f6f7","#ccf5f6f7","#cc1e2228","#cc1e2228")
    DECLARE_QGC_COLOR(windowShadeLight,         "#e1e4e8", "#d1d5da", "#2d333b", "#373e47")
    DECLARE_QGC_COLOR(windowShade,              "#d1d5da", "#d1d5da", "#16191d", "#16191d")
    DECLARE_QGC_COLOR(windowShadeDark,          "#959da5", "#959da5", "#0d1117", "#0d1117")

    // TEXT
    // High contrast for readability in sunlight
    DECLARE_QGC_COLOR(text,                     "#959da5", "#24292e", "#6a737d", "#e6edf3")
    DECLARE_QGC_COLOR(windowTransparentText,    "#959da5", "#24292e", "#6a737d", "#e6edf3")
    DECLARE_QGC_COLOR(warningText,              "#cc0808", "#d73a49", "#f85761", "#ff7b72")

    // BUTTONS (Standard)
    DECLARE_QGC_COLOR(button,                   "#ffffff", "#ffffff", "#2d333b", "#373e47")
    DECLARE_QGC_COLOR(buttonBorder,             "#e1e4e8", "#d1d5da", "#444c56", "#444c56")
    DECLARE_QGC_COLOR(buttonText,               "#959da5", "#24292e", "#768390", "#adbac7")

    // BUTTON HIGHLIGHTS (The "Brand" Identity)
    // Light: Forest Green | Dark: Neon Mint (High Vis)
    DECLARE_QGC_COLOR(buttonHighlight,          "#e4e4e4", "#2ea44f", "#3a3a3a", "#238636")
    DECLARE_QGC_COLOR(buttonHighlightText,      "#2c2c2c", "#ffffff", "#2c2c2c", "#ffffff")

    // PRIMARY BUTTONS (The "Go" actions)
    // Strong Green for "Action"
    DECLARE_QGC_COLOR(primaryButton,            "#585858", "#2ea44f", "#585858", "#238636")
    DECLARE_QGC_COLOR(primaryButtonText,        "#2c2c2c", "#ffffff", "#2c2c2c", "#ffffff")

    // INPUTS
    DECLARE_QGC_COLOR(textField,                "#ffffff", "#ffffff", "#0d1117", "#0d1117")
    DECLARE_QGC_COLOR(textFieldText,            "#808080", "#000000", "#000000", "#e6edf3")

    // MAP CONTROLS
    DECLARE_QGC_COLOR(mapButton,                "#585858", "#000000", "#585858", "#000000")
    DECLARE_QGC_COLOR(mapButtonHighlight,       "#585858", "#2ea44f", "#585858", "#238636")
    DECLARE_QGC_COLOR(mapIndicator,             "#585858", "#2ea44f", "#585858", "#238636")
    DECLARE_QGC_COLOR(mapIndicatorChild,        "#585858", "#235c35", "#585858", "#235c35")

    // SYSTEM STATUS COLORS (Refined for outdoor visibility)
    DECLARE_QGC_COLOR(colorGreen,               "#008f2d", "#008f2d", "#2ea44f", "#2ea44f") 
    DECLARE_QGC_COLOR(colorYellow,              "#b08800", "#b08800", "#dbab09", "#dbab09")  
    DECLARE_QGC_COLOR(colorYellowGreen,         "#799f26", "#799f26", "#9dbe2f", "#9dbe2f")  
    DECLARE_QGC_COLOR(colorOrange,              "#d06109", "#d06109", "#d46815", "#d46815")  
    DECLARE_QGC_COLOR(colorRed,                 "#cb2431", "#cb2431", "#f85149", "#f85149")
    DECLARE_QGC_COLOR(colorGrey,                "#808080", "#808080", "#6e7681", "#6e7681")
    DECLARE_QGC_COLOR(colorBlue,                "#0366d6", "#0366d6", "#58a6ff", "#58a6ff")

    // ALERTS
    DECLARE_QGC_COLOR(alertBackground,          "#dbab09", "#dbab09", "#d29922", "#d29922")
    DECLARE_QGC_COLOR(alertBorder,              "#808080", "#808080", "#808080", "#808080")
    DECLARE_QGC_COLOR(alertText,                "#000000", "#000000", "#000000", "#000000")

    // MISSION EDITOR
    DECLARE_QGC_COLOR(missionItemEditor,        "#585858", "#dbfef8", "#585858", "#16191d")

    // TOOLBAR
    DECLARE_QGC_COLOR(toolStripHoverColor,      "#585858", "#9D9D9D", "#585858", "#373e47")
    DECLARE_QGC_COLOR(toolStripFGColor,         "#707070", "#585858", "#707070", "#e6edf3")
    DECLARE_QGC_COLOR(statusFailedText,         "#9d9d9d", "#000000", "#707070", "#e6edf3")
    DECLARE_QGC_COLOR(statusPassedText,         "#9d9d9d", "#000000", "#707070", "#e6edf3")
    DECLARE_QGC_COLOR(statusPendingText,        "#9d9d9d", "#000000", "#707070", "#e6edf3")
    DECLARE_QGC_COLOR(toolbarBackground,        "#00ffffff", "#00ffffff", "#001e2228", "#001e2228")
    DECLARE_QGC_COLOR(toolbarDivider,           "#00000000", "#00000000", "#00000000", "#00000000")
    
    DECLARE_QGC_COLOR(groupBorder,              "#bbbbbb", "#bbbbbb", "#444c56", "#444c56")

    // --------------------------------------------------------------------
    // BRANDING COLORS
    // --------------------------------------------------------------------
    // These often appear in the top bar or specific custom widgets
    // We are changing standard Purple/Blue to "Agri-Tech" Green/Blue
    
    DECLARE_QGC_NONTHEMED_COLOR(brandingPurple,     "#238636", "#238636") // Changed to Tech Green
    DECLARE_QGC_NONTHEMED_COLOR(brandingBlue,       "#58a6ff", "#58a6ff") // Sky Blue

    // MAP WIDGET COLORS
    DECLARE_QGC_SINGLE_COLOR(mapWidgetBorderLight,          "#ffffff")
    DECLARE_QGC_SINGLE_COLOR(mapWidgetBorderDark,           "#000000")
    DECLARE_QGC_SINGLE_COLOR(mapMissionTrajectory,          "#d29922") // Gold/Orange for visibility on Sat maps
    DECLARE_QGC_SINGLE_COLOR(surveyPolygonInterior,         "rgba(46, 164, 79, 0.5)") // Translucent Green
    DECLARE_QGC_SINGLE_COLOR(surveyPolygonTerrainCollision, "red")

    // Colors for UTM Adapter
    #ifdef QGC_UTM_ADAPTER
        DECLARE_QGC_COLOR(switchUTMSP,        "#b0e0e6", "#b0e0e6", "#b0e0e6", "#b0e0e6");
        DECLARE_QGC_COLOR(sliderUTMSP,        "#9370db", "#9370db", "#9370db", "#9370db");
        DECLARE_QGC_COLOR(successNotifyUTMSP, "#3cb371", "#3cb371", "#3cb371", "#3cb371");
    #endif
}

void QGCPalette::setColorGroupEnabled(bool enabled)
{
    _colorGroupEnabled = enabled;
    emit paletteChanged();
}

void QGCPalette::setGlobalTheme(Theme newTheme)
{
    // Mobile build does not have themes
    if (_theme != newTheme) {
        _theme = newTheme;
        _signalPaletteChangeToAll();
    }
}

void QGCPalette::_signalPaletteChangeToAll()
{
    // Notify all objects of the new theme
    for (QGCPalette *palette : std::as_const(_paletteObjects)) {
        palette->_signalPaletteChanged();
    }
}

void QGCPalette::_signalPaletteChanged()
{
    emit paletteChanged();
}
