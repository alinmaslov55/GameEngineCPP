#include <gtest/gtest.h>
#include "Engine/Scene/Tilemap.h"

namespace {

TEST(TilemapTest, InitializationAndBounds) {
    Engine::Tilemap tilemap(10, 8);
    EXPECT_EQ(tilemap.GetWidth(), 10u);
    EXPECT_EQ(tilemap.GetHeight(), 8u);

    // Default tile has id 0 and is walkable
    const auto& defaultTile = tilemap.GetTile(0, 0);
    EXPECT_EQ(defaultTile.id, 0u);
    EXPECT_TRUE(defaultTile.walkable);
}

TEST(TilemapTest, SetAndGetTile) {
    Engine::Tilemap tilemap(4, 4);

    tilemap.SetTile(2, 3, 42, false);
    const auto& tile = tilemap.GetTile(2, 3);
    EXPECT_EQ(tile.id, 42u);
    EXPECT_FALSE(tile.walkable);

    // Out of bounds SetTile should not crash
    tilemap.SetTile(10, 10, 99, true);
}

TEST(TileSetTest, AddAndRetrieveTiles) {
    Engine::TileSet tileSet;
    EXPECT_TRUE(tileSet.GetTiles().empty());

    // Looking up non-existent tile returns null
    EXPECT_EQ(tileSet.GetTileTexture(999), nullptr);

    // Add null texture or dummy pointer
    tileSet.AddTile(1, nullptr);
    EXPECT_EQ(tileSet.GetTiles().size(), 1u);
    EXPECT_EQ(tileSet.GetTileTexture(1), nullptr);
}

} // namespace

