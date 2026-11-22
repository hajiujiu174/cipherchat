#include "profilepage.h"
#include "ui/pagewidgets.h"
#include "features/files/imageimport.h"
#include <QBuffer>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QUuid>
#include <QVBoxLayout>
namespace Messager {
// 添加动态操作列表。
void addMomentActions(QVBoxLayout* v,ChatService* service,const QJsonObject& post,const QString& author,QWidget* parent) {
    const auto id=post["id"].toInteger();
    const auto encoded=post["image"].toString();
    if(!encoded.isEmpty()){QImage image;const auto bytes=QByteArray::fromBase64(encoded.toLatin1());if(bytes.size()<=288*1024&&image.loadFromData(bytes,"PNG")&&image.width()<=256&&image.height()<=256){auto* picture=new QLabel;picture->setPixmap(QPixmap::fromImage(image));picture->setObjectName("momentImage");v->addWidget(picture);}}
    auto* controls=new QHBoxLayout;const auto likes=post["likes"].toArray();bool liked=false;for(auto value:likes)liked|=value.toString()==service->username();
    auto* like=button(QString(liked?"取消赞 (%1)":"点赞 (%1)").arg(likes.size()));like->setObjectName("momentLike");controls->addWidget(like);
    QObject::connect(like,&QPushButton::clicked,parent,[service,id,liked]{service->socialAction({{"op","moment_like"},{"id",id},{"liked",!liked}});});
    auto* comment=button("评论");comment->setObjectName("momentComment");controls->addWidget(comment);
    QObject::connect(comment,&QPushButton::clicked,parent,[service,id,parent]{bool ok=false;const auto text=QInputDialog::getMultiLineText(parent,"评论","评论内容（最多500字）",{},&ok);if(ok&&!text.trimmed().isEmpty())service->socialAction({{"op","moment_comment"},{"id",id},{"text",text}});});
    if(author==service->username()){
        auto* edit=button("编辑");edit->setObjectName("momentEdit");controls->addWidget(edit);QObject::connect(edit,&QPushButton::clicked,parent,[service,id,post,parent]{bool ok=false;const auto text=QInputDialog::getMultiLineText(parent,"编辑动态","内容",post["text"].toString(),&ok);if(ok)service->socialAction({{"op","moment_edit"},{"id",id},{"text",text}});});
        auto* remove=button("删除");remove->setObjectName("momentDelete");controls->addWidget(remove);QObject::connect(remove,&QPushButton::clicked,parent,[service,id,parent]{if(QMessageBox::question(parent,"删除动态","确定删除这条动态及其评论和点赞？")==QMessageBox::Yes)service->socialAction({{"op","moment_delete"},{"id",id}});});
    }
    controls->addStretch();v->addLayout(controls);
    for(const auto& value:post["comments"].toArray()){
        const auto c=value.toObject();auto* row=new QHBoxLayout;auto* text=label(c["username"].toString()+"："+c["text"].toString(),13);text->setTextFormat(Qt::PlainText);text->setWordWrap(true);row->addWidget(text,1);
        if(c["username"].toString()==service->username()||author==service->username()){auto* remove=button("删除评论");row->addWidget(remove);QObject::connect(remove,&QPushButton::clicked,parent,[service,id,c]{service->socialAction({{"op","moment_comment_delete"},{"id",id},{"comment_id",c["id"]}});});}v->addLayout(row);
    }
}
// 返回资料与社交使用的资料头像。
QPixmap profileAvatar(const QString& base64,int size) {
    QImage image;auto raw=QByteArray::fromBase64(base64.toLatin1());
    if(raw.size()>288*1024 || !image.loadFromData(raw,"PNG") || image.width()>256 || image.height()>256)return {};
    QPixmap result(size,size);result.fill(Qt::transparent);QPainter p(&result);p.setRenderHint(QPainter::Antialiasing);QPainterPath clip;clip.addEllipse(result.rect());p.setClipPath(clip);p.drawImage(result.rect(),image);return result;
}
// 构造个人资料页，初始化后续操作需要的状态或依赖。
ProfilePage::ProfilePage(ChatService* service,QWidget* parent):QWidget(parent),m_service(service) {
    setObjectName("personalProfilePage");auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);outer->addWidget(scroll);
    auto* body=new PaperFrame;scroll->setWidget(body);auto* centered=new QHBoxLayout(body);centered->setContentsMargins(28,24,28,24);
    auto* content=new QWidget;content->setMaximumWidth(860);centered->addStretch();centered->addWidget(content,1);centered->addStretch();auto* v=new QVBoxLayout(content);v->setSpacing(20);
    auto* nav=new QHBoxLayout;auto* back=button("返回", "ghost", "back");back->setObjectName("profileBack");nav->addWidget(back);nav->addStretch();auto* reload=button("刷新");reload->setObjectName("profileRefresh");nav->addWidget(reload);v->addLayout(nav);
    connect(back,&QPushButton::clicked,this,&ProfilePage::backRequested);connect(reload,&QPushButton::clicked,this,[this]{m_service->requestProfile(m_user);});
    auto* header=card();auto* h=new QHBoxLayout(header);h->setContentsMargins(26,26,26,26);h->setSpacing(22);
    m_picture=new Avatar("",QColor("#6954d6"),96);m_picture->setObjectName("profileAvatar");h->addWidget(m_picture);
    auto* names=new QVBoxLayout;m_name=label("个人资料",26,nullptr,true);m_name->setObjectName("profileNickname");m_name->setTextFormat(Qt::PlainText);names->addWidget(m_name);
    m_account=label("",13,"muted");m_account->setObjectName("profileUsername");m_account->setTextFormat(Qt::PlainText);names->addWidget(m_account);h->addLayout(names,1);v->addWidget(header);
    m_editArea=new QWidget;auto* edit=new QHBoxLayout(m_editArea);edit->setContentsMargins(0,0,0,0);
    m_nickname=new QLineEdit;m_nickname->setMaxLength(64);m_nickname->setObjectName("profileNicknameEdit");m_nickname->setPlaceholderText("昵称，最多32个字");edit->addWidget(m_nickname,1);
    auto* photo=button("更换头像");photo->setObjectName("changeAvatarButton");edit->addWidget(photo);auto* reset=button("默认头像");reset->setObjectName("resetAvatarButton");edit->addWidget(reset);
    m_save=button("保存资料","primary");m_save->setObjectName("saveProfileButton");edit->addWidget(m_save);v->addWidget(m_editArea);
    connect(m_nickname,&QLineEdit::textEdited,this,[this]{m_dirty=true;updateActions();});
    connect(photo,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,"选择头像",{},"图片 (*.png *.jpg *.jpeg *.webp)");if(!path.isEmpty())chooseAvatarFile(path);});
    connect(reset,&QPushButton::clicked,this,[this]{m_avatar.clear();m_picture->setImage({});m_dirty=true;updateActions();});
    connect(m_save,&QPushButton::clicked,this,[this]{m_saving=true;updateActions();m_service->saveProfile(m_nickname->text().trimmed(),m_avatar);});
    m_notice=label("",13,"muted");m_notice->setWordWrap(true);m_notice->setObjectName("profileStatus");v->addWidget(m_notice);
    m_compose=card();auto* compose=new QVBoxLayout(m_compose);compose->setContentsMargins(20,18,20,18);compose->addWidget(label("写一条朋友圈",16,nullptr,true));
    m_text=new QPlainTextEdit;m_text->setObjectName("momentText");m_text->setPlaceholderText("记录此刻的想法…仅自己和好友可见");m_text->setFixedHeight(120);compose->addWidget(m_text);
    auto* controls=new QHBoxLayout;m_count=label("0 / 2000",12,"muted");controls->addWidget(m_count);controls->addStretch();auto* attach=button("添加图片");attach->setObjectName("momentAttachImage");controls->addWidget(attach);
    connect(attach,&QPushButton::clicked,this,[this,attach]{const auto path=QFileDialog::getOpenFileName(this,"动态图片",{},"图片 (*.png *.jpg *.jpeg *.webp)");if(path.isEmpty())return;QString error;m_momentImage=imageAsPng(path,&error);m_publishKey.clear();attach->setText(m_momentImage.isEmpty()?"添加图片":"更换图片");m_notice->setText(error.isEmpty()?"动态图片已选择":error);});
    m_publish=button("发布","primary");m_publish->setObjectName("publishMomentButton");controls->addWidget(m_publish);compose->addLayout(controls);v->addWidget(m_compose);
    connect(m_text,&QPlainTextEdit::textChanged,this,[this]{m_publishKey.clear();updateActions();});
    connect(m_publish,&QPushButton::clicked,this,[this]{if(m_publishKey.isEmpty())m_publishKey=QUuid::createUuid().toString(QUuid::WithoutBraces);m_posting=true;updateActions();if(m_momentImage.isEmpty())m_service->publishMoment(m_text->toPlainText().trimmed(),m_publishKey);else m_service->socialAction({{"op","publish"},{"text",m_text->toPlainText().trimmed()},{"request_id",m_publishKey},{"image",m_momentImage}});});
    v->addWidget(label("朋友圈",18,nullptr,true));m_posts=new QVBoxLayout;m_posts->setSpacing(14);v->addLayout(m_posts);
    m_more=button("加载更早的动态");m_more->setObjectName("profileMore");v->addWidget(m_more);connect(m_more,&QPushButton::clicked,this,[this]{m_more->setEnabled(false);m_service->requestProfile(m_user,m_next);});m_message=button("发消息","primary","chat");m_message->setObjectName("profileSendMessage");v->addWidget(m_message);
    connect(m_message,&QPushButton::clicked,this,[this]{emit chatRequested(m_user);});v->addStretch();
    connect(service,&ChatService::profileChanged,this,[this](const QString& user){if(user==m_user)refresh();});
    connect(service,&ChatService::profileOperationFinished,this,[this](const QString& op,const QString& user,bool ok,const QString& error){if(user!=m_user)return;if(op=="save"){m_saving=false;if(ok)m_dirty=false;}if(op=="publish"){m_posting=false;if(ok){m_text->clear();m_publishKey.clear();m_momentImage.clear();}}if(!ok)m_notice->setText(error);else if(op=="save")m_notice->setText("资料已保存");updateActions();});
    connect(service,&ChatService::sessionChanged,this,[this]{if(!m_service->online()){m_saving=m_posting=m_dirty=false;m_user.clear();m_avatar.clear();m_text->clear();m_name->setText("个人资料");m_account->clear();m_picture->setImage({});while(auto* item=m_posts->takeAt(0)){if(item->widget()){item->widget()->hide();item->widget()->deleteLater();}delete item;}m_notice->setText("请登录后查看个人资料");}updateActions();});
    connect(service,&ChatService::operationError,this,[this](const QString& error){if(!isVisible())return;m_saving=m_posting=false;m_more->setEnabled(m_service->online());m_notice->setText(error);updateActions();});
    connect(&Theme::instance(),&Theme::changed,this,[this]{const auto c=Theme::instance().colors();m_text->setStyleSheet(QString("QPlainTextEdit { background:%1; color:%2; border:1px solid %3; border-radius:8px; padding:10px; }").arg(c.subtle.name(),c.text.name(),c.border.name()));});
    const auto c=Theme::instance().colors();m_text->setStyleSheet(QString("QPlainTextEdit { background:%1; color:%2; border:1px solid %3; border-radius:8px; padding:10px; }").arg(c.subtle.name(),c.text.name(),c.border.name()));
}
// 切换个人资料页当前展示的账号，请求该账号的最新资料。
void ProfilePage::openUser(const QString& user) {
    if(m_user!=user){m_user=user;m_dirty=false;m_avatar.clear();m_text->clear();m_publishKey.clear();m_momentImage.clear();m_saving=m_posting=false;}
    m_notice->setText("正在加载资料…");refresh();m_service->requestProfile(user);
}
// 读取用户选择的头像文件并预览可保存的图片。
bool ProfilePage::chooseAvatarFile(const QString& path) {
    if(m_user!=m_service->username()||!m_service->online())return false;
    QString error;auto image=importImage(path,&error);
    if(image.isNull()){m_notice->setText(error);return false;}
    image=image.scaled(256,256,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);image=image.copy((image.width()-256)/2,(image.height()-256)/2,256,256);
    QImage normalized(256,256,QImage::Format_ARGB32);normalized.fill(Qt::transparent);{QPainter p(&normalized);p.drawImage(0,0,image);}image=normalized;
    QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);
    if(!image.save(&output,"PNG")||bytes.size()>288*1024){m_notice->setText("头像处理失败，请换一张图片");return false;}
    m_avatar=QString::fromLatin1(bytes.toBase64());m_picture->setImage(bytes);m_dirty=true;m_notice->setText("头像已选择，点击“保存资料”后同步");updateActions();return true;
}
// 更新操作列表。
void ProfilePage::updateActions() {
    const bool own=m_user==m_service->username();const bool online=m_service->online();
    m_message->setVisible(!own&&!m_user.isEmpty());m_message->setEnabled(online);
    m_editArea->setVisible(own);m_editArea->setEnabled(online&&!m_saving);m_compose->setVisible(own);m_nickname->setEnabled(!m_saving);m_save->setEnabled(online&&m_dirty&&!m_saving&&!m_nickname->text().trimmed().isEmpty()&&m_nickname->text().toUcs4().size()<=32);
    m_text->setEnabled(online&&!m_posting);m_count->setText(QString::number(m_text->toPlainText().toUcs4().size())+" / 2000");m_publish->setEnabled(online&&!m_posting&&!m_text->toPlainText().trimmed().isEmpty()&&m_text->toPlainText().toUcs4().size()<=2000);
}
// 刷新个人资料页。
void ProfilePage::refresh() {
    auto p=m_service->profile(m_user);const auto nickname=p.value("nickname").toString(m_user);m_name->setText(nickname);m_account->setText("@"+m_user);
    if(!m_dirty){m_avatar=p["avatar"].toString();m_picture->setImage(QByteArray::fromBase64(m_avatar.toLatin1()));m_nickname->setText(nickname);}
    while(auto* item=m_posts->takeAt(0)){if(item->widget()){item->widget()->hide();item->widget()->deleteLater();}delete item;}
    if(!p.isEmpty())m_notice->setText(p["can_view"].toBool()?"朋友圈仅自己和好友可见":"添加对方为好友后可查看朋友圈");
    for(auto item:p["posts"].toArray()){
        auto post=item.toObject();auto* f=card();f->setObjectName("moment_"+QString::number(qint64(post["id"].toDouble())));auto* v=new QVBoxLayout(f);v->setContentsMargins(22,18,22,18);v->setSpacing(12);
        auto* date=label(QDateTime::fromMSecsSinceEpoch(qint64(post["created_ms"].toDouble())).toString("yyyy年M月d日 HH:mm"),12,"muted");v->addWidget(date);
        auto* text=label(post["text"].toString(),15);text->setObjectName("momentBody");text->setTextFormat(Qt::PlainText);text->setWordWrap(true);text->setTextInteractionFlags(Qt::TextSelectableByMouse);v->addWidget(text);addMomentActions(v,m_service,post,m_user,this);m_posts->addWidget(f);
    }
    if(!p.isEmpty()&&p["posts"].toArray().isEmpty())m_posts->addWidget(label(p["can_view"].toBool()?"还没有发布朋友圈":"对方的朋友圈仅好友可见",13,"muted"));
    m_next=qint64(p["next"].toDouble());m_more->setVisible(m_next>0);m_more->setEnabled(m_service->online());updateActions();
}
}
