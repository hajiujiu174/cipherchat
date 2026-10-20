#pragma once
#include <QPlainTextEdit>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <functional>
#include <string>

namespace ccfw::gui {
enum class MessengerPage { Messages, Contacts, Files };
struct ConversationTarget {
    enum class Kind { Peer, Group } kind = Kind::Peer;
    std::string peer;
    std::int64_t group = 0;
    std::string key() const { return kind == Kind::Group ? "group:" + std::to_string(group) : "peer:" + peer; }
    bool valid() const { return kind == Kind::Group ? group > 0 : !peer.empty(); }
};
// Keep the IME composition in the editor. Shortcuts must never consume a
// Return that commits a candidate or a composition that has just ended.
class MessageComposer final : public QPlainTextEdit {
public:
    bool enterSends = true;
    std::function<void()> submit;
protected:
    void inputMethodEvent(QInputMethodEvent* event) override {
        if (composing_ && !event->commitString().isEmpty()) { commitClock_.start(); suppressCommitReturn_ = true; }
        composing_ = !event->preeditString().isEmpty();
        QPlainTextEdit::inputMethodEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
        if (enter && suppressCommitReturn_ && commitClock_.elapsed() < 120 && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) { suppressCommitReturn_ = false; event->accept(); return; }
        if (enter && !composing_ && !(event->modifiers() & Qt::ShiftModifier) &&
            ((event->modifiers() & Qt::ControlModifier) || (enterSends && event->modifiers() == Qt::NoModifier))) {
            if (!event->isAutoRepeat() && submit) submit();
            event->accept(); return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
private:
    bool composing_ = false;
    bool suppressCommitReturn_ = false;
    QElapsedTimer commitClock_;
};
}
