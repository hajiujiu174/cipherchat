#include "mainwindow.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "pagewidgets.h"
namespace Messager {
void MainWindow::showLogin() {
    if (!m_controller->service()->isDemo()) {
        showAccountDialog();
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("NEXTALK · 账号原型");
    dialog.setMinimumWidth(440);
    auto* v = new QVBoxLayout(&dialog);
    v->setContentsMargins(36, 30, 36, 28);
    v->setSpacing(18);
    v->addWidget(new Avatar("M", QColor("#6954d6"), 58), 0, Qt::AlignHCenter);
    auto* title = label("欢迎来到 NEXTALK", 24, nullptr, true);
    title->setAlignment(Qt::AlignCenter);
    v->addWidget(title);
    auto* hint = label("登录 / 注册界面预览 · 不会连接服务器", 12, "muted");
    hint->setAlignment(Qt::AlignCenter);
    v->addWidget(hint);
    auto* tabs = new QHBoxLayout;
    auto *login = button("登录", "chip"), *reg = button("注册", "chip");
    login->setCheckable(true);
    reg->setCheckable(true);
    auto* g = new QButtonGroup(&dialog);
    g->addButton(login, 0);
    g->addButton(reg, 1);
    login->setChecked(true);
    tabs->addWidget(login);
    tabs->addWidget(reg);
    v->addLayout(tabs);
    auto* name = new QLineEdit;
    name->setPlaceholderText("演示昵称");
    name->setAccessibleName("演示昵称");
    auto* account = new QLineEdit;
    account->setPlaceholderText("账号（演示占位）");
    account->setAccessibleName("演示账号");
    auto* password = new QLineEdit;
    password->setPlaceholderText("密码（请勿输入真实密码）");
    password->setAccessibleName("演示密码");
    password->setEchoMode(QLineEdit::Password);
    v->addWidget(name);
    name->hide();
    v->addWidget(account);
    v->addWidget(password);
    connect(g, &QButtonGroup::idClicked, &dialog, [name](int mode) { name->setVisible(mode == 1); });
    v->addWidget(banner("真实身份认证尚未实现。此表单不保存账号或密码，进入后使用虚构的演示账号。"));
    auto* enter = button("进入本地演示", "primary");
    v->addWidget(enter);
    connect(enter, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

} // namespace Messager
