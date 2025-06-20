#pragma once
#define YOBOT_DATA_NEW_SQL R"(
CREATE TABLE IF NOT EXISTS "db_schema" ("key" VARCHAR(64) NOT NULL PRIMARY KEY, "value" TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS "admin_key" ("key" VARCHAR(255) NOT NULL PRIMARY KEY, "valid" INTEGER NOT NULL, "key_used" INTEGER NOT NULL, "cookie" VARCHAR(255) NOT NULL, "create_time" INTEGER NOT NULL);
CREATE INDEX "admin_key_cookie" ON "admin_key" ("cookie");
CREATE TABLE IF NOT EXISTS "user" ("qqid" INTEGER NOT NULL PRIMARY KEY, "nickname" TEXT, "authority_group" INTEGER NOT NULL, "privacy" INTEGER NOT NULL, "clan_group_id" INTEGER, "last_login_time" INTEGER NOT NULL, "last_login_ipaddr" INTEGER NOT NULL, "password" CHAR(64), "must_change_password" INTEGER NOT NULL, "login_code" CHAR(6), "login_code_available" INTEGER NOT NULL, "login_code_expire_time" INTEGER NOT NULL, "salt" VARCHAR(16) NOT NULL, "deleted" INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS "user_login" ("qqid" INTEGER NOT NULL, "auth_cookie" CHAR(64) NOT NULL, "auth_cookie_expire_time" INTEGER NOT NULL, "last_login_time" INTEGER NOT NULL, "last_login_ipaddr" INTEGER NOT NULL, PRIMARY KEY ("qqid", "auth_cookie"));
CREATE TABLE IF NOT EXISTS "clan_group" ("group_id" INTEGER NOT NULL PRIMARY KEY, "group_name" TEXT, "privacy" INTEGER NOT NULL, "game_server" VARCHAR(2) NOT NULL, "notification" INTEGER NOT NULL, "battle_id" INTEGER NOT NULL, "apikey" VARCHAR(16) NOT NULL, "threshold" INTEGER NOT NULL, "boss_cycle" INTEGER NOT NULL, "now_cycle_boss_health" TEXT NOT NULL, "next_cycle_boss_health" TEXT NOT NULL, "challenging_member_list" TEXT, "subscribe_list" TEXT, "challenging_start_time" INTEGER NOT NULL, "deleted" INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS "clan_member" ("group_id" INTEGER NOT NULL, "qqid" INTEGER NOT NULL, "role" INTEGER NOT NULL, "last_save_slot" INTEGER, "remaining_status" TEXT, PRIMARY KEY ("group_id", "qqid"));
CREATE INDEX "clan_member_group_id" ON "clan_member" ("group_id");
CREATE INDEX "clan_member_qqid" ON "clan_member" ("qqid");
CREATE TABLE IF NOT EXISTS "clan_group_backups" ("group_id" INTEGER NOT NULL, "battle_id" INTEGER NOT NULL, "group_data" TEXT, PRIMARY KEY ("group_id", "battle_id"));
CREATE INDEX "clan_group_backups_group_id" ON "clan_group_backups" ("group_id");
CREATE INDEX "clan_group_backups_battle_id" ON "clan_group_backups" ("battle_id");
CREATE TABLE IF NOT EXISTS "clan_challenge" ("cid" INTEGER NOT NULL PRIMARY KEY, "bid" INTEGER NOT NULL, "gid" INTEGER NOT NULL, "qqid" INTEGER NOT NULL, "challenge_pcrdate" INTEGER NOT NULL, "challenge_pcrtime" INTEGER NOT NULL, "boss_cycle" INTEGER NOT NULL, "boss_num" INTEGER NOT NULL, "boss_health_remain" INTEGER NOT NULL, "challenge_damage" INTEGER NOT NULL, "is_continue" INTEGER NOT NULL, "message" TEXT, "behalf" INTEGER);
CREATE INDEX "clan_challenge_qqid" ON "clan_challenge" ("qqid");
CREATE INDEX "clan_challenge_bid_gid" ON "clan_challenge" ("bid", "gid");
CREATE INDEX "clan_challenge_qqid_challenge_pcrdate" ON "clan_challenge" ("qqid", "challenge_pcrdate");
CREATE INDEX "clan_challenge_bid_gid_challenge_pcrdate" ON "clan_challenge" ("bid", "gid", "challenge_pcrdate");
CREATE TABLE IF NOT EXISTS "character" ("chid" INTEGER NOT NULL PRIMARY KEY, "name" VARCHAR(64) NOT NULL, "frequent" INTEGER NOT NULL);
)"