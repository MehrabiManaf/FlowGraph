#include "Theme.hpp"
#include "Settings.hpp"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPixmapCache>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>
#include <QSvgRenderer>

namespace fged {

namespace {

Theme g_choice = Theme::Dark;
bool g_dark = true;
int g_generation = 0;                       // changes with every switch: part of the icon cache keys
bool g_followingSystem = false;             // connected to QStyleHints::colorSchemeChanged

QPalette darkPalette() {
    // the look FlowGraph Studio always had
    const QColor darkBg(45, 45, 45), darkerBg(35, 35, 35), lightBg(53, 53, 53), textColor(240, 240, 240), disabledText(120, 120, 120);
    const QColor highlight(255, 127, 0), highlightedText(Qt::black);
    QPalette p;
    p.setColor(QPalette::Window, darkBg);
    p.setColor(QPalette::WindowText, textColor);
    p.setColor(QPalette::Base, darkerBg);
    p.setColor(QPalette::AlternateBase, lightBg);
    p.setColor(QPalette::Text, textColor);
    p.setColor(QPalette::ButtonText, textColor);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::ToolTipText, textColor);
    p.setColor(QPalette::ToolTipBase, darkBg);
    p.setColor(QPalette::Button, lightBg);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Base, darkBg);
    p.setColor(QPalette::Disabled, QPalette::Window, darkBg);
    p.setColor(QPalette::Disabled, QPalette::Button, darkBg);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, highlightedText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, highlight.darker(150));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    p.setColor(QPalette::Link, QColor(42, 130, 218));
    p.setColor(QPalette::LinkVisited, QColor(80, 80, 200));
    p.setColor(QPalette::PlaceholderText, QColor(160, 160, 160));
    p.setColor(QPalette::Light, lightBg.lighter(120));
    p.setColor(QPalette::Midlight, lightBg);
    p.setColor(QPalette::Dark, darkerBg);
    p.setColor(QPalette::Mid, darkBg);
    p.setColor(QPalette::Shadow, Qt::black);
    return p;
}

QPalette lightPalette() {
    const QColor window(236, 236, 236), base(255, 255, 255), alternate(244, 244, 244), button(228, 228, 228);
    const QColor textColor(31, 31, 31), disabledText(160, 160, 160);
    const QColor highlight(255, 127, 0), highlightedText(Qt::black);       // the same orange as the dark theme
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, textColor);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::Text, textColor);
    p.setColor(QPalette::ButtonText, textColor);
    p.setColor(QPalette::BrightText, QColor(200, 0, 0));
    p.setColor(QPalette::ToolTipText, textColor);
    p.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Base, window);
    p.setColor(QPalette::Disabled, QPalette::Window, window);
    p.setColor(QPalette::Disabled, QPalette::Button, window);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, highlightedText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, highlight.lighter(140));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    p.setColor(QPalette::Link, QColor(11, 98, 196));
    p.setColor(QPalette::LinkVisited, QColor(106, 63, 181));
    p.setColor(QPalette::PlaceholderText, QColor(128, 128, 128));
    p.setColor(QPalette::Light, QColor(255, 255, 255));
    p.setColor(QPalette::Midlight, QColor(245, 245, 245));
    p.setColor(QPalette::Dark, QColor(160, 160, 160));
    p.setColor(QPalette::Mid, QColor(200, 200, 200));
    p.setColor(QPalette::Shadow, QColor(120, 120, 120));
    return p;
}

ThemeColors darkColors() {
    ThemeColors c;
    c.dark = true;
    c.error = QColor(255, 123, 123);
    c.warning = QColor(230, 190, 60);
    c.success = QColor(123, 216, 143);
    c.canvas = QColor(35, 35, 35);          // what the graph view always used: Base, grid lines in Window, nodes in Window
    c.gridMinor = QColor(45, 45, 45);
    c.gridMajor = QColor(45, 45, 45);
    c.nodeBody = QColor(45, 45, 45);
    c.constLock = QColor(235, 200, 90);
    return c;
}

ThemeColors lightColors() {
    ThemeColors c;
    c.dark = false;
    c.error = QColor(198, 40, 40);
    c.warning = QColor(154, 103, 0);
    c.success = QColor(46, 125, 50);
    c.canvas = QColor(244, 245, 247);
    c.gridMinor = QColor(230, 232, 236);
    c.gridMajor = QColor(213, 217, 224);
    c.nodeBody = QColor(255, 255, 255);
    c.constLock = QColor(183, 121, 31);
    return c;
}

ThemeColors& current() {
    static ThemeColors c = darkColors();
    return c;
}

bool systemIsDark() {
    if (!qApp) return true;
    const Qt::ColorScheme s = QGuiApplication::styleHints()->colorScheme();
    return s != Qt::ColorScheme::Light;         // Unknown: the editor's own default, dark
}

void show(QApplication& app, bool dark) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    g_dark = dark;
    current() = dark ? darkColors() : lightColors();
    ++g_generation;
    QPixmapCache::clear();                      // icons rendered for the other theme
    app.setPalette(dark ? darkPalette() : lightPalette());
    emit ThemeNotifier::instance().changed();
}

