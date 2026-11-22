#include "accountdialog.h"
#include "theme.h"
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

namespace Messager {
void AccountDialog::showEmailLogin() {
    if(m_busy)return;
    if(auto* existing=findChild<QDialog*>("emailLoginDialog")){existing->show();existing->raise();return;}
    auto* page=new QDialog(this);page->setObjectName("emailLoginDialog");page->setAttribute(Qt::WA_DeleteOnClose);
    page->setWindowTitle("NEXTALK · 邮箱验证码登录");page->setMinimumWidth(470);
    auto style=[this,page]{auto css=styleSheet();css.replace("#accountDialog","#emailLoginDialog").replace("#accountSubmit","#emailLoginSubmit").replace("#sendRegistrationCode","#emailLoginSendCode").replace("#registrationCodeStatus","#emailLoginStatus");page->setStyleSheet(css);};
    style();connect(&Theme::instance(),&Theme::changed,page,style);
    auto* layout=new QVBoxLayout(page);layout->setContentsMargins(34,32,34,28);layout->setSpacing(16);
    auto* title=new QLabel("邮箱验证码登录");title->setStyleSheet("font-size:25px;font-weight:700");layout->addWidget(title);
    auto* subtitle=new QLabel("使用注册时已绑定的邮箱，无需输入密码。");subtitle->setWordWrap(true);layout->addWidget(subtitle);layout->addSpacing(8);
    layout->addWidget(new QLabel("已绑定邮箱"));
    auto* email=new QLineEdit;email->setObjectName("emailLoginEmail");email->setAccessibleName("已绑定邮箱");email->setPlaceholderText("请输入已绑定邮箱");email->setMaxLength(254);email->setFixedHeight(48);email->setInputMethodHints(Qt::ImhEmailCharactersOnly);layout->addWidget(email);
    if(m_username->text().contains('@'))email->setText(m_username->text().trimmed());
    layout->addWidget(new QLabel("邮箱验证码"));
    auto* row=new QHBoxLayout;
    auto* code=new QLineEdit;code->setObjectName("emailLoginCode");code->setAccessibleName("邮箱验证码");code->setPlaceholderText("6 位验证码");code->setMaxLength(6);code->setFixedHeight(48);code->setInputMethodHints(Qt::ImhDigitsOnly);row->addWidget(code,1);
    auto* send=new QPushButton("获取验证码");send->setObjectName("emailLoginSendCode");send->setFixedHeight(48);send->setMinimumWidth(128);send->setAutoDefault(false);row->addWidget(send);layout->addLayout(row);
    auto* status=new QLabel;status->setObjectName("emailLoginStatus");status->setWordWrap(true);status->setMinimumHeight(45);status->setText("验证码 10 分钟内有效，仅用于本次登录。");layout->addWidget(status);
    auto* submit=new QPushButton("登录");submit->setObjectName("emailLoginSubmit");submit->setProperty("authRole","submit");submit->setFixedHeight(50);submit->setDefault(true);layout->addWidget(submit);
    auto* back=new QPushButton("返回密码登录");back->setObjectName("emailLoginBack");back->setProperty("authRole","link");back->setAutoDefault(false);layout->addWidget(back);
    struct State{bool sending=false,logging=false;qint64 nextSend=0;QString recipient;};auto state=std::make_shared<State>();
    auto refresh=[state,email,code,send,submit,back]{
        const auto remaining=qMax<qint64>(0,(state->nextSend-QDateTime::currentMSecsSinceEpoch()+999)/1000);
        send->setText(remaining ? QString::number(remaining)+" 秒后重发" : "获取验证码");
        send->setEnabled(!state->sending&&!state->logging&&!remaining);submit->setEnabled(!state->sending&&!state->logging);
        email->setEnabled(!state->sending&&!state->logging);code->setEnabled(!state->logging);back->setEnabled(!state->sending&&!state->logging);
    };
    auto request=[this,email,state]{auto r=m_config;const auto address=email->text().trimmed().toLower();r["username"]=address;r["email"]=address;r["emailLogin"]=true;r["registerNew"]=false;r["passwordReset"]=false;r["password"]="";state->recipient=address;return r;};
    auto valid=[email,status]{if(!QRegularExpression("^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$").match(email->text().trimmed()).hasMatch()){status->setText("请输入有效的已绑定邮箱。");return false;}return true;};
    connect(send,&QPushButton::clicked,page,[this,state,refresh,status,request,valid]{if(!valid())return;auto r=request();state->sending=true;status->setText("正在发送验证码…");refresh();m_service->requestRegistrationCode(r);});
    connect(submit,&QPushButton::clicked,page,[this,state,refresh,status,request,valid,code]{
        if(!valid())return;if(!QRegularExpression("^[0-9]{6}$").match(code->text()).hasMatch()){status->setText("请输入六位数字验证码。");return;}
        auto r=request();r["emailCode"]=code->text();state->logging=true;status->setText("正在登录…");refresh();m_service->authenticate(r);
    });
    connect(m_service,&ChatService::registrationCodeResult,page,[state,refresh,status](bool ok,const QString& recipient,const QString& error,int retry){
        if(!state->sending || recipient.compare(state->recipient,Qt::CaseInsensitive)!=0)return;
        state->sending=false;if(ok || retry>0)state->nextSend=QDateTime::currentMSecsSinceEpoch()+qMax(ok?60:0,retry)*1000LL;
        status->setText(ok ? "验证码已发送，请查看邮箱。" : error);refresh();
    });
    connect(m_service,&ChatService::operationError,page,[state,refresh,status,code](const QString& error){if(!state->sending&&!state->logging)return;state->sending=false;state->logging=false;code->clear();status->setText(error);refresh();});
    connect(m_service,&ChatService::sessionChanged,page,[this,state,page,code,email]{
        if(!state->logging || !m_service->online())return;
        state->logging=false;code->clear();m_username->setText(email->text().trimmed());emit authenticated();page->accept();accept();
    });
    connect(email,&QLineEdit::textEdited,page,[code,status]{code->clear();status->setText("验证码只可用于接收它的邮箱。");});
    auto* timer=new QTimer(page);timer->setInterval(500);connect(timer,&QTimer::timeout,page,refresh);timer->start();refresh();
    connect(back,&QPushButton::clicked,page,&QDialog::reject);
    connect(page,&QDialog::finished,this,[this,state]{if(!m_service->online()){if(state->sending||state->logging)m_service->disconnectAccount();show();raise();activateWindow();}});
    hide();page->show();page->raise();page->activateWindow();email->setFocus();
}
}
