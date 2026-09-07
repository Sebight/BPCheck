// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckEditor.h"
#include "ToolMenus.h"
#include "BPCheckCore.h"
#include "BlueprintCompilationManager.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckEditor);

void FBPCheckEditorModule::StartupModule()
{
	UE_LOG(LogBPCheckEditor, Log, TEXT("BPCheck-Editor started."));
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBPCheckEditorModule::CreateMenuItem));

	UBPCompilerExtension* Extension = NewObject<UBPCompilerExtension>();
	FBlueprintCompilationManager::RegisterCompilerExtension(UBlueprint::StaticClass(), Extension);
}

void FBPCheckEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FBPCheckEditorModule::CreateMenuItem()
{
	FToolMenuOwnerScoped Owner(this);

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& Section = ToolBar->FindOrAddSection("BPCheck");

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"BPCheckButton",
		FUIAction(FExecuteAction::CreateRaw(this, &FBPCheckEditorModule::OnButtonClicked)),
		LOCTEXT("BPCheckLabel", "BP Check"),
		LOCTEXT("BPCheckTooltip", "Run blueprint checks"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.ViewOptions")
	));
}

void FBPCheckEditorModule::OnButtonClicked()
{
}

void UBPCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	if (!CompilationContext.Blueprint->GetPackage()->GetName().StartsWith("/Game/"))
	{
		// This skips analysis of any engine side Blueprints
		// TODO(Seb): This also skips all plugins, which might be an issue
		return;
	}

	FAnalyzer::Analyze(CompilationContext, Data);
	Super::ProcessBlueprintCompiled(CompilationContext, Data);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckEditorModule, BPCheckEditor)