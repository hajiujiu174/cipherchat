#pragma once
#include "core/types.h"
#include <QPushButton>

class QCheckBox;
class QLabel;
class QLineEdit;
class QMenu;
class QScrollArea;
class QVBoxLayout;

namespace Messager {
class GroupMemberPicker final : public QPushButton {
    Q_OBJECT
  public:
    // 构造群成员选择器，初始化后续操作需要的状态或依赖。
    explicit GroupMemberPicker(QWidget* parent = nullptr);
    // 设置群成员选择器使用的联系人。
    void setContacts(const QVector<Contact>& contacts, const QString& self);
    // 返回群成员选择器当前勾选的账号列表。
    QStringList selectedMembers() const;

  signals:
    // 通知监听方选中项发生变化，供界面或调用方刷新。
    void selectionChanged();

  protected:
    // 在 Qt 的绘制回调中渲染群成员选择器的当前外观。
    void paintEvent(QPaintEvent* event) override;
    // 响应群成员选择器的按键按下，同步界面或输入状态。
    void keyPressEvent(QKeyEvent* event) override;

  private:
    // 更新摘要。
    void updateSummary();
    // 筛选成员。
    void filterMembers();
    QMenu* m_popup;
    QWidget* m_content;
    QLineEdit* m_search;
    QScrollArea* m_scroll;
    QVBoxLayout* m_rows;
    QLabel *m_empty, *m_summary;
    QVector<QCheckBox*> m_checks;
};
} // namespace Messager
