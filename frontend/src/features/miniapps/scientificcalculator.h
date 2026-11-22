#pragma once
#include "ui/pencilskin.h"
class QLineEdit;
class QLabel;
class QComboBox;
namespace Messager {
class ScientificCalculator final : public PaperFrame {
    Q_OBJECT
  public:
    // 构造科学计算器，初始化后续操作需要的状态或依赖。
    explicit ScientificCalculator(QWidget* parent = nullptr);
    // 清除当前算式、结果提示以及上次求值状态。
    void reset();
    // 返回科学计算器使用的表达式。
    QLineEdit* expression() const {
        return m_expression;
    }
    // 返回科学计算器使用的结果。
    QLabel* result() const {
        return m_result;
    }

  private:
    // 读取算式和角度模式，显示求值结果并更新可再次使用的上次答案。
    void calculate();
    // 插入科学计算器。
    void insert(const QString& token);
    QLineEdit* m_expression;
    QLabel* m_result;
    QComboBox* m_angle;
    double m_answer = 0;
};
} // namespace Messager
