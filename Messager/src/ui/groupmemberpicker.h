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
    explicit GroupMemberPicker(QWidget* parent = nullptr);
    void setContacts(const QVector<Contact>& contacts, const QString& self);
    QStringList selectedMembers() const;

  signals:
    void selectionChanged();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    void updateSummary();
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
