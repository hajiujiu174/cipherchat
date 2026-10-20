#include "components.h"
#include "models/conversationmodel.h"
#include "services/localfiles.h"
#include "services/gomokuwire.h"
#include <QApplication>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QStyle>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QVBoxLayout>

namespace Messager {
QIcon applicationIcon() { return QIcon(":/resources/app.png"); }
QIcon glyph(const QString& name, const QColor& color, int size) {
    if(name=="brand")return applicationIcon();
    QPixmap px(size * 2, size * 2);
    px.setDevicePixelRatio(2);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24., size / 24.);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    if (name == "chat") {
        path.moveTo(6, 4);
        path.lineTo(18, 4);
        path.quadTo(21, 4, 21, 7);
        path.lineTo(21, 15);
        path.quadTo(21, 18, 18, 18);
        path.lineTo(10, 18);
        path.lineTo(5, 21);
        path.lineTo(5, 18);
        path.quadTo(3, 18, 3, 15);
        path.lineTo(3, 7);
        path.quadTo(3, 4, 6, 4);
        p.drawPath(path);
        p.drawLine(7, 9, 17, 9);
        p.drawLine(7, 13, 14, 13);
    } else if (name == "users") {
        p.drawEllipse(QRectF(8, 3, 8, 8));
        path.moveTo(4, 21);
        path.cubicTo(4, 12, 20, 12, 20, 21);
        p.drawPath(path);
        p.drawArc(QRectF(1, 4, 8, 8), 80 * 16, 150 * 16);
        p.drawArc(QRectF(15, 4, 8, 8), -50 * 16, 150 * 16);
    } else if (name == "apps") {
        for (int x : {3, 14}) for (int y : {3, 14}) p.drawRoundedRect(QRectF(x, y, 7, 7), 2, 2);
    } else if (name == "moments") {
        p.drawEllipse(QRectF(8,8,8,8));
        for(int k=0;k<8;++k){
            p.save();p.translate(12,12);p.rotate(k*45);
            p.drawArc(QRectF(-9,-9,18,18),8*16,29*16);
            p.drawLine(QPointF(5.8,1.1),QPointF(8.7,1.6));p.restore();
        }
    } else if (name == "file") {
        path.moveTo(6, 2);
        path.lineTo(14, 2);
        path.lineTo(20, 8);
        path.lineTo(20, 21);
        path.lineTo(5, 21);
        path.lineTo(5, 2);
        p.drawPath(path);
        p.drawLine(14, 2, 14, 8);
        p.drawLine(14, 8, 20, 8);
        p.drawLine(9, 13, 16, 13);
        p.drawLine(9, 17, 14, 17);
    } else if (name == "bell") {
        path.moveTo(4, 17);
        path.lineTo(6, 14);
        path.lineTo(6, 9);
        path.cubicTo(6, 1, 18, 1, 18, 9);
        path.lineTo(18, 14);
        path.lineTo(20, 17);
        path.closeSubpath();
        p.drawPath(path);
        p.drawArc(QRectF(9, 17, 6, 5), 180 * 16, 180 * 16);
    } else if (name == "settings") {
        p.drawEllipse(QRectF(4, 4, 16, 16));
        p.drawEllipse(QRectF(9, 9, 6, 6));
        for (int k = 0; k < 8; ++k) {
            p.save();
            p.translate(12, 12);
            p.rotate(k * 45);
            p.drawLine(0, 8, 0, 11);
            p.restore();
        }
    } else if (name == "search") {
        p.drawEllipse(QRectF(4, 3, 12, 12));
        p.drawLine(14, 14, 21, 21);
    } else if (name == "send") {
        path.moveTo(3, 4);
        path.lineTo(21, 12);
        path.lineTo(3, 20);
        path.lineTo(6, 12);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(6, 12, 15, 12);
    } else if (name == "attach") {
        path.moveTo(8, 15);
        path.lineTo(15, 8);
        path.cubicTo(19, 4, 22, 8, 18, 12);
        path.lineTo(10, 20);
        path.cubicTo(4, 26, -1, 18, 5, 12);
        path.lineTo(14, 3);
        p.drawPath(path);
    } else if (name == "info") {
        p.drawEllipse(QRectF(3, 3, 18, 18));
        p.drawPoint(12, 7);
        p.drawLine(12, 11, 12, 17);
    } else if (name == "plus") {
        p.drawLine(12, 5, 12, 19);
        p.drawLine(5, 12, 19, 12);
    } else if (name == "close") {
        p.drawLine(6, 6, 18, 18);
        p.drawLine(18, 6, 6, 18);
    } else if (name == "smile") {
        p.drawEllipse(QRectF(3, 3, 18, 18));
        p.drawPoint(8, 9);
        p.drawPoint(16, 9);
        p.drawArc(QRectF(7, 9, 10, 9), 200 * 16, 140 * 16);
    } else if (name == "download") {
        p.drawLine(12, 3, 12, 15);
        p.drawLine(7, 11, 12, 16);
        p.drawLine(17, 11, 12, 16);
        path.moveTo(4, 17);
        path.lineTo(4, 21);
        path.lineTo(20, 21);
        path.lineTo(20, 17);
        p.drawPath(path);
    } else if (name == "spark") {
        path.moveTo(12, 2);
        path.lineTo(15, 9);
        path.lineTo(22, 12);
        path.lineTo(15, 15);
        path.lineTo(12, 22);
        path.lineTo(9, 15);
        path.lineTo(2, 12);
        path.lineTo(9, 9);
        path.closeSubpath();
        p.drawPath(path);
    } else if (name == "check") {
        path.moveTo(5, 12);
        path.lineTo(10, 17);
        path.lineTo(20, 6);
        p.drawPath(path);
    } else if (name == "arrow") {
        p.drawLine(5, 12, 20, 12);
        p.drawLine(15, 7, 20, 12);
        p.drawLine(15, 17, 20, 12);
    }
    return QIcon(px);
}
QLabel* label(const QString& text, int pixels, const char* role, bool bold) {
    auto* l = new QLabel(text);
    l->setTextFormat(Qt::PlainText);
    if (role)
        l->setProperty("role", role);
    if (pixels || bold) {
        QFont f = l->font();
        if (pixels)
            f.setPixelSize(pixels);
        if (bold)
            f.setWeight(QFont::DemiBold);
        l->setFont(f);
    }
    QObject::connect(&Theme::instance(),&Theme::changed,l,[l] {
        auto font=l->font();font.setFamilies(qApp->font().families());l->setFont(font);
    });
    return l;
}
QPushButton* button(const QString& text, const char* variant, const QString& icon) {
    auto* b = new PencilButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("variant", variant);
    if (!icon.isEmpty()) {
        auto refresh = [b, icon] { b->setIcon(glyph(icon, Theme::instance().colors().accent)); };
        refresh();
        QObject::connect(&Theme::instance(), &Theme::changed, b, refresh);
    }
    return b;
}
void paintAvatar(QPainter& p, const QRectF& r, const QString& initials, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, color.lighter(120));
    g.setColorAt(1, color);
    p.setBrush(g);
    p.drawRoundedRect(r, r.width() * .36, r.height() * .36);
    p.setPen(Qt::white);
    QFont f = qApp->font();
    f.setPixelSize(int(r.width() * .38));
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.drawText(r, Qt::AlignCenter, initials);
    p.restore();
}
Avatar::Avatar(QString s, QColor c, int size, QWidget* p) : QWidget(p), m_initials(s), m_color(c) {
    setFixedSize(size, size);
    setAccessibleName(s + "的头像");
}
void Avatar::setImage(const QByteArray& png) {
    m_image={};if(!png.isEmpty()&&png.size()<=288*1024){QImage image;if(image.loadFromData(png,"PNG")&&image.width()<=256&&image.height()<=256)m_image=QPixmap::fromImage(image);}update();
}
void Avatar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if(m_image.isNull())paintAvatar(p,rect(),m_initials,m_color);
    else {p.setRenderHint(QPainter::Antialiasing);QPainterPath clip;clip.addEllipse(rect());p.setClipPath(clip);p.drawPixmap(rect(),m_image);}
    if(Theme::instance().pencil())paintPencilAvatar(p,rect());
}
IconButton::IconButton(const QString& name, const QString& tooltip, QWidget* parent) : QPushButton(parent) {
    setFixedSize(36, 36);
    setIconSize(QSize(21, 21));
    setToolTip(tooltip);
    setAccessibleName(tooltip);
    setCursor(Qt::PointingHandCursor);
    setProperty("variant", "ghost");
    auto refresh = [this, name] { setIcon(glyph(name, Theme::instance().colors().muted)); };
    refresh();
    connect(&Theme::instance(), &Theme::changed, this, refresh);
}
QSize ConversationDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {280, 86};
}
void ConversationDelegate::paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const auto& c = Theme::instance().colors();
    QRect r = o.rect.adjusted(8, 3, -8, -3);
    bool selected = o.state & QStyle::State_Selected;
    p->setPen(Qt::NoPen);
    p->setBrush(selected ? c.accentSoft : (o.state & QStyle::State_MouseOver ? c.subtle : c.surface));
    p->drawRoundedRect(r, 12, 12);
    const QRectF avatarRect(r.left()+12,r.top()+17,44,44);
    const auto image=i.data(ConversationModel::AvatarImage).value<QPixmap>();
    if(image.isNull())paintAvatar(*p,avatarRect,i.data(ConversationModel::Initials).toString(),i.data(ConversationModel::Color).value<QColor>());
    else {p->save();QPainterPath clip;clip.addEllipse(avatarRect);p->setClipPath(clip);p->drawPixmap(avatarRect.toRect(),image);p->restore();}
    if(Theme::instance().pencil())paintPencilAvatar(*p,avatarRect);
    int left = r.left() + 68, right = r.right() - 12;
    QFont f = qApp->font();
    f.setPixelSize(14);
    f.setWeight(QFont::DemiBold);
    p->setFont(f);
    p->setPen(c.text);
    p->drawText(QRect(left, r.top() + 17, qMax(10, right - left - 50), 23), Qt::AlignVCenter,
                p->fontMetrics().elidedText(i.data(ConversationModel::Name).toString(), Qt::ElideRight,
                                            qMax(10, right - left - 50)));
    f.setPixelSize(11);
    f.setWeight(QFont::Normal);
    p->setFont(f);
    p->setPen(c.muted);
    p->drawText(QRect(right - 50, r.top() + 20, 50, 20), Qt::AlignRight,
                i.data(ConversationModel::Time).toString());
    int unread = i.data(ConversationModel::Unread).toInt();
    f.setPixelSize(12);
    p->setFont(f);
    p->drawText(QRect(left, r.top() + 45, qMax(10, right - left - (unread ? 29 : 0)), 21), Qt::AlignVCenter,
                p->fontMetrics().elidedText(i.data(ConversationModel::Preview).toString(), Qt::ElideRight,
                                            qMax(10, right - left - (unread ? 29 : 0))));
    if (unread) {
        QRect badge(right - 21, r.top() + 45, 21, 21);
        p->setBrush(c.accent);
        p->setPen(Qt::NoPen);
        p->drawEllipse(badge);
        p->setPen(c.onAccent);
        f.setPixelSize(10);
        f.setBold(true);
        p->setFont(f);
        p->drawText(badge, Qt::AlignCenter, QString::number(unread));
    }
    p->restore();
}
MessageEditor::MessageEditor(QWidget* p) : QPlainTextEdit(p) {
    setObjectName("messageInput");
    setAccessibleName("消息输入框");
    setPlaceholderText("发送消息…");
    setTabChangesFocus(true);
    setMinimumHeight(52);
    setMaximumHeight(132);
    setFrameShape(QFrame::NoFrame);
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        int lines = 0;
        for (auto block = document()->begin(); block.isValid(); block = block.next())
            lines += qMax(1, block.layout()->lineCount());
        setFixedHeight(qBound(52, lines * fontMetrics().lineSpacing() + 12, 132));
    });
}
void MessageEditor::inputMethodEvent(QInputMethodEvent* e) {
    m_composing = !e->preeditString().isEmpty();
    QPlainTextEdit::inputMethodEvent(e);
}
void MessageEditor::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier) &&
        !m_composing) {
        emit submitted();
        e->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}
