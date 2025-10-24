#include "thread_pool/thread_pool.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <filesystem>

TEST(ConfigLoader, FromString) {
    const std::string str = R"({
        "queue_cap": 256,
        "core_threads": 2,
        "max_threads": 4,
        "queue_policy": "Discard"
    })";

    auto loadout = thread_pool::ThreadPoolConfigLoader::FromString(str);
    EXPECT_TRUE(loadout.has_value());
    thread_pool::ThreadPoolConfigLoader loader = std::move(*loadout); // move out of optional
    EXPECT_TRUE(loader.Ready());

    const auto cfg = loader.GetConfig();
    EXPECT_EQ(cfg.queue_cap, 256u);
    EXPECT_EQ(cfg.core_threads, 2u);
    EXPECT_EQ(cfg.max_threads, 4u);
    EXPECT_EQ(cfg.queue_policy, thread_pool::QueueFullPolicy::Discard);

    // Export to JSON
    const auto dumped = loader.Dump();
    auto j = nlohmann::json::parse(dumped);
    EXPECT_EQ(j.at("queue_cap").get<size_t>(), 256u);
    EXPECT_EQ(j.at("core_threads").get<size_t>(), 2u);
}

TEST(ConfigLoader, FromJson) {
    nlohmann::json j = {
        {"queue_cap", 1024},
        {"core_threads", 3},
        {"max_threads", 8},
        {"scale_down_threshold", 0.25},
        {"scale_up_threshold", 0.75},
        {"pending_low", 4},
        {"pending_hi", 32},
        {"debounce_hits", 2},
        {"cooldown_ms", 500},
        {"queue_policy", "Block"}
    };

    auto loadout = thread_pool::ThreadPoolConfigLoader::FromJson(j);
    EXPECT_TRUE(loadout.has_value());
    thread_pool::ThreadPoolConfigLoader loader = std::move(*loadout);

    const auto cfg = loader.GetConfig();
    EXPECT_LE(cfg.scale_down_threshold, cfg.scale_up_threshold);
    EXPECT_EQ(cfg.queue_policy, thread_pool::QueueFullPolicy::Block);
}

namespace fs = std::filesystem;

TEST(ConfigLoader, FromFile) {
    fs::path path;
#ifdef TEST_SOURCE_DIR
    path = fs::path(TEST_SOURCE_DIR) / "test_config" / "sample_config.json";
#else
    fs::path here = fs::path(__FILE__).parent_path();
    fs::path repo_test_dir = here.parent_path();
    path = repo_test_dir / "test_config" / "sample_config.json";
#endif

    auto loadout = thread_pool::ThreadPoolConfigLoader::FromFile(path.string());
    ASSERT_TRUE(loadout.has_value());
    thread_pool::ThreadPoolConfigLoader loader = std::move(*loadout);

    const auto cfg = loader.GetConfig();
    EXPECT_EQ(cfg.queue_cap, 2048u);
    EXPECT_EQ(cfg.core_threads, 4u);
    EXPECT_EQ(cfg.max_threads, 6u);
    EXPECT_EQ(cfg.pending_low, 8u);
    EXPECT_EQ(cfg.pending_hi, 64u);
    EXPECT_EQ(cfg.queue_policy, thread_pool::QueueFullPolicy::Overwrite);

    auto dumped = loader.Dump();
    EXPECT_NE(dumped.find("Overwrite"), std::string::npos);
}