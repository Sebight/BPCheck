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

	FSuggestionContext() = default;

	FSuggestionContext(ESuggestion S, FGuid Node)
	{
		Suggestion = S;
		NodeGuid = Node;
		Scope = ESuggestionScope::Node;
	}

	FSuggestionContext(ESuggestion S, TWeakObjectPtr<const UEdGraph> G)
	{
		Suggestion = S;
		Graph = G;
		Scope = ESuggestionScope::Graph;
	}

	FSuggestionContext(ESuggestion S, ESuggestionScope Sc)
	{
		Suggestion = S;
		Scope = Sc;
	}

	ESuggestion Suggestion = ESuggestion::None;
	ESuggestionScope Scope = ESuggestionScope::None;
	FGuid NodeGuid;
	TWeakObjectPtr<const UEdGraph> Graph;
	FString Subject;
};

USTRUCT()
struct FSuppression
{
	GENERATED_BODY()

	UPROPERTY() FSoftObjectPath Blueprint;
	// This can now also hold Guid of a Graph, so that's why the rename
	UPROPERTY() TOptional<FGuid> TargetGuid;
	UPROPERTY() ESuggestion Suggestion = ESuggestion::None;

	bool operator==(const FSuppression& Other) const
	{
		return Suggestion == Other.Suggestion && Blueprint == Other.Blueprint && ((TargetGuid.IsSet() && Other.TargetGuid.IsSet()) ? TargetGuid.GetValue() == Other.TargetGuid.GetValue() : true);
	}
};

FORCEINLINE uint32 GetTypeHash(const FSuppression& S)
{
	return HashCombine(HashCombine(GetTypeHash(S.Blueprint), GetTypeHash(S.TargetGuid)), GetTypeHash(S.Suggestion));
}

UCLASS(config = BPCheck, defaultconfig, meta = (DisplayName = "BPCheck"))
class BPCHECKCORE_API UBPCheckSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName()  const override { return TEXT("Plugins"); }
	virtual FName GetSectionName()   const override { return TEXT("BPCheck"); }
	void PruneDeadNodes(UBlueprint* Blueprint, const TArray<FGuid>& AliveNodes);

	UPROPERTY(Config, EditAnywhere, Category = "Thresholds", meta = (ClampMin = 1, ToolTip = "Graphs with more nodes than this are flagged."))
	int32 MaxGraphNodes = 300;

	UPROPERTY(Config, EditAnywhere, Category = "Thresholds", meta = (ClampMin = 1, ToolTip = "Functions with more arguments than this are flagged."))
	int32 MaxFunctionArguments = 7;

	UPROPERTY(Config, EditAnywhere, Category = "Suggestions")
	TMap<ESuggestion, bool> SuggestionEnabled;

	UPROPERTY(Config)
	TSet<FSuppression> Suppressions;

	void Suppress(const UBlueprint* Blueprint, TOptional<FGuid> NodeGuid, ESuggestion Suggestion);
	void Unsuppress(const FSuppression& Suppression);
	void DisableSuggestion(ESuggestion Suggestion);

	void ClearSuppressions();
	void ClearAll();

	bool IsSuppressed(const UBlueprint* Blueprint, TOptional<FGuid> NodeGuid, ESuggestion Suggestion) const;
	bool IsSuggestionEnabled(ESuggestion Suggestion) const;
	void SetSuggestionEnabled(ESuggestion S, bool bEnabled);

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

enum class EFunctionEffect
{
	ExpensiveActorQuery = 0,
	Latent,
	DebugSideEffect
};

struct FFunctionSummary
{
	TSet<EFunctionEffect> Effects;
	TMap<EFunctionEffect, TArray<FString>> StackTrace;
};

class FEffectResolver
{
public:
	FFunctionSummary EffectsOf(UFunction* Fn);

	void ClearCache();
private:
	TMap<UFunction*, FFunctionSummary> KnownEffects;
	TMap<UFunction*, FFunctionSummary> EffectsCache;
	bool bKnownEffectsBuilt = false;

	void BuildKnownEffects();
	FFunctionSummary ComputeBPFunctionEffects(UFunction* Fn);
	UEdGraph* FindFunctionGraph(UFunction* Fn);
};

class FAnalysisContext
{
public:
	void AddFlags(FGuid NodeGuid, TSet<ENodeFlag> Flags);
	bool HasFlag(FGuid NodeGuid, ENodeFlag Flag) const;

	void MarkNodeAlive(FGuid NodeGuid);

	bool IsDead(FGuid NodeGuid) const;
	FFunctionSummary EffectsOf(UFunction* Fn) const;

	const UBPCheckSettings* Settings = nullptr;

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

	virtual void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) {}
	virtual void Check(const UEdGraph* Graph, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) {}
	virtual void Check(const UBlueprint* Blueprint, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) {}
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

class BPCHECKCORE_API FGraphNodeCountRule : public FAnalysisRule
{
public:
	void Check(const UEdGraph* Graph, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

class BPCHECKCORE_API FFunctionArgumentRule : public FAnalysisRule
{
public:
	void Check(const UEdGraph* Graph, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

class BPCHECKCORE_API FEmptyEventHandlerRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

class BPCHECKCORE_API FLoopNestingRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

class BPCHECKCORE_API FRedundantBranchRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

class BPCHECKCORE_API FUnusedVariableRule : public FAnalysisRule
{
public:
	void Check(const UBlueprint* Blueprint, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};
#pragma endregion

struct FNextExecNode
{
	UEdGraphNode* Node;
	uint8 PinIndex;
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
