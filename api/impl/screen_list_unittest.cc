// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "api/impl/screen_list.h"

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace openscreen {

TEST(ScreenListTest, AddScreens) {
  ScreenList list;

  EXPECT_TRUE(list.GetScreens().empty());

  const ScreenInfo screen1{
      "id1", "name1", "eth0", {{192, 168, 1, 10}, 12345}, {{}, 0}};
  list.OnScreenAdded(screen1);

  ASSERT_EQ(1u, list.GetScreens().size());
  EXPECT_EQ(screen1, list.GetScreens()[0]);

  const ScreenInfo screen2{
      "id2", "name2", "eth0", {{192, 168, 1, 11}, 12345}, {{}, 0}};
  list.OnScreenAdded(screen2);

  ASSERT_EQ(2u, list.GetScreens().size());
  EXPECT_EQ(screen1, list.GetScreens()[0]);
  EXPECT_EQ(screen2, list.GetScreens()[1]);

  list.OnScreenAdded(screen1);

  // No duplicate checking.
  ASSERT_EQ(3u, list.GetScreens().size());
  EXPECT_EQ(screen1, list.GetScreens()[0]);
  EXPECT_EQ(screen2, list.GetScreens()[1]);
  EXPECT_EQ(screen1, list.GetScreens()[2]);
}

TEST(ScreenListTest, ChangeScreens) {
  ScreenList list;
  const ScreenInfo screen1{
      "id1", "name1", "eth0", {{192, 168, 1, 10}, 12345}, {{}, 0}};
  const ScreenInfo screen2{
      "id2", "name2", "eth0", {{192, 168, 1, 11}, 12345}, {{}, 0}};
  const ScreenInfo screen3{
      "id3", "name3", "eth0", {{192, 168, 1, 12}, 12345}, {{}, 0}};
  const ScreenInfo screen1_alt_name{
      "id1", "name1 alt", "eth0", {{192, 168, 1, 10}, 12345}, {{}, 0}};
  list.OnScreenAdded(screen1);
  list.OnScreenAdded(screen2);

  EXPECT_TRUE(list.OnScreenChanged(screen1_alt_name));
  EXPECT_FALSE(list.OnScreenChanged(screen3));

  ASSERT_EQ(2u, list.GetScreens().size());
  EXPECT_EQ(screen1_alt_name, list.GetScreens()[0]);
  EXPECT_EQ(screen2, list.GetScreens()[1]);
}

TEST(ScreenListTest, RemoveScreens) {
  ScreenList list;
  const ScreenInfo screen1{
      "id1", "name1", "eth0", {{192, 168, 1, 10}, 12345}, {{}, 0}};
  const ScreenInfo screen2{
      "id2", "name2", "eth0", {{192, 168, 1, 11}, 12345}, {{}, 0}};
  EXPECT_FALSE(list.OnScreenRemoved(screen1));
  list.OnScreenAdded(screen1);
  EXPECT_FALSE(list.OnScreenRemoved(screen2));
  list.OnScreenAdded(screen2);
  list.OnScreenAdded(screen1);

  EXPECT_TRUE(list.OnScreenRemoved(screen1));

  ASSERT_EQ(1u, list.GetScreens().size());
  EXPECT_EQ(screen2, list.GetScreens()[0]);
}

TEST(ScreenListTest, RemoveAllScreens) {
  ScreenList list;
  const ScreenInfo screen1{
      "id1", "name1", "eth0", {{192, 168, 1, 10}, 12345}, {{}, 0}};
  const ScreenInfo screen2{
      "id2", "name2", "eth0", {{192, 168, 1, 11}, 12345}, {{}, 0}};
  EXPECT_FALSE(list.OnAllScreensRemoved());
  list.OnScreenAdded(screen1);
  list.OnScreenAdded(screen2);

  EXPECT_TRUE(list.OnAllScreensRemoved());
  ASSERT_TRUE(list.GetScreens().empty());
}

}  // namespace openscreen