MessageBubble::MessageBubble(const Message& m, int availableWidth, QWidget* p)
    : QFrame(p), m_outgoing(m.outgoing && !m.file), m_ownMessage(m.outgoing) {
    const auto invitation=GomokuWire::decode(m.text);
    const bool gameInvite=!m.file&&invitation["type"]=="invite";
    int desired = m.file || gameInvite ? 350 : 100;
    QFontMetrics fm(qApp->font());
    if(!gameInvite)for (const auto& part : m.text.split('\n'))
        desired = qMax(desired, fm.horizontalAdvance(part) + 32);
    setFixedWidth(qMin(qMin(desired, 470), qMax(160, availableWidth)));
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(15, 10, 15, 10);
    v->setSpacing(7);
    if (m.file) {
        auto* h = new QHBoxLayout;
        QString extension = QFileInfo(m.text).suffix().left(4).toUpper();
        auto* icon = label(extension.isEmpty() ? "FILE" : extension, 12, "accent", true);
        icon->setFixedSize(42, 44);
        icon->setAlignment(Qt::AlignCenter);
        h->addWidget(icon);
        auto* t = new QVBoxLayout;
        auto* name = label(m.text, 14, nullptr, true);
        name->setWordWrap(true);
        t->addWidget(name);
        t->addWidget(
            label(m.transportStatus.isEmpty() ? "演示文件 · 未下载到本机" : m.transportStatus, 11, "muted"));
        h->addLayout(t, 1);
        auto* open = new IconButton("download", "查看文件传输详情");
        h->addWidget(open);
        connect(open, &QPushButton::clicked, this, &MessageBubble::openFileRequested);
        v->addLayout(h);
    } else if(gameInvite) {
        auto* title=label(GomokuWire::title(GomokuWire::gameId(invitation))+" · 双人对弈",16,nullptr,true);title->setProperty("bubbleOutgoing",m.outgoing);v->addWidget(title);
        auto* text=label(invitation["black"].toString()+" 邀请 "+invitation["white"].toString()+" 加入房间",13);text->setTextFormat(Qt::PlainText);text->setProperty("bubbleOutgoing",m.outgoing);text->setWordWrap(true);v->addWidget(text);
        auto recolor=[title,text,outgoing=m.outgoing]{for(auto* item:QVector<QLabel*>{title,text}){auto palette=item->palette();palette.setColor(QPalette::WindowText,outgoing&&!Theme::instance().pencil()?Theme::instance().colors().onAccent:Theme::instance().colors().text);item->setPalette(palette);}};
        recolor();connect(&Theme::instance(),&Theme::changed,this,recolor);
        auto* join=button(m.outgoing?"打开房间":"加入房间","default");join->setObjectName("joinGomoku_"+invitation["room"].toString());v->addWidget(join);
        connect(join,&QPushButton::clicked,this,&MessageBubble::openGomokuRequested);
    } else {
        auto* text = label(m.text);
        text->setProperty("bubbleOutgoing",m.outgoing);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        if (m.outgoing && !Theme::instance().pencil()) {
            auto p = text->palette();
            p.setColor(QPalette::WindowText, Qt::white);
            text->setPalette(p);
        }
        v->addWidget(text);
    }
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}
void MessageBubble::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if(Theme::instance().pencil()) {
        paintPencilBubble(p,rect(),m_ownMessage,property("locatedMessage").toBool());
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_outgoing ? Theme::instance().colors().outgoing : Theme::instance().colors().incoming);
    p.drawRoundedRect(rect(), 15, 15);
    if (property("locatedMessage").toBool()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::instance().colors().accent, 2));
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 14, 14);
    }
}
TransferCard::TransferCard(const Transfer& t, QWidget* parent) : PaperFrame(parent), m_id(t.id) {
    setProperty("card", true);
    setObjectName("transfer_" + t.id);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(22, 18, 22, 18);
    v->setSpacing(14);
    auto* h = new QHBoxLayout;
    auto* a = new Avatar(t.name.endsWith(".pdf") ? "P" : "F", QColor("#7862bc"), 44);
    h->addWidget(a);
    h->addSpacing(6);
    auto* info = new QVBoxLayout;
    auto* name = label(t.name, 14, nullptr, true);
    name->setWordWrap(true);
    name->setMinimumWidth(0);
    auto* meta = label(t.meta, 12, "muted");
    m_meta = meta;
    meta->setWordWrap(true);
    info->addWidget(name);
    info->addWidget(meta);
    h->addLayout(info, 1);
    m_primary = button("");
    m_primary->setObjectName("transferAction_" + t.id);
    m_cancel = button("取消");
    m_cancel->setObjectName("transferCancel_" + t.id);
    h->addWidget(m_primary);
    h->addWidget(m_cancel);
    v->addLayout(h);
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    v->addWidget(m_progress);
    m_status = label("", 12, "muted");
    m_status->setWordWrap(true);
    m_status->setMinimumWidth(0);
    auto* footer = new QHBoxLayout;
    footer->setSpacing(10);
    footer->addWidget(m_status, 1);
    m_locate = button("对话位置");
    m_locate->setObjectName("transferLocate_" + t.id);
    footer->addWidget(m_locate);
#ifdef Q_OS_WIN
    m_reveal = button("在资源管理器中打开");
#else
    m_reveal = button("在文件管理器中打开");
#endif
    m_reveal->setObjectName("transferReveal_" + t.id);
    footer->addWidget(m_reveal);
    v->addLayout(footer);
    connect(m_primary, &QPushButton::clicked, this, [this] { emit action(m_id, m_action); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { emit action(m_id, "cancel"); });
    connect(m_locate, &QPushButton::clicked, this, [this] { emit action(m_id, "locate"); });
    connect(m_reveal, &QPushButton::clicked, this, [this] { emit action(m_id, "reveal"); });
    refresh(t);
}
void TransferCard::refresh(const Transfer& t) {
    m_meta->setText(t.meta);
    m_locate->setEnabled(!t.conversation.isEmpty() && !t.messageId.isEmpty());
    m_locate->setToolTip(m_locate->isEnabled() ? "在聊天中定位这条文件消息" : "没有对应的消息记录");
    const auto location = t.simulated ? LocalFileLocation{} : localFileLocation(t.path);
    m_reveal->setEnabled(location.available());
    m_reveal->setToolTip(!location.available() ? (t.simulated ? "演示传输没有真实本地文件" : "本地文件位置不可用")
                        : location.selectedFile.isEmpty() ? "文件尚未保存或已移走，打开所在文件夹"
                                                          : "打开所在文件夹并选中文件");
    QString status = t.state == TransferState::Completed ? "completed"
                     : t.state == TransferState::Failed  ? "failed"
                                                         : "active";
    if (m_progress->property("status").toString() != status) {
        m_progress->setProperty("status", status);
        m_progress->style()->unpolish(m_progress);
        m_progress->style()->polish(m_progress);
        m_status->setProperty("role", status == "completed" ? "success"
                                      : status == "failed"  ? "danger"
                                                            : "muted");
        m_status->style()->unpolish(m_status);
        m_status->style()->polish(m_status);
    }
    m_progress->setValue(t.progress);
    m_cancel->setVisible(t.state == TransferState::Running || t.state == TransferState::Paused);
    m_primary->show();
    if (!t.simulated) {
        m_status->setText(t.state == TransferState::Completed
                              ? (t.outgoing ? "已完成 · 对端确认接收" : "已完成 · 长度与摘要校验通过")
                          : t.state == TransferState::Cancelled ? "已取消 · 重新发送需重新选择文件"
                          : t.state == TransferState::Paused ? "历史未完成项 · 可恢复"
                                                             : QString("已验证进度 · %1%").arg(t.progress));
        m_primary->setVisible(t.state != TransferState::Cancelled);
        m_primary->setText(t.state == TransferState::Completed ? "打开文件" : "恢复");
        m_primary->setEnabled(t.state == TransferState::Completed || t.state == TransferState::Paused ||
                              t.state == TransferState::Failed);
        m_action = t.state == TransferState::Completed ? "open" : "resume";
        return;
    }
    switch (t.state) {
    case TransferState::Running:
        m_status->setText(QString("模拟传输中 · %1% · 仅演示进度，不读写文件").arg(t.progress));
        m_primary->setText("暂停");
        m_action = "pause";
        break;
    case TransferState::Paused:
        m_status->setText(QString("已暂停 · %1%").arg(t.progress));
        m_primary->setText("继续");
        m_action = "resume";
        break;
    case TransferState::Failed:
        m_status->setText("模拟失败 · 点击重试可重新演示传输");
        m_primary->setText("重试");
        m_action = "retry";
        break;
    case TransferState::Cancelled:
        m_status->setText("已取消 · 没有文件被发送");
        m_primary->setText("重新开始");
        m_action = "retry";
        break;
    case TransferState::Completed:
        m_status->setText("模拟完成 · 没有真实文件保存到本机");
        m_primary->hide();
        break;
    }
}
} // namespace Messager