// ---- themed icons
// The icons are made for a dark background: neutral greys (paper, outlines, strokes) are light. On a light background these become
// darker; the colours of the icons (blue, green, red...) and the white on coloured shapes stay.
QByteArray lightVariant(QByteArray svg) {
    struct Swap { const char* from; const char* to; };
    static const Swap swaps[] = {
        // strokes are the drawing itself: dark
        {"stroke=\"#e3e9f2\"", "stroke=\"#44516a\""}, {"stroke=\"#dfe8f7\"", "stroke=\"#44516a\""}, {"stroke=\"#f2f5fa\"", "stroke=\"#44516a\""},
        {"stroke=\"#9fb0c9\"", "stroke=\"#56657d\""},
        // fills are paper and surfaces: a light slate that still stands out from white
        {"fill=\"#e3e9f2\"", "fill=\"#b9c4d4\""}, {"fill=\"#dfe8f7\"", "fill=\"#b9c4d4\""}, {"fill=\"#f2f5fa\"", "fill=\"#c6cfdc\""},
        {"fill=\"#9fb0c9\"", "fill=\"#6b7a90\""},
    };
    for (const Swap& s : swaps) svg.replace(s.from, s.to);
    return svg;
}

const QByteArray& svgSource(const QString& path, bool dark) {
    static QHash<QString, QByteArray> cache;
    const QString key = (dark ? QStringLiteral("d|") : QStringLiteral("l|")) + path;
    auto it = cache.find(key);
    if (it == cache.end()) {
        QFile f(path);
        QByteArray data = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        it = cache.insert(key, dark ? data : lightVariant(data));
    }
    return *it;
}

class ThemedIconEngine : public QIconEngine {
public:
    explicit ThemedIconEngine(QString path) : path_(std::move(path)) {}
    QIconEngine* clone() const override { return new ThemedIconEngine(path_); }
    QString key() const override { return QStringLiteral("fged::ThemedIconEngine"); }
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    bool isNull() override { return svgSource(path_, true).isEmpty(); }

    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
        p->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override { return scaledPixmap(size, mode, state, 1.0); }
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override {
        const QSize px = (QSizeF(size) * scale).toSize();
        if (px.isEmpty()) return QPixmap();
        const QString cacheKey = QStringLiteral("fgicon|%1|%2|%3x%4|%5").arg(path_).arg(g_generation).arg(px.width()).arg(px.height()).arg(int(mode));
        QPixmap pm;
        if (QPixmapCache::find(cacheKey, &pm)) return pm;
        QImage img(px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QSvgRenderer renderer(svgSource(path_, g_dark));
            QPainter painter(&img);
            renderer.render(&painter);
        }
        pm = QPixmap::fromImage(img);
        if (mode == QIcon::Disabled) {                  // like Qt's own SVG icons: only disabled ones look different
            QStyleOption opt;
            opt.palette = QApplication::palette();
            const QPixmap styled = QApplication::style()->generatedIconPixmap(mode, pm, &opt);
            if (!styled.isNull()) pm = styled;
        }
        pm.setDevicePixelRatio(scale);
        QPixmapCache::insert(cacheKey, pm);
        return pm;
    }

private:
    QString path_;
};

} // namespace

const ThemeColors& themeColors() { return current(); }
Theme currentThemeChoice() { return g_choice; }
bool darkThemeShown() { return g_dark; }

QString themeKey(Theme t) {
    switch (t) {
    case Theme::Light: return QStringLiteral("light");
    case Theme::System: return QStringLiteral("system");
    case Theme::Dark: break;
    }
    return QStringLiteral("dark");
}
Theme themeFromKey(const QString& key) {
    if (key == QLatin1String("light")) return Theme::Light;
    if (key == QLatin1String("system")) return Theme::System;
    return Theme::Dark;
}
QString themeTitle(Theme t) {
    switch (t) {
    case Theme::Light: return QStringLiteral("Light");
    case Theme::System: return QStringLiteral("System");
    case Theme::Dark: break;
    }
    return QStringLiteral("Dark");
}

ThemeNotifier& ThemeNotifier::instance() {
    static ThemeNotifier n;
    return n;
}

QIcon themedIcon(const QString& resourcePath) { return QIcon(new ThemedIconEngine(resourcePath)); }

// ---- public API (ScriptEditor.hpp)
void applyTheme(QApplication& app, Theme theme) {
    g_choice = theme;
    if (theme == Theme::System && !g_followingSystem) {
        // the system switches between light and dark (Windows: Settings > Personalization > Colors): follow it while System is chosen
        g_followingSystem = true;
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &ThemeNotifier::instance(), [] {
            if (g_choice == Theme::System && qApp) show(*qApp, systemIsDark());
        });
    }
    show(app, theme == Theme::System ? systemIsDark() : theme == Theme::Dark);
}

void applyDarkTheme(QApplication& app) { applyTheme(app, Theme::Dark); }

Theme savedTheme(const QString& applicationName) {
    return themeFromKey(editorSettings().value(appSettingsKey(applicationName, QStringLiteral("theme"))).toString());
}

void saveTheme(const QString& applicationName, Theme theme) {
    editorSettings().setValue(appSettingsKey(applicationName, QStringLiteral("theme")), themeKey(theme));
}

void applySavedTheme(QApplication& app, const QString& applicationName) { applyTheme(app, savedTheme(applicationName)); }

} // namespace fged
