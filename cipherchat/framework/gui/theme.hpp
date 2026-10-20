#pragma once
// CipherChat 界面设计系统：颜色令牌、全局样式表、程序化图标与头像。
// 只包含绘制与样式，不含业务逻辑；所有窗口复用同一份视觉定义。
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
namespace ccfw::gui::theme {
enum class Glyph { Account, Chat, Group, Lock, Attach, Send, Document };
// 在 24x24 设计网格上绘制单色字形，size 为逻辑像素。
QPixmap glyph(Glyph kind, const QColor& color, int size);
QIcon icon(Glyph kind, const QColor& color);
// 联系人头像：稳定取色 + 首字符。size 为逻辑像素。
QPixmap avatar(const QString& name, int size);
// 品牌徽标：主色圆形 + 白色锁形。
QPixmap brand(int size);
// 文件卡图标块：主色圆角方块 + 白色文档字形。
QPixmap fileChip(int size, const QColor& accent = QColor());
// 全局样式表，MainWindow 构造时应用一次。
QString stylesheet();
}
