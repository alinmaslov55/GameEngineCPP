#include <gtest/gtest.h>
#include "Engine/Scene/ComponentMask128.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"

#include <vector>
#include <string>

namespace {

// Template dummy component generator to create up to 90+ distinct component types
template<size_t N>
class DummyComponent : public Engine::Component {
public:
    static constexpr size_t Index = N;
};

// Helper macro to register dummy component types
#define REGISTER_DUMMY(N) \
    masks[N] = Engine::Detail::GetComponentTypeMask<DummyComponent<N>>();

TEST(TagsAndLayersTest, ComponentMask128BitwiseOperations) {
    Engine::ComponentMask128 empty;
    EXPECT_EQ(empty.lo, 0u);
    EXPECT_EQ(empty.hi, 0u);
    EXPECT_TRUE(empty.IsEmpty());
    EXPECT_FALSE(static_cast<bool>(empty));

    // Bit positions across 64-bit boundary
    Engine::ComponentMask128 b0 = Engine::ComponentMask128::FromBit(0);
    EXPECT_EQ(b0.lo, 1ULL);
    EXPECT_EQ(b0.hi, 0ULL);
    EXPECT_TRUE(static_cast<bool>(b0));

    Engine::ComponentMask128 b63 = Engine::ComponentMask128::FromBit(63);
    EXPECT_EQ(b63.lo, 1ULL << 63);
    EXPECT_EQ(b63.hi, 0ULL);

    Engine::ComponentMask128 b64 = Engine::ComponentMask128::FromBit(64);
    EXPECT_EQ(b64.lo, 0ULL);
    EXPECT_EQ(b64.hi, 1ULL);

    Engine::ComponentMask128 b127 = Engine::ComponentMask128::FromBit(127);
    EXPECT_EQ(b127.lo, 0ULL);
    EXPECT_EQ(b127.hi, 1ULL << 63);

    Engine::ComponentMask128 b128 = Engine::ComponentMask128::FromBit(128);
    EXPECT_TRUE(b128.IsEmpty());

    // Bitwise OR, AND, XOR, NOT
    Engine::ComponentMask128 combined = b0 | b64;
    EXPECT_EQ(combined.lo, 1ULL);
    EXPECT_EQ(combined.hi, 1ULL);
    EXPECT_TRUE(combined.Intersects(b0));
    EXPECT_TRUE(combined.Intersects(b64));
    EXPECT_FALSE(combined.Intersects(b63));
    EXPECT_FALSE(combined.Intersects(b127));

    Engine::ComponentMask128 masked = combined & b64;
    EXPECT_EQ(masked, b64);
    EXPECT_NE(masked, b0);

    Engine::ComponentMask128 xored = combined ^ b0;
    EXPECT_EQ(xored, b64);

    Engine::ComponentMask128 inverted = ~b0;
    EXPECT_EQ(inverted.lo, ~1ULL);
    EXPECT_EQ(inverted.hi, ~0ULL);
}

template<size_t... Is>
void PopulateDummyMasks(std::vector<Engine::ComponentMask128>& masks, std::index_sequence<Is...>) {
    ((masks[Is] = Engine::Detail::GetComponentTypeMask<DummyComponent<Is>>()), ...);
}

TEST(TagsAndLayersTest, EightyDistinctComponentsZeroMaskCollision) {
    constexpr size_t kNumComponents = 80;
    std::vector<Engine::ComponentMask128> masks(kNumComponents);

    PopulateDummyMasks(masks, std::make_index_sequence<kNumComponents>{});

    // 1. Verify every component received a valid, single-bit mask
    for (size_t i = 0; i < kNumComponents; ++i) {
        EXPECT_FALSE(masks[i].IsEmpty());
        // Verify exactly one bit is set across the 128-bit structure
        uint32_t setBits = 0;
        for (int b = 0; b < 64; ++b) {
            if ((masks[i].lo >> b) & 1ULL) ++setBits;
            if ((masks[i].hi >> b) & 1ULL) ++setBits;
        }
        EXPECT_EQ(setBits, 1u) << "Component " << i << " did not get exactly one bit set";
    }

    // 2. Verify all 80 masks are mutually disjoint (zero bit collisions)
    for (size_t i = 0; i < kNumComponents; ++i) {
        for (size_t j = i + 1; j < kNumComponents; ++j) {
            EXPECT_FALSE(masks[i].Intersects(masks[j]))
                << "Collision detected between component " << i << " and " << j;
            EXPECT_NE(masks[i], masks[j]);
        }
    }

    // 3. Test GameObject HasComponent with components across the 64-bit boundary
    Engine::GameObject entity("MultiComponentEntity");

    EXPECT_FALSE(entity.HasComponent<DummyComponent<0>>());
    EXPECT_FALSE(entity.HasComponent<DummyComponent<63>>());
    EXPECT_FALSE(entity.HasComponent<DummyComponent<64>>());
    EXPECT_FALSE(entity.HasComponent<DummyComponent<79>>());

    entity.AddComponent<DummyComponent<0>>();
    entity.AddComponent<DummyComponent<63>>();
    entity.AddComponent<DummyComponent<64>>();
    entity.AddComponent<DummyComponent<79>>();

    EXPECT_TRUE(entity.HasComponent<DummyComponent<0>>());
    EXPECT_TRUE(entity.HasComponent<DummyComponent<63>>());
    EXPECT_TRUE(entity.HasComponent<DummyComponent<64>>());
    EXPECT_TRUE(entity.HasComponent<DummyComponent<79>>());

    // Components not added must return false
    EXPECT_FALSE(entity.HasComponent<DummyComponent<1>>());
    EXPECT_FALSE(entity.HasComponent<DummyComponent<62>>());
    EXPECT_FALSE(entity.HasComponent<DummyComponent<65>>());
    EXPECT_FALSE(entity.HasComponent<DummyComponent<78>>());

    // 4. Test Component removal clears only the intended bit
    EXPECT_TRUE(entity.RemoveComponent<DummyComponent<64>>());
    EXPECT_FALSE(entity.HasComponent<DummyComponent<64>>());
    EXPECT_TRUE(entity.HasComponent<DummyComponent<0>>());
    EXPECT_TRUE(entity.HasComponent<DummyComponent<63>>());
    EXPECT_TRUE(entity.HasComponent<DummyComponent<79>>());
}

TEST(TagsAndLayersTest, EntityTagSceneQueries) {
    Engine::Scene scene("TagQueryScene");

    auto* player = scene.CreateGameObject("Hero");
    player->SetTag(Engine::EntityTag::Player);

    auto* enemy1 = scene.CreateGameObject("Ghoul_1");
    enemy1->SetTag(Engine::EntityTag::Enemy);
    auto* enemy2 = scene.CreateGameObject("Ghoul_2");
    enemy2->SetTag(Engine::EntityTag::Enemy);
    auto* enemy3 = scene.CreateGameObject("Ghoul_3");
    enemy3->SetTag(Engine::EntityTag::Enemy);

    auto* prop1 = scene.CreateGameObject("Crate_1");
    prop1->SetTag(Engine::EntityTag::DestructibleProp);
    auto* prop2 = scene.CreateGameObject("Crate_2");
    prop2->SetTag(Engine::EntityTag::DestructibleProp);

    auto* untagged = scene.CreateGameObject("Doodad");
    EXPECT_EQ(untagged->GetTag(), Engine::EntityTag::Untagged);

    // 1. FindFirstGameObjectWithTag
    EXPECT_EQ(scene.FindFirstGameObjectWithTag(Engine::EntityTag::Player), player);
    EXPECT_EQ(scene.FindFirstGameObjectWithTag(Engine::EntityTag::Shrine), nullptr);

    // 2. FindGameObjectsWithTag (out vector & return vector overloads)
    std::vector<Engine::GameObject*> enemies;
    scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy, enemies);
    EXPECT_EQ(enemies.size(), 3u);

    auto props = scene.FindGameObjectsWithTag(Engine::EntityTag::DestructibleProp);
    EXPECT_EQ(props.size(), 2u);

    // 3. Inactive entities excluded from tag queries
    enemy1->SetActive(false);
    scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy, enemies);
    EXPECT_EQ(enemies.size(), 2u);

    // 4. Pending-destroy entities excluded
    scene.DestroyGameObject(enemy2);
    scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy, enemies);
    EXPECT_EQ(enemies.size(), 1u);
    EXPECT_EQ(enemies[0], enemy3);

    // 5. String conversions
    EXPECT_STREQ(Engine::EntityTagToString(Engine::EntityTag::Player), "Player");
    EXPECT_STREQ(Engine::EntityTagToString(Engine::EntityTag::SafeZoneBrazier), "SafeZoneBrazier");
    EXPECT_EQ(Engine::StringToEntityTag("Enemy"), Engine::EntityTag::Enemy);
    EXPECT_EQ(Engine::StringToEntityTag("NonExistent"), Engine::EntityTag::Untagged);
}

TEST(TagsAndLayersTest, EntityLayerQueries) {
    Engine::Scene scene("LayerQueryScene");

    auto* hero = scene.CreateGameObject("Hero");
    hero->SetLayer(Engine::EntityLayer::Player | Engine::EntityLayer::PlayerHitbox);

    auto* tree = scene.CreateGameObject("PineTree");
    tree->SetLayer(Engine::EntityLayer::Environment);

    auto* fence = scene.CreateGameObject("LowFence");
    fence->SetLayer(Engine::EntityLayer::LowObstacle);

    // Layer checks on GameObject
    EXPECT_TRUE(hero->IsInLayer(Engine::EntityLayer::Player));
    EXPECT_TRUE(hero->IsInLayer(Engine::EntityLayer::PlayerHitbox));
    EXPECT_FALSE(hero->IsInLayer(Engine::EntityLayer::Enemy));
    EXPECT_TRUE(hero->IsInLayer(Engine::EntityLayer::Player | Engine::EntityLayer::Enemy));

    // Scene layer queries
    std::vector<Engine::GameObject*> envObjects;
    scene.FindGameObjectsInLayer(Engine::EntityLayer::Environment, envObjects);
    EXPECT_EQ(envObjects.size(), 1u);
    EXPECT_EQ(envObjects[0], tree);

    std::vector<Engine::GameObject*> playerLayerObjects;
    scene.FindGameObjectsInLayer(Engine::EntityLayer::Player, playerLayerObjects);
    EXPECT_EQ(playerLayerObjects.size(), 1u);
    EXPECT_EQ(playerLayerObjects[0], hero);
}

TEST(TagsAndLayersTest, SceneSerializerTagAndLayerRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>("SerializerTagTestScene");

    auto* hero = sourceScene->CreateGameObject("Hero");
    hero->SetTag(Engine::EntityTag::Player);
    hero->SetLayer(Engine::EntityLayer::Player | Engine::EntityLayer::PlayerHitbox);

    auto* brazier = sourceScene->CreateGameObject("SanctuaryBrazier");
    brazier->SetTag(Engine::EntityTag::SafeZoneBrazier);
    brazier->SetLayer(Engine::EntityLayer::Environment | Engine::EntityLayer::LowObstacle);

    Engine::SceneSerializer serializer(sourceScene);
    std::string jsonStr = serializer.SerializeToString();

    // Verify string JSON contains human-readable tag names
    EXPECT_NE(jsonStr.find("\"Tag\": \"Player\""), std::string::npos);
    EXPECT_NE(jsonStr.find("\"Tag\": \"SafeZoneBrazier\""), std::string::npos);

    // Deserialize into cloned scene
    auto clonedScene = std::make_shared<Engine::Scene>();
    Engine::SceneSerializer cloner(clonedScene);
    ASSERT_TRUE(cloner.DeserializeFromString(jsonStr));

    auto* clonedHero = clonedScene->FindFirstGameObjectWithTag(Engine::EntityTag::Player);
    ASSERT_NE(clonedHero, nullptr);
    EXPECT_EQ(clonedHero->GetName(), "Hero");
    EXPECT_EQ(clonedHero->GetTag(), Engine::EntityTag::Player);
    EXPECT_EQ(clonedHero->GetLayer(), Engine::EntityLayer::Player | Engine::EntityLayer::PlayerHitbox);

    auto* clonedBrazier = clonedScene->FindFirstGameObjectWithTag(Engine::EntityTag::SafeZoneBrazier);
    ASSERT_NE(clonedBrazier, nullptr);
    EXPECT_EQ(clonedBrazier->GetName(), "SanctuaryBrazier");
    EXPECT_EQ(clonedBrazier->GetTag(), Engine::EntityTag::SafeZoneBrazier);
    EXPECT_EQ(clonedBrazier->GetLayer(), Engine::EntityLayer::Environment | Engine::EntityLayer::LowObstacle);

    // Backward compatibility: deserialize legacy JSON without "Tag" or "LayerMask"
    std::string legacyJson = R"({
        "SceneName": "LegacyScene",
        "GameObjects": [
            {
                "Name": "OldObject",
                "UUID": 999999999
            }
        ]
    })";

    auto legacyScene = std::make_shared<Engine::Scene>();
    Engine::SceneSerializer legacyCloner(legacyScene);
    ASSERT_TRUE(legacyCloner.DeserializeFromString(legacyJson));
    ASSERT_EQ(legacyScene->GetGameObjects().size(), 1u);
    auto* oldObj = legacyScene->GetGameObjects()[0].get();
    EXPECT_EQ(oldObj->GetTag(), Engine::EntityTag::Untagged);
    EXPECT_EQ(oldObj->GetLayer(), Engine::EntityLayer::Default);
}

} // namespace
