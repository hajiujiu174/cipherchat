#include "scientificcalculator.h"
#include "components.h"
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>

namespace Messager {
namespace {
constexpr double pi = 3.14159265358979323846;
class Expression {
    QString text;
    int pos = 0;
    bool valid = true, degrees;
    double answer;
    double finite(double value) {
        if (!std::isfinite(value)) {
            valid = false;
            return 0;
        }
        return value;
    }
    void spaces() {
        while (pos < text.size() && text[pos].isSpace())
            ++pos;
    }
    bool take(QChar c) {
        spaces();
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    double primary(int depth) {
        if (depth > 64) {
            valid = false;
            return 0;
        }
        if (take('(')) {
            double value = sum(depth + 1);
            if (!take(')'))
                valid = false;
            return value;
        }
        spaces();
        int start = pos;
        if (pos < text.size() && text[pos].isLetter()) {
            while (pos < text.size() && text[pos].isLetter())
                ++pos;
            const auto name = text.mid(start, pos - start).toLower();
            if (name == "pi")
                return pi;
            if (name == "e")
                return std::exp(1.0);
            if (name == "ans")
                return answer;
            if (!take('(')) {
                valid = false;
                return 0;
            }
            double value = sum(depth + 1);
            if (!take(')'))
                valid = false;
            const double angle = degrees ? value * pi / 180.0 : value;
            double result = 0;
            if (name == "sin")
                result = std::sin(angle);
            else if (name == "cos")
                result = std::cos(angle);
            else if (name == "tan") {
                if (std::abs(std::cos(angle)) < 1e-14)
                    valid = false;
                result = std::tan(angle);
            } else if (name == "asin")
                result = std::asin(value) * (degrees ? 180.0 / pi : 1);
            else if (name == "acos")
                result = std::acos(value) * (degrees ? 180.0 / pi : 1);
            else if (name == "atan")
                result = std::atan(value) * (degrees ? 180.0 / pi : 1);
            else if (name == "ln")
                return finite(std::log(value));
            else if (name == "log")
                return finite(std::log10(value));
            else if (name == "sqrt")
                return finite(std::sqrt(value));
            else if (name == "exp")
                return finite(std::exp(value));
            else if (name == "abs")
                return std::abs(value);
            else {
                valid = false;
                return 0;
            }
            if ((name == "sin" || name == "tan") && std::remainder(angle, pi) == 0)
                return 0;
            if (name == "cos" && std::remainder(angle - pi / 2, pi) == 0)
                return 0;
            return finite(result);
        }
        while (pos < text.size() && (text[pos].isDigit() || text[pos] == '.'))
            ++pos;
        if (pos < text.size() && text[pos].toLower() == 'e' && pos > start) {
            ++pos;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
                ++pos;
            int exponentStart = pos;
            while (pos < text.size() && text[pos].isDigit())
                ++pos;
            if (pos == exponentStart)
                valid = false;
        }
        bool ok = false;
        double value = text.mid(start, pos - start).toDouble(&ok);
        valid = valid && ok;
        return value;
    }
    double postfix(int depth) {
        double value = primary(depth);
        while (valid) {
            if (take('%'))
                value /= 100.0;
            else if (take('!')) {
                if (!std::isfinite(value) || value < 0 || value > 170 || std::floor(value) != value) {
                    valid = false;
                    return 0;
                }
                double result = 1;
                for (int n = 2; n <= int(value); ++n)
                    result *= n;
                value = result;
            } else
                break;
        }
        return value;
    }
    double power(int depth) {
        double value = postfix(depth);
        if (take('^'))
            value = finite(std::pow(value, unary(depth + 1)));
        return value;
    }
    double unary(int depth) {
        if (depth > 64) {
            valid = false;
            return 0;
        }
        if (take('+'))
            return unary(depth + 1);
        if (take('-'))
            return -unary(depth + 1);
        return power(depth);
    }
    double product(int depth) {
        double value = unary(depth);
        while (valid) {
            if (take('*'))
                value *= unary(depth);
            else if (take('/')) {
                double divisor = unary(depth);
                if (divisor == 0)
                    valid = false;
                else
                    value /= divisor;
            } else
                break;
        }
        return value;
    }
    double sum(int depth) {
        double value = product(depth);
        while (valid) {
            if (take('+'))
                value += product(depth);
            else if (take('-'))
                value -= product(depth);
            else
                break;
        }
        return value;
    }

  public:
    Expression(QString input, bool deg, double ans) : text(std::move(input)), degrees(deg), answer(ans) {
        text.replace(QChar(0x00d7), '*').replace(QChar(0x00f7), '/').replace(QChar(0x2212), '-');
        text.replace(QChar(0x03c0), "pi");
    }
    bool evaluate(double& value) {
        if (text.size() > 512)
            return false;
        value = sum(0);
        spaces();
        return valid && pos == text.size() && std::isfinite(value);
    }
};
} // namespace

ScientificCalculator::ScientificCalculator(QWidget* parent) : PaperFrame(parent) {
    setObjectName("scientificCalculator");
    setProperty("card", true);
    setMaximumWidth(760);
    setMinimumWidth(600);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 16);
    layout->setSpacing(8);
    auto* modes = new QHBoxLayout;
    modes->addWidget(label("SCIENTIFIC", 12, "muted", true));
    modes->addStretch();
    m_angle = new QComboBox;
    m_angle->setObjectName("calculatorAngleMode");
    m_angle->addItems({"DEG · 角度", "RAD · 弧度"});
    modes->addWidget(m_angle);
    layout->addLayout(modes);
    m_expression = new QLineEdit;
    m_expression->setObjectName("miniAppExpression");
    m_expression->setPlaceholderText("输入算式或点击下方按键");
    m_expression->setMaxLength(512);
    m_expression->setAlignment(Qt::AlignRight);
    layout->addWidget(m_expression);
    m_result = label("0", 30, nullptr, true);
    m_result->setObjectName("miniAppResult");
    m_result->setWordWrap(true);
    m_result->setAlignment(Qt::AlignRight);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_result);
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    layout->addLayout(grid);
    struct Key {
        const char* title;
        const char* token;
    };
    const Key keys[] = {{"sin", "sin("},
                        {"cos", "cos("},
                        {"tan", "tan("},
                        {"ln", "ln("},
                        {"log", "log("},
                        {"√", "sqrt("},
                        {"asin", "asin("},
                        {"acos", "acos("},
                        {"atan", "atan("},
                        {"x²", "^2"},
                        {"xʸ", "^"},
                        {"n!", "!"},
                        {"π", "pi"},
                        {"e", "e"},
                        {"(", "("},
                        {")", ")"},
                        {"%", "%"},
                        {"⌫", "backspace"},
                        {"7", "7"},
                        {"8", "8"},
                        {"9", "9"},
                        {"÷", "/"},
                        {"1/x", "reciprocal"},
                        {"AC", "clear"},
                        {"4", "4"},
                        {"5", "5"},
                        {"6", "6"},
                        {"×", "*"},
                        {"10ˣ", "10^("},
                        {"|x|", "abs("},
                        {"1", "1"},
                        {"2", "2"},
                        {"3", "3"},
                        {"−", "-"},
                        {"exp", "exp("},
                        {"±", "negate"},
                        {"0", "0"},
                        {".", "."},
                        {"Ans", "Ans"},
                        {"+", "+"},
                        {"=", "equals"}};
    for (int i = 0; i < int(sizeof(keys) / sizeof(keys[0])); ++i) {
        const QString token = keys[i].token;
        const bool equals = token == "equals";
        auto* key = button(keys[i].title, equals ? "primary" : "default");
        key->setObjectName(equals ? "miniAppCalculate" : "calculatorKey_" + token);
        key->setAccessibleName(keys[i].title);
        key->setFixedHeight(34);
        key->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        grid->addWidget(key, i / 6, i % 6, 1, equals ? 2 : 1);
        connect(key, &QPushButton::clicked, this, [this, token] {
            if (token == "equals")
                calculate();
            else
                insert(token);
        });
    }
    for (int i = 0; i < 6; ++i)
        grid->setColumnStretch(i, 1);
    connect(m_expression, &QLineEdit::returnPressed, this, &ScientificCalculator::calculate);
    connect(m_angle, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_expression->text().isEmpty())
            calculate();
    });
}
void ScientificCalculator::calculate() {
    double value = 0;
    if (!Expression(m_expression->text(), m_angle->currentIndex() == 0, m_answer).evaluate(value)) {
        m_result->setText("表达式无效，请检查括号、数字或除数。");
        return;
    }
    m_answer = value;
    m_result->setText(QString::number(value, 'g', 15));
}
void ScientificCalculator::insert(const QString& token) {
    if (token == "clear") {
        m_expression->clear();
        m_result->setText("0");
        m_expression->setFocus();
        return;
    }
    if (token == "backspace")
        m_expression->backspace();
    else if (token == "negate" || token == "reciprocal") {
        const auto value =
            m_expression->hasSelectedText() ? m_expression->selectedText() : m_expression->text();
        const auto wrapped =
            (token == "negate" ? "-(" : "1/(") + (value.isEmpty() ? QString("Ans") : value) + ")";
        if (m_expression->hasSelectedText())
            m_expression->insert(wrapped);
        else
            m_expression->setText(wrapped);
    } else {
        const int cursor = m_expression->cursorPosition();
        QString inserted = token;
        if (!m_expression->hasSelectedText() && cursor > 0 && !token.isEmpty()) {
            const QChar previous = m_expression->text()[cursor - 1];
            const bool leftOperand = previous.isDigit() || previous.isLetter() || previous == ')' ||
                                     previous == '!' || previous == '%';
            const bool rightOperand = token[0].isLetter() || token == "(" || token == "10^(";
            if (leftOperand && (rightOperand || ((previous == ')' || previous.isLetter()) &&
                                                 (token[0].isDigit() || token == "."))))
                inserted.prepend('*');
        }
        m_expression->insert(inserted);
    }
    m_expression->setFocus();
}
void ScientificCalculator::reset() {
    m_answer = 0;
    m_expression->clear();
    m_result->setText("0");
}
} // namespace Messager
