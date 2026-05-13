#include "cpp_chat/storage/user_store.h"

#include <gtest/gtest.h>

namespace cpp_chat::storage {

TEST(UserStore, RegisterSuccessGeneratesUserIdAndHash) {
    UserStore store;
    const auto result = store.register_user("alice", "secret");

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.user_id, 1u);

    const auto user = store.find_user_by_username("alice");
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->id, 1u);
    EXPECT_EQ(user->username, "alice");
    EXPECT_NE(user->password_hash, "secret");
    EXPECT_TRUE(user->password_hash.rfind("pbkdf2_sha256$", 0) == 0);
}

TEST(UserStore, DuplicateUsernameFails) {
    UserStore store;
    ASSERT_TRUE(store.register_user("alice", "secret").success);

    const auto result = store.register_user("alice", "other");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.reason, "username already exists");
}

TEST(UserStore, EmptyFieldsFail) {
    UserStore store;
    EXPECT_FALSE(store.register_user("", "secret").success);
    EXPECT_FALSE(store.register_user("alice", "").success);
}

TEST(UserStore, VerifyLogin) {
    UserStore store;
    ASSERT_TRUE(store.register_user("alice", "secret").success);

    UserRecord user;
    EXPECT_TRUE(store.verify_login("alice", "secret", user));
    EXPECT_EQ(user.username, "alice");
    EXPECT_FALSE(store.verify_login("alice", "wrong", user));
    EXPECT_FALSE(store.verify_login("missing", "secret", user));
}

TEST(UserStore, FindById) {
    UserStore store;
    const auto result = store.register_user("alice", "secret");
    ASSERT_TRUE(result.success);

    const auto user = store.find_user_by_id(result.user_id);
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->username, "alice");
    EXPECT_FALSE(store.find_user_by_id(999).has_value());
}

TEST(PasswordHashing, HashesAndVerifies) {
    const auto hash = hash_password_pbkdf2("secret");
    ASSERT_FALSE(hash.empty());
    EXPECT_NE(hash, "secret");
    EXPECT_TRUE(verify_password_pbkdf2("secret", hash));
    EXPECT_FALSE(verify_password_pbkdf2("wrong", hash));
}

} // namespace cpp_chat::storage
