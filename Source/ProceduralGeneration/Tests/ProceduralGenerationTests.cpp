#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProceduralGeneration/Utils/FastNoiseLite.h"
#include "ProceduralGeneration/Utils/ChunkMeshData.h"
#include "ProceduralGeneration/WorldGenerator.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace
{
	void ConfigureMainNoise(FastNoiseLite& N, int Seed)
	{
		N.SetSeed(Seed);
		N.SetFrequency(0.01f);
		N.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		N.SetFractalType(FastNoiseLite::FractalType_FBm);
		N.SetFractalOctaves(3);
		N.SetFractalLacunarity(2.0f);
		N.SetFractalGain(0.5f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoiseDeterminismTest,
	"ProceduralGeneration.Generation.NoiseDeterminism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNoiseDeterminismTest::RunTest(const FString& Parameters)
{
	FastNoiseLite A, B;
	ConfigureMainNoise(A, 1337);
	ConfigureMainNoise(B, 1337);

	for (int x = -64; x <= 64; x += 8)
	{
		for (int y = -64; y <= 64; y += 8)
		{
			const float VA = A.GetNoise((float)x, (float)y);
			const float VB = B.GetNoise((float)x, (float)y);
			TestEqual(TEXT("meme graine + meme position => meme valeur"), VA, VB);
			TestTrue(TEXT("domaine de sortie borne [-1, 1]"), VA >= -1.0f && VA <= 1.0f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoiseStatelessTest,
	"ProceduralGeneration.Generation.NoiseIsStateless",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNoiseStatelessTest::RunTest(const FString& Parameters)
{
	FastNoiseLite N;
	ConfigureMainNoise(N, 1337);

	const float Reference = N.GetNoise(12.0f, -34.0f);

	for (int i = 0; i < 50; ++i)
	{
		N.GetNoise((float)i * 7.0f, (float)i * -3.0f);
	}

	TestEqual(TEXT("l'ordre d'echantillonnage n'influence pas le resultat"),
		N.GetNoise(12.0f, -34.0f), Reference);

	FastNoiseLite Fresh;
	ConfigureMainNoise(Fresh, 1337);
	TestEqual(TEXT("instance neuve => meme valeur (reproductible entre sessions)"),
		Fresh.GetNoise(12.0f, -34.0f), Reference);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoiseSeedVarianceTest,
	"ProceduralGeneration.Generation.SeedVariance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNoiseSeedVarianceTest::RunTest(const FString& Parameters)
{
	FastNoiseLite A, B;
	ConfigureMainNoise(A, 1337);
	ConfigureMainNoise(B, 4242);

	bool bAnyDifference = false;
	for (int x = -64; x <= 64 && !bAnyDifference; x += 8)
		for (int y = -64; y <= 64 && !bAnyDifference; y += 8)
			if (!FMath::IsNearlyEqual(A.GetNoise((float)x, (float)y), B.GetNoise((float)x, (float)y)))
				bAnyDifference = true;

	TestTrue(TEXT("graines differentes => mondes differents"), bAnyDifference);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenerationDefaultsTest,
	"ProceduralGeneration.Generation.TunedDefaultsUnchanged",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenerationDefaultsTest::RunTest(const FString& Parameters)
{
	const AWorldGenerator* Defaults = GetDefault<AWorldGenerator>();
	TestNotNull(TEXT("classe par defaut accessible"), Defaults);
	if (!Defaults) return false;

	TestEqual(TEXT("Frequency"), Defaults->Frequency, 0.01f);
	TestEqual(TEXT("FractalOctaves"), Defaults->FractalOctaves, 3);
	TestEqual(TEXT("FractalLacunarity"), Defaults->FractalLacunarity, 2.0f);
	TestEqual(TEXT("FractalGain"), Defaults->FractalGain, 0.5f);

	TestTrue(TEXT("MaxChunksPerFrame >= 1 (le streaming ne peut pas se figer)"),
		Defaults->MaxChunksPerFrame >= 1);
	TestTrue(TEXT("InitialFillChunksPerFrame >= MaxChunksPerFrame (le burst depasse le repos)"),
		Defaults->InitialFillChunksPerFrame >= Defaults->MaxChunksPerFrame);
	TestTrue(TEXT("StreamRecomputeEveryNFrames >= 1"),
		Defaults->StreamRecomputeEveryNFrames >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOctreeKeyHashTest,
	"ProceduralGeneration.Octree.KeyEqualityAndHash",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOctreeKeyHashTest::RunTest(const FString& Parameters)
{
	FOctreeNodeKey K1; K1.X = 3; K1.Y = -7; K1.Z = 2; K1.S = 4;
	FOctreeNodeKey K2; K2.X = 3; K2.Y = -7; K2.Z = 2; K2.S = 4;
	FOctreeNodeKey K3; K3.X = 3; K3.Y = -7; K3.Z = 2; K3.S = 2;

	TestTrue(TEXT("cles identiques => egales"), K1 == K2);
	TestFalse(TEXT("echelle differente => non egales"), K1 == K3);
	TestEqual(TEXT("cles egales => hashs egaux"), GetTypeHash(K1), GetTypeHash(K2));

	TMap<FOctreeNodeKey, int32> Map;
	Map.Add(K1, 42);
	const int32* Found = Map.Find(K2);
	TestNotNull(TEXT("lookup TMap par cle equivalente"), Found);
	if (Found) TestEqual(TEXT("valeur retrouvee"), *Found, 42);

	Map.Add(K3, 7);
	TestEqual(TEXT("deux echelles au meme endroit = deux entrees distinctes"), Map.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOctreeKeyDistinctnessTest,
	"ProceduralGeneration.Octree.KeyDistinctness",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOctreeKeyDistinctnessTest::RunTest(const FString& Parameters)
{
	TSet<FOctreeNodeKey> Keys;
	int32 Expected = 0;

	for (int32 x = -2; x <= 2; ++x)
	for (int32 y = -2; y <= 2; ++y)
	for (int32 z = -1; z <= 1; ++z)
	for (int32 s = 1; s <= 8; s *= 2)
	{
		FOctreeNodeKey K; K.X = x; K.Y = y; K.Z = z; K.S = s;
		Keys.Add(K);
		++Expected;
	}

	TestEqual(TEXT("chaque (X,Y,Z,S) distinct => une entree distincte (pas de collision fatale)"),
		Keys.Num(), Expected);

	FOctreeNodeKey A; A.X = 1; A.Y = 0; A.Z = 0; A.S = 1;
	FOctreeNodeKey B; B.X = 0; B.Y = 1; B.Z = 0; B.S = 1;
	TestFalse(TEXT("permuter X et Y donne une cle differente"), A == B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOctreeKeySerializationTest,
	"ProceduralGeneration.Octree.KeySerializationRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOctreeKeySerializationTest::RunTest(const FString& Parameters)
{
	FOctreeNodeKey Written; Written.X = -12; Written.Y = 34; Written.Z = -5; Written.S = 8;

	FBufferArchive Writer;
	Writer << Written;

	FOctreeNodeKey Read;
	FMemoryReader Reader(Writer, true);
	Reader << Read;

	TestEqual(TEXT("X restitue"), Read.X, Written.X);
	TestEqual(TEXT("Y restitue"), Read.Y, Written.Y);
	TestEqual(TEXT("Z restitue"), Read.Z, Written.Z);
	TestEqual(TEXT("S restitue"), Read.S, Written.S);
	TestTrue(TEXT("cle relue identique a la cle ecrite"), Read == Written);
	TestEqual(TEXT("hash preserve par l'aller-retour"), GetTypeHash(Read), GetTypeHash(Written));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditStoreRoundTripTest,
	"ProceduralGeneration.Persistence.EditStoreRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FEditStoreRoundTripTest::RunTest(const FString& Parameters)
{
	int32 Version = 3;
	int32 Seed = 1337;
	int32 Size = 96;

	TMap<FIntPoint, TMap<int32, float>> EditStore;
	TMap<int32, float>& ChunkEdits = EditStore.Add(FIntPoint(2, -1));
	ChunkEdits.Add(153, -0.75f);
	ChunkEdits.Add(9001, 0.5f);

	TMap<FOctreeNodeKey, TMap<int32, float>> OctreeEditStore;
	FOctreeNodeKey Key; Key.X = 1; Key.Y = 0; Key.Z = -2; Key.S = 2;
	OctreeEditStore.Add(Key).Add(77, 0.25f);

	FBufferArchive Writer;
	Writer << Version << Seed << Size << EditStore << OctreeEditStore;

	int32 RVersion = 0, RSeed = 0, RSize = 0;
	TMap<FIntPoint, TMap<int32, float>> REditStore;
	TMap<FOctreeNodeKey, TMap<int32, float>> ROctreeEditStore;

	FMemoryReader Reader(Writer, true);
	Reader << RVersion << RSeed << RSize << REditStore << ROctreeEditStore;

	TestEqual(TEXT("version restituee"), RVersion, Version);
	TestEqual(TEXT("graine restituee"), RSeed, Seed);
	TestEqual(TEXT("taille restituee"), RSize, Size);

	const TMap<int32, float>* RChunk = REditStore.Find(FIntPoint(2, -1));
	TestNotNull(TEXT("chunk edite restitue"), RChunk);
	if (RChunk)
	{
		TestEqual(TEXT("nombre d'edits du chunk"), RChunk->Num(), 2);
		TestEqual(TEXT("valeur d'edit restituee"), RChunk->FindRef(153), -0.75f);
	}

	const TMap<int32, float>* RNode = ROctreeEditStore.Find(Key);
	TestNotNull(TEXT("noeud octree restitue"), RNode);
	if (RNode) TestEqual(TEXT("edit du noeud restitue"), RNode->FindRef(77), 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditStoreEmptyRoundTripTest,
	"ProceduralGeneration.Persistence.EmptyStoreRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FEditStoreEmptyRoundTripTest::RunTest(const FString& Parameters)
{
	int32 Version = 3;
	int32 Seed = 1337;
	int32 Size = 96;
	TMap<FIntPoint, TMap<int32, float>> EditStore;
	TMap<FOctreeNodeKey, TMap<int32, float>> OctreeEditStore;

	FBufferArchive Writer;
	Writer << Version << Seed << Size << EditStore << OctreeEditStore;

	int32 RVersion = 0, RSeed = 0, RSize = 0;
	TMap<FIntPoint, TMap<int32, float>> REditStore;
	TMap<FOctreeNodeKey, TMap<int32, float>> ROctreeEditStore;

	FMemoryReader Reader(Writer, true);
	Reader << RVersion << RSeed << RSize << REditStore << ROctreeEditStore;

	TestEqual(TEXT("version restituee sur monde vierge"), RVersion, Version);
	TestEqual(TEXT("aucun chunk edite"), REditStore.Num(), 0);
	TestEqual(TEXT("aucun noeud octree edite"), ROctreeEditStore.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkMeshDataClearTest,
	"ProceduralGeneration.Mesh.ChunkMeshDataClear",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FChunkMeshDataClearTest::RunTest(const FString& Parameters)
{
	FChunkMeshData Data;
	Data.Vertices.Add(FVector::ZeroVector);
	Data.Triangles.Add(0);
	Data.Normals.Add(FVector::UpVector);
	Data.Colors.Add(FColor::White);
	Data.UV0.Add(FVector2D::ZeroVector);

	Data.Clear();

	TestEqual(TEXT("sommets vides"), Data.Vertices.Num(), 0);
	TestEqual(TEXT("triangles vides"), Data.Triangles.Num(), 0);
	TestEqual(TEXT("normales vides"), Data.Normals.Num(), 0);
	TestEqual(TEXT("couleurs vides"), Data.Colors.Num(), 0);
	TestEqual(TEXT("UV vides"), Data.UV0.Num(), 0);

	Data.Vertices.Add(FVector::UpVector);
	TestEqual(TEXT("reutilisable apres Clear"), Data.Vertices.Num(), 1);
	return true;
}

#endif
