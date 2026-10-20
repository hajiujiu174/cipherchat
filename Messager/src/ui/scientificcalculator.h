#pragma once
#include "pencilskin.h"
class QLineEdit;
class QLabel;
class QComboBox;
namespace Messager {
class ScientificCalculator final : public PaperFrame {
    Q_OBJECT
  public:
    explicit ScientificCalculator(QWidget* parent = nullptr);
    void reset();
    QLineEdit* expression() const {
        return m_expression;
    }
    QLabel* result() const {
        return m_result;
    }

  private:
    void calculate();
    void insert(const QString& token);
    QLineEdit* m_expression;
    QLabel* m_result;
    QComboBox* m_angle;
    double m_answer = 0;
};
} // namespace Messager
