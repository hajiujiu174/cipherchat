// Included in storage.cpp, after Stmt and account helpers have been defined.
// 判断Blocked条件是否成立。
bool Storage::isBlocked(const std::string& a,const std::string& b) {
    Stmt q(db_,"SELECT 1 FROM blocked_users x JOIN accounts a ON a.id=x.actor JOIN accounts b ON b.id=x.target WHERE (a.username=?1 AND b.username=?2) OR (a.username=?2 AND b.username=?1)");
    q.bind(1,a);q.bind(2,b);return q.step();
}
// 补充动态。
void Storage::decorateMoment(nlohmann::json& post) {
    const auto id=post.at("id").get<std::int64_t>();
    post["image"]="";post["likes"]=nlohmann::json::array();post["comments"]=nlohmann::json::array();
    Stmt image(db_,"SELECT png FROM moment_images WHERE post_id=?1");image.bind(1,id);if(image.step())post["image"]=image.colText(0);
    Stmt likes(db_,"SELECT username FROM moment_likes WHERE post_id=?1 ORDER BY username LIMIT 1000");likes.bind(1,id);while(likes.step())post["likes"].push_back(likes.colText(0));
    Stmt comments(db_,"SELECT id,username,text,created_ms FROM moment_comments WHERE post_id=?1 ORDER BY id LIMIT 200");comments.bind(1,id);
    while(comments.step())post["comments"].push_back({{"id",comments.colInt(0)},{"username",comments.colText(1)},{"text",comments.colText(2)},{"created_ms",comments.colInt(3)}});
}
// 返回服务端 SQLite 存储使用的extendedSocial请求。
std::optional<nlohmann::json> Storage::extendedSocialRequest(const std::string& actor,const nlohmann::json& r,std::int64_t now) {
    const auto user=findAccount(actor);if(!user)throw std::runtime_error("not_logged_in");
    const auto op=r.at("op").get<std::string>();
    nlohmann::json out={{"op",op},{"ok",true},{"username",actor}};
    if(op=="groups") {out["groups"]=nlohmann::json::array();for(const auto& group:groupsForUser(actor))out["groups"].push_back({{"id",group.id},{"revision",group.revision},{"name",group.name},{"members",group.members}});return out;}
    if(op=="friend_remove"||op=="block"||op=="unblock"||op=="blocked") {
        if(op=="blocked") {
            out["users"]=nlohmann::json::array();Stmt list(db_,"SELECT a.username FROM blocked_users b JOIN accounts a ON a.id=b.target WHERE b.actor=?1 ORDER BY a.username");list.bind(1,user->id);while(list.step())out["users"].push_back(list.colText(0));return out;
        }
        const auto name=r.at("username").get<std::string>();const auto target=findAccount(name);
        if(!target||name==actor)throw std::runtime_error("invalid_user");
        ccfw::sql::Transaction tx(db_);
        if(op!="unblock") {
            Stmt remove(db_,"DELETE FROM friends WHERE (user_id=?1 AND friend_id=?2) OR (user_id=?2 AND friend_id=?1)");remove.bind(1,user->id);remove.bind(2,target->id);remove.step();
            Stmt requests(db_,"DELETE FROM friend_requests WHERE (sender=?1 AND recipient=?2) OR (sender=?2 AND recipient=?1)");requests.bind(1,user->id);requests.bind(2,target->id);requests.step();
        }
        Stmt block(db_,op=="block"?"INSERT OR IGNORE INTO blocked_users VALUES(?1,?2)":"DELETE FROM blocked_users WHERE actor=?1 AND target=?2");
        if(op!="friend_remove"){block.bind(1,user->id);block.bind(2,target->id);block.step();}
        tx.commit();out["notify_users"]={actor,name};return out;
    }
    if(op=="group_rename"||op=="group_add"||op=="group_remove"||op=="group_leave"||op=="group_dissolve") {
        const auto id=r.at("group_id").get<std::int64_t>();auto group=findGroup(id);if(!group)throw std::runtime_error("not_found");
        if(std::find(group->members.begin(),group->members.end(),actor)==group->members.end())throw std::runtime_error("forbidden");
        Stmt owner(db_,"SELECT username FROM group_owners WHERE group_id=?1");owner.bind(1,id);if(!owner.step())throw std::runtime_error("forbidden");const auto creator=owner.colText(0);
        if(op!="group_leave"&&creator!=actor)throw std::runtime_error("forbidden");
        const auto old=group->members;auto members=old;std::string name=group->name;
        if(op=="group_rename"){name=r.at("name").get<std::string>();validateSocialText(name,32,false);}
        if(op=="group_add") {
            const auto names=r.at("members").get<std::vector<std::string>>();const auto friends=friendsForUser(actor);
            for(const auto& member:names){if(!findAccount(member)||isBlocked(actor,member)||std::find(friends.begin(),friends.end(),member)==friends.end())throw std::runtime_error("forbidden");if(std::find(members.begin(),members.end(),member)==members.end())members.push_back(member);}
            if(members.size()>64)throw std::runtime_error("quota_reached");
        }
        if(op=="group_remove"||op=="group_leave") {
            const auto member=op=="group_leave"?actor:r.at("member").get<std::string>();
            if(member==creator&&op!="group_leave")throw std::runtime_error("forbidden");
            if(std::find(members.begin(),members.end(),member)==members.end())throw std::runtime_error("not_found");
            std::erase(members,member);
        }
        if(op=="group_dissolve")members.clear();
        ccfw::sql::Transaction tx(db_);
        Stmt update(db_,"UPDATE groups SET name=?1,revision=revision+1 WHERE id=?2");update.bind(1,name);update.bind(2,id);update.step();
        for(const auto& member:old)if(std::find(members.begin(),members.end(),member)==members.end()){Stmt former(db_,"INSERT OR IGNORE INTO group_former_members VALUES(?1,?2)");former.bind(1,id);former.bind(2,member);former.step();}
        for(const auto& member:members){Stmt former(db_,"DELETE FROM group_former_members WHERE group_id=?1 AND username=?2");former.bind(1,id);former.bind(2,member);former.step();}
        Stmt clear(db_,"DELETE FROM group_members WHERE group_id=?1");clear.bind(1,id);clear.step();
        for(const auto& member:members){Stmt add(db_,"INSERT INTO group_members VALUES(?1,?2)");add.bind(1,id);add.bind(2,member);add.step();}
        if(!members.empty()&&std::find(members.begin(),members.end(),creator)==members.end()){Stmt transfer(db_,"UPDATE group_owners SET username=?1 WHERE group_id=?2");transfer.bind(1,members.front());transfer.bind(2,id);transfer.step();}
        tx.commit();out["group"]={{"id",id},{"name",name},{"revision",group->revision+1},{"members",members}};out["notify_users"]=old;
        for(const auto& member:members)if(std::find(old.begin(),old.end(),member)==old.end())out["notify_users"].push_back(member);
        return out;
    }
    if(op=="app_get"||op=="app_save") {
        ccfw::sql::Transaction tx(db_);Stmt prior(db_,"SELECT revision,value FROM app_sync WHERE user_id=?1");prior.bind(1,user->id);
        std::int64_t revision=0;auto value=nlohmann::json::object();if(prior.step()){revision=prior.colInt(0);value=nlohmann::json::parse(prior.colText(1));}
        if(op=="app_save") {
            if(r.at("revision").get<std::int64_t>()!=revision)throw std::runtime_error("conflict");
            value=r.at("value");if(!value.is_object()||value.dump().size()>256*1024)throw std::runtime_error("quota_reached");++revision;
            Stmt write(db_,"INSERT INTO app_sync VALUES(?1,?2,?3) ON CONFLICT(user_id) DO UPDATE SET revision=excluded.revision,value=excluded.value");write.bind(1,user->id);write.bind(2,revision);write.bind(3,value.dump());write.step();
        }
        tx.commit();out["revision"]=revision;out["value"]=value;return out;
    }
    if(op=="moment_like"||op=="moment_comment"||op=="moment_comment_delete"||op=="moment_edit"||op=="moment_delete") {
        const auto id=r.at("id").get<std::int64_t>();Stmt post(db_,"SELECT a.username FROM user_moments m JOIN accounts a ON a.id=m.user_id WHERE m.id=?1");post.bind(1,id);if(!post.step())throw std::runtime_error("not_found");const auto author=post.colText(0);const auto friends=friendsForUser(actor);
        if(actor!=author&&(isBlocked(actor,author)||std::find(friends.begin(),friends.end(),author)==friends.end()))throw std::runtime_error("forbidden");
        ccfw::sql::Transaction tx(db_);
        if(op=="moment_delete"||op=="moment_edit") {
            if(actor!=author)throw std::runtime_error("forbidden");
            Stmt change(db_,op=="moment_delete"?"DELETE FROM user_moments WHERE id=?1":"UPDATE user_moments SET text=?2 WHERE id=?1");change.bind(1,id);
            if(op=="moment_edit"){auto text=r.at("text").get<std::string>();validateSocialText(text,2000,true);change.bind(2,text);}change.step();
        } else if(op=="moment_like") {
            Stmt change(db_,r.value("liked",true)?"INSERT OR IGNORE INTO moment_likes VALUES(?1,?2)":"DELETE FROM moment_likes WHERE post_id=?1 AND username=?2");change.bind(1,id);change.bind(2,actor);change.step();
        } else if(op=="moment_comment") {
            auto text=r.at("text").get<std::string>();validateSocialText(text,500,true);
            Stmt count(db_,"SELECT count(*) FROM moment_comments WHERE post_id=?1");count.bind(1,id);count.step();if(count.colInt(0)>=200)throw std::runtime_error("quota_reached");
            Stmt add(db_,"INSERT INTO moment_comments(post_id,username,text,created_ms) VALUES(?1,?2,?3,?4)");add.bind(1,id);add.bind(2,actor);add.bind(3,text);add.bind(4,now);add.step();
        } else {
            Stmt remove(db_,"DELETE FROM moment_comments WHERE id=?1 AND post_id=?2 AND (username=?3 OR ?4=?3)");remove.bind(1,r.at("comment_id").get<std::int64_t>());remove.bind(2,id);remove.bind(3,actor);remove.bind(4,author);remove.step();if(!sqlite3_changes(db_))throw std::runtime_error("forbidden");
        }
        tx.commit();out["username"]=author;out["id"]=id;return out;
    }
    return std::nullopt;
}
