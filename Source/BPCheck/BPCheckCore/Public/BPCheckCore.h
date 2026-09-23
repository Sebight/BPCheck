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

class FAnalysisContext
{
public:
	void AddFlags(FGuid NodeGuid, TSet<ENodeFlag> Flags);
	bool HasFlag(FGuid NodeGuid, ENodeFlag Flag) const;

	void MarkNodeAlive(FGuid NodeGuid);

	bool IsDead(FGuid NodeGuid) const;
private:
	TSet<FGuid> AliveNodes;
	TMap<FGuid, TSet<ENodeFlag>> NodeData;
};

class BPCHECKCORE_API FAnalysisRule
{
public:
	FAnalysisRule() = default;
	virtual ~FAnalysisRule() = default;
	virtual void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) = 0;
};

class BPCHECKCORE_API FPrintInHotPathRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context) override;
};

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

private:
	// I think these could be static'd and moved into a namespace
	TSet<UEdGraphNode*> CollectEntryPoints(const UEdGraph* Graph) const;
	UEdGraphNode* FindTick(const UBlueprint* Blueprint);
	TArray<FNextExecNode> GetNextExecNode(const UEdGraphNode* CurrentNode);

	FAnalysisContext BuildContext(const UBlueprint* Blueprint);

private:
	TArray<TUniquePtr<FAnalysisRule>> Rules;
};
