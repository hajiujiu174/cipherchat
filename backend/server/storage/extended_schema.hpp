#pragma once
namespace cc::server {
constexpr const char* extendedSchema=R"(
CREATE TABLE IF NOT EXISTS blocked_users(actor INTEGER REFERENCES accounts(id) ON DELETE CASCADE,target INTEGER REFERENCES accounts(id) ON DELETE CASCADE,PRIMARY KEY(actor,target),CHECK(actor<>target));
CREATE TABLE IF NOT EXISTS group_owners(group_id INTEGER PRIMARY KEY REFERENCES groups(id) ON DELETE CASCADE,username TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS group_former_members(group_id INTEGER REFERENCES groups(id) ON DELETE CASCADE,username TEXT NOT NULL,PRIMARY KEY(group_id,username));
INSERT OR IGNORE INTO group_owners SELECT group_id,min(username) FROM group_members GROUP BY group_id;
CREATE TABLE IF NOT EXISTS moment_images(post_id INTEGER PRIMARY KEY REFERENCES user_moments(id) ON DELETE CASCADE,png TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS moment_likes(post_id INTEGER REFERENCES user_moments(id) ON DELETE CASCADE,username TEXT NOT NULL,PRIMARY KEY(post_id,username));
CREATE TABLE IF NOT EXISTS moment_comments(id INTEGER PRIMARY KEY AUTOINCREMENT,post_id INTEGER REFERENCES user_moments(id) ON DELETE CASCADE,username TEXT NOT NULL,text TEXT NOT NULL,created_ms INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS app_sync(user_id INTEGER PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,revision INTEGER NOT NULL,value TEXT NOT NULL);
)";
}
