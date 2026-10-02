// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Suggestions.h"
#include "BlueprintCompilerExtension.h"
#include "BPCheckCore.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBPCheckCore, Log, All);

class FBPCheckCoreModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

USTRUCT()
struct FSuggestionContext
{
	GENERATED_BODY()

	ESuggestion Suggestion;
	FGuid NodeGuid;
};

UCLASS(config = BPCheck, DefaultConfig, meta = (DisplayName = "BPCheck"))
class BPCHECKCORE_API UBPCheckSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName()  const override { return TEXT("Plugins"); }
	virtual FName GetSectionName()   const override { return TEXT("BPCheck"); }

	UPROPERTY(Config, EditAnywhere, Category = "Suggestions")
	TMap<ESuggestion, bool> SuggestionEnabled;

	bool IsSuggestionEnabled(ESuggestion Suggestion) const;

	DECLARE_MULTICAST_DELEGATE(FOnBPCheckSettingsChanged);
	FOnBPCheckSettingsChanged OnSettingsChanged;
};

USTRUCT()
struct FAnalyzerResult
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSuggestionContext> Suggestions;

	UPROPERTY()
	bool Success = false;
};

USTRUCT()
struct FAnalyzerReport
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAnalyzerResult> Results;
};

enum class ENodeFlag
{
	InTick = 0,
	InALoop = 1
};

class FEffectResolver;
enum class EFunctionEffect;

class FAnalysisContext
{
public:
	void AddFlags(FGuid NodeGuid, TSet<ENodeFlag> Flags);
	bool HasFlag(FGuid NodeGuid, ENodeFlag Flag) const;

	void MarkNodeAlive(FGuid NodeGuid);

	bool IsDead(FGuid NodeGuid) const;
	TSet<EFunctionEffect> EffectsOf(UFunction* Fn) const;

private:
	friend class FAnalyzer;

	TSet<FGuid> AliveNodes;
	TMap<FGuid, TSet<ENodeFlag>> NodeData;
	FEffectResolver* EffectResolver = nullptr;
};

#pragma region Rules
class BPCHECKCORE_API FAnalysisRule
{
public:
	FAnalysisRule() = default;
	virtual ~FAnalysisRule() = default;
	virtual void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) = 0;
};

class BPCHECKCORE_API FExpensiveHotNodeRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

class BPCHECKCORE_API FCastRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

class BPCHECKCORE_API FDeadCodeRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

class BPCHECKCORE_API FLatentNodeInLoopRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};
#pragma endregion

struct FNextExecNode
{
	UEdGraphNode* Node;
	uint8 PinIndex;
};

enum class EFunctionEffect
{
	ExpensiveActorQuery = 0,
	Latent,
	DebugSideEffect
};

class FEffectResolver
{
public:
	TSet<EFunctionEffect> EffectsOf(UFunction* Fn);

	void ClearCache();
private:
	TMap<UFunction*, TSet<EFunctionEffect>> KnownEffects;
	TMap<UFunction*, TSet<EFunctionEffect>> EffectsCache;
	bool bKnownEffectsBuilt = false;

	void BuildKnownEffects();
	TSet<EFunctionEffect> ComputeBPFunctionEffects(UFunction* Fn);
	UEdGraph* FindFunctionGraph(UFunction* Fn);
};

class BPCHECKCORE_API FAnalyzer
{
public:
	FAnalyzer();
	FAnalyzer(const FAnalyzer&) = delete;
	FAnalyzer& operator=(const FAnalyzer&) = delete;
	FAnalyzer(FAnalyzer&&) = default;
	FAnalyzer& operator=(FAnalyzer&&) = default;

	FAnalyzerResult Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data);
	FAnalyzerResult Analyze(const UBlueprint* Blueprint);

	static bool IsNodePure(const UEdGraphNode* Node);
	static TArray<FNextExecNode> GetNextExecNode(const UEdGraphNode* CurrentNode);
private:
	// I think these could be static'd and moved into a namespace
	TSet<UEdGraphNode*> CollectEntryPoints(const UEdGraph* Graph) const;
	UEdGraphNode* FindTick(const UBlueprint* Blueprint);

	FAnalysisContext BuildContext(const UBlueprint* Blueprint);

private:
	TArray<TUniquePtr<FAnalysisRule>> Rules;
	FEffectResolver EffectResolver;
};
