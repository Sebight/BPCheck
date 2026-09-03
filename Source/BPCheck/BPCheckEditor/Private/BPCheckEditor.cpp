// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckEditor.h"
#include "ToolMenus.h"
#include "BPCheckCore.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckEditor);

void FBPCheckEditorModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("BPCheck-Editor started."));
	UE_LOG(LogBPCheckEditor, Log, TEXT("BPCheck-Editor started."));
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBPCheckEditorModule::CreateMenuItem));
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
	FAnalyzer::Analyze();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckEditorModule, BPCheckEditor)