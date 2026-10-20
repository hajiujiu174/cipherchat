#include "momentspage.h"
#include "pagewidgets.h"
#include "profilepage.h"
#include <QDateTime>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
namespace Messager {
MomentsPage::MomentsPage(ChatService* service,QWidget* parent):QWidget(parent),m_service(service) {
    setObjectName("momentsFeedPage");auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    m_scroll=new QScrollArea;m_scroll->setWidgetResizable(true);m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);outer->addWidget(m_scroll);
    auto* body=new PaperFrame;m_scroll->setWidget(body);auto* centered=new QHBoxLayout(body);centered->setContentsMargins(28,24,28,24);
    auto* content=new QWidget;content->setMaximumWidth(860);centered->addStretch();centered->addWidget(content,1);centered->addStretch();auto* v=new QVBoxLayout(content);v->setSpacing(18);
    auto* head=new QHBoxLayout;auto* titles=new QVBoxLayout;titles->addWidget(label("朋友圈",28,nullptr,true));titles->addWidget(label("自己和好友的近况 · 最新动态在前",13,"muted"));head->addLayout(titles,1);
    m_refresh=button("刷新");m_refresh->setObjectName("momentsFeedRefresh");head->addWidget(m_refresh);
    m_publish=button("发布动态","primary","plus");m_publish->setObjectName("momentsFeedPublish");head->addWidget(m_publish);v->addLayout(head);
    m_status=label("",13,"muted");m_status->setObjectName("momentsFeedStatus");m_status->setWordWrap(true);v->addWidget(m_status);
    m_posts=new QVBoxLayout;m_posts->setSpacing(16);v->addLayout(m_posts);
    m_more=button("加载更早的动态");m_more->setObjectName("momentsFeedMore");v->addWidget(m_more);v->addStretch();
    connect(m_refresh,&QPushButton::clicked,this,[this]{load();});connect(m_more,&QPushButton::clicked,this,[this]{load(true);});
    connect(m_publish,&QPushButton::clicked,this,[this]{emit profileRequested(m_service->username());});
    connect(service,&ChatService::momentsFeedChanged,this,&MomentsPage::render);
    connect(service,&ChatService::momentsFeedFinished,this,[this](bool ok,const QString& error){m_loading=false;render();if(!ok)m_status->setText(error);});
    connect(service,&ChatService::sessionChanged,this,[this]{if(!m_service->online())m_loading=false;render();});
    connect(service,&ChatService::operationError,this,[this](const QString& error){if(isVisible()&&m_loading){m_loading=false;render();m_status->setText(error);}});
    render();
}
void MomentsPage::openFeed(){load();}
void MomentsPage::load(bool older) {
    if(m_loading)return;
    m_loading=true;
    if(!older)m_scroll->verticalScrollBar()->setValue(0);
    const auto feed=m_service->momentsFeed();render();
    m_service->requestMomentsFeed(older?qint64(feed["next_ms"].toDouble()):0,older?qint64(feed["next_id"].toDouble()):0);
}
void MomentsPage::render() {
    while(auto* item=m_posts->takeAt(0)){if(auto* widget=item->widget()){widget->hide();widget->deleteLater();}delete item;}
    const auto feed=m_service->momentsFeed();const bool online=m_service->online();
    m_refresh->setEnabled(online&&!m_loading);m_publish->setEnabled(online);m_more->setVisible(online&&feed["next_id"].toDouble()>0);m_more->setEnabled(!m_loading);
    m_status->setText(!online?"请登录后查看自己和好友的朋友圈":m_loading?"正在加载动态…":"仅自己和好友可见");
    if(!online)return;
    const auto posts=feed["posts"].toArray();
    if(posts.isEmpty()&&!m_loading)m_posts->addWidget(label("还没有动态，写下今天的第一条近况吧。",14,"muted"));
    for(auto item:posts){
        const auto post=item.toObject();const auto user=post["username"].toString();const auto nickname=post["nickname"].toString(user);
        auto* frame=card();frame->setObjectName("feedMoment_"+QString::number(qint64(post["id"].toDouble())));auto* v=new QVBoxLayout(frame);v->setContentsMargins(22,20,22,20);v->setSpacing(14);
        auto* header=new QHBoxLayout;header->setSpacing(12);auto* avatar=button(nickname.left(1),"chip");avatar->setFixedSize(44,44);avatar->setObjectName("feedAvatar_"+user);avatar->setAccessibleName(nickname+"的个人资料");
        const auto image=profileAvatar(m_service->profile(user)["avatar"].toString(),80);if(!image.isNull()){avatar->setText({});avatar->setIcon(QIcon(image));avatar->setIconSize({38,38});}header->addWidget(avatar);
        auto* identity=new QVBoxLayout;auto* name=button(nickname,"ghost");name->setObjectName("feedAuthor_"+user);name->setAccessibleName(nickname+"的个人资料");identity->addWidget(name,0,Qt::AlignLeft);
        identity->addWidget(label(QDateTime::fromMSecsSinceEpoch(qint64(post["created_ms"].toDouble())).toString("yyyy年M月d日 HH:mm"),12,"muted"));header->addLayout(identity);header->addStretch();if(user==m_service->username())header->addWidget(label("我",12,"muted"));v->addLayout(header);
        connect(avatar,&QPushButton::clicked,this,[this,user]{emit profileRequested(user);});connect(name,&QPushButton::clicked,this,[this,user]{emit profileRequested(user);});
        auto* text=label(post["text"].toString(),15);text->setObjectName("feedMomentBody");text->setTextFormat(Qt::PlainText);text->setWordWrap(true);text->setTextInteractionFlags(Qt::TextSelectableByMouse);v->addWidget(text);m_posts->addWidget(frame);
    }
}
}
