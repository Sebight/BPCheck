// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckCore.h"

#include "K2Node_DynamicCast.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckCore);

void FBPCheckCoreModule::StartupModule()
{
	UE_LOG(LogBPCheckCore, Log, TEXT("BPCheck-Core started."));
}

void FBPCheckCoreModule::ShutdownModule()
{

}

FAnalyzerResult FAnalyzer::Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	return Analyze(CompilationContext.Blueprint);
}

FAnalyzerResult FAnalyzer::Analyze(const UBlueprint* Blueprint)
{
	UE_LOG(LogBPCheckCore, Log, TEXT("Analyze %s"), *Blueprint->GetName());

	UEdGraphNode* TickNode = FindTick(Blueprint);

	TArray<FString> Suggestions;

	if (!TickNode) return Fail(Blueprint, "Tick not found.");
	TFunction<void(UEdGraphNode*)> WalkNode;
	WalkNode = [&WalkNode, &Suggestions](UEdGraphNode* Node)
		{
			UE_LOG(LogBPCheckCore, Log, TEXT("Walked: %s(%s)"), *Node->GetName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

			if (Cast<UK2Node_DynamicCast>(Node))
			{
				Suggestions.Push("Using cast.");
			}

			auto NextNodes = GetNextExecNode(Node);

			for (auto& NextNode : NextNodes)
			{
				WalkNode(NextNode);
			}
		};

	UEdGraphNode* Current = TickNode;
	WalkNode(Current);

	return Ok(Blueprint, FString::Join(Suggestions, TEXT(", ")));
}

UEdGraphNode* FAnalyzer::FindTick(const UBlueprint* Blueprint)
{
	for (auto& Graph : Blueprint->UbergraphPages)
	{
		for (auto& Node : Graph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				if (EventNode->GetFunctionName() == "ReceiveTick") return Node;
			}
		}
	}
	return nullptr;
}

TArray<UEdGraphNode*> FAnalyzer::GetNextExecNode(const UEdGraphNode* CurrentNode)
{
	TArray<UEdGraphNode*> Out;

	for (auto Pin : CurrentNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			auto LinkedToCount = Pin->LinkedTo.Num();

			if (LinkedToCount == 0) continue;

			// Note(Seb): I think assuming 0 here is okay, since the output execution pin can only go to one?
			Out.Push(Pin->LinkedTo[0]->GetOwningNode());
		}
	}

	return Out;
}

FAnalyzerResult FAnalyzer::Fail(const UBlueprint* Blueprint, const FString& Reason)
{
	FAnalyzerResult Result;
	Result.BlueprintName = Blueprint->GetName();
	Result.Success = false;
	Result.Message = Reason.IsEmpty() ? "unknown reason" : Reason;

	return Result;
}

FAnalyzerResult FAnalyzer::Ok(const UBlueprint* Blueprint, const FString& Message)
{
	FAnalyzerResult Result;
	Result.BlueprintName = Blueprint->GetName();
	Result.Success = true;
	Result.Message = Message.IsEmpty() ? "unknown reason" : Message;

	return Result;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckCoreModule, BPCheckCore)