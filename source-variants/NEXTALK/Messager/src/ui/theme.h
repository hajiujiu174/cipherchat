#pragma once
#include <QColor>
#include <QObject>
namespace Messager {
struct Colors {
    QColor window, surface, rail, subtle, border, text, muted, accent, accentSoft, incoming, outgoing,
        onAccent, success, danger;
};
class Theme final : public QObject {
    Q_OBJECT
  public:
    enum Mode { System = 0, Light = 1, Dark = 2 };
    enum Skin { DefaultSkin = 0, PencilSkin = 1 };
    static Theme& instance();
    const Colors& colors() const {
        return m_colors;
    }
    Mode mode() const {
        return m_mode;
    }
    bool dark() const {
        return m_dark;
    }
    int fontSize() const {
        return m_fontSize;
    }
    void setMode(Mode mode);
    Skin skin() const { return m_skin; }
    bool pencil() const { return m_skin == PencilSkin; }
    void setSkin(Skin skin);
    void setFontSize(int pixels);
    void apply();
  signals:
    void changed();

  private:
    Theme();
    Colors m_colors;
    Mode m_mode = System;
    Skin m_skin = DefaultSkin;
    bool m_dark = false;
    int m_fontSize = 14;
};
} // namespace Messager
