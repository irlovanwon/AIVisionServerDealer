/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Unit tests for ImageBuffer (lock-free SPSC, drop-newest)
 * Date: 20260623
 * Modification:
 */
#include "ai_vision/core/ImageBuffer.h"
#include <gtest/gtest.h>

using namespace ai_vision;

TEST(ImageBufferTest, PushPopBasic) {
    ImageBuffer buffer(10);
    auto img = std::make_shared<ImageData>();
    img->channel = "image";
    img->resolution = "1920,1080";
    buffer.push(img);

    auto result = buffer.pop(100);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->channel, "image");
    EXPECT_EQ(result->resolution, "1920,1080");
}

TEST(ImageBufferTest, DropNewestWhenFull) {
    ImageBuffer buffer(3);
    for (int i = 0; i < 5; ++i) {
        auto img = std::make_shared<ImageData>();
        img->pair_id = static_cast<uint64_t>(i);
        buffer.push(img);
    }
    EXPECT_EQ(buffer.size(), 3u);
    EXPECT_EQ(buffer.dropped(), 2u);

    auto first = buffer.pop(100);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->pair_id, 0u);
}

TEST(ImageBufferTest, PopTimeoutReturnsNull) {
    ImageBuffer buffer(10);
    auto result = buffer.pop(50);
    EXPECT_EQ(result, nullptr);
}

TEST(ImageBufferTest, TryPopEmpty) {
    ImageBuffer buffer(10);
    auto result = buffer.try_pop();
    EXPECT_EQ(result, nullptr);
}
