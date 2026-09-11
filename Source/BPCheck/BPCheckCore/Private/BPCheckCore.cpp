// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckCore.h"

#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckCore);

void FBPCheckCoreModule::StartupModule()
{
	UE_LOG(LogBPCheckCore, Log, TEXT("BPCheck-Core started."));
}

void FBPCheckCoreModule::ShutdownModule()
{

}

void FPrintInHotPathRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions)
{
	static UFunction* PrintStringFn = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));

	if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
	{
		if (Call->GetTargetFunction() == PrintStringFn)
		{
			// TODO(Seb): Check with Context
			// if (Context.IsInHotPath(Call->NodeGuid))

			Suggestions.Push({
				ESuggestion::PrintInHotPath,
				Call->GetNodeTitle(ENodeTitleType::FullTitle).ToString()
				});
		}
	}
}

FAnalyzer::FAnalyzer()
{
	Rules.Push(MakeUnique<FPrintInHotPathRule>());
}

FAnalyzerResult FAnalyzer::Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	return Analyze(CompilationContext.Blueprint);
}

FAnalyzerResult FAnalyzer::Analyze(const UBlueprint* Blueprint)
{
	UE_LOG(LogBPCheckCore, Log, TEXT("Analyze %s"), *Blueprint->GetName());

	TArray<FSuggestionContext> Suggestions;
	for (auto& Page : Blueprint->UbergraphPages)
	{
		for (auto Node : Page->Nodes)
		{
			if (IsDead(Node)) continue;

			for (const auto& Rule : Rules)
			{
				Rule->Check(Node, Suggestions);
			}
		}
	}

	TArray<FString> Descriptions;
	for (auto& SuggestionContext : Suggestions)
	{
		auto& Info = GetSuggestionInfo(SuggestionContext.Suggestion);
		Descriptions.Push(FString::Printf(TEXT("(%s) %s"), *Info.SuggestionCode, *Info.SuggestionDescription));
	}

	return Ok(Blueprint, FString::Join(Descriptions, TEXT(", ")));
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

bool FAnalyzer::IsDead(const UEdGraphNode* Node)
{
	// TODO(Seb): This function is reliable for a single node, but breaks for chains. I think this should be part of the context-build in the future.
	for (auto Pin : Node->Pins) {
		if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return Pin->LinkedTo.Num() == 0;
		}
	}

	return false;
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