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
    // 返回桌面主题使用的实例。
    static Theme& instance();
    // 返回桌面主题的生效的配色表。
    const Colors& colors() const {
        return m_colors;
    }
    // 返回桌面主题的当前模式。
    Mode mode() const {
        return m_mode;
    }
    // 判断桌面主题的是否使用深色模式。
    bool dark() const {
        return m_dark;
    }
    // 返回桌面主题使用的字体大小。
    int fontSize() const {
        return m_fontSize;
    }
    // 设置桌面主题使用的模式。
    void setMode(Mode mode);
    // 返回桌面主题使用的皮肤。
    Skin skin() const { return m_skin; }
    // 返回桌面主题使用的铅笔皮肤。
    bool pencil() const { return m_skin == PencilSkin; }
    // 设置桌面主题使用的皮肤。
    void setSkin(Skin skin);
    // 设置桌面主题使用的字体大小。
    void setFontSize(int pixels);
    // 应用桌面主题。
    void apply();
  signals:
    // 通知订阅方组件状态已经变化。
    void changed();

  private:
    // 构造桌面主题，初始化后续操作需要的状态或依赖。
    Theme();
    Colors m_colors;
    Mode m_mode = System;
    Skin m_skin = DefaultSkin;
    bool m_dark = false;
    int m_fontSize = 14;
};
} // namespace Messager
