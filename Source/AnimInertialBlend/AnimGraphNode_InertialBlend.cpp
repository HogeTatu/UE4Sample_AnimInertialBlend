// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimGraphNode_InertialBlend.h"

#define LOCTEXT_NAMESPACE "A3Nodes"

UAnimGraphNode_InertialBlend::UAnimGraphNode_InertialBlend(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FLinearColor UAnimGraphNode_InertialBlend::GetNodeTitleColor() const
{
	return FLinearColor(0.823f, 0.867f, 0.914f);
}

FText UAnimGraphNode_InertialBlend::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("AnimGraphNodeInertialBlend_Title", "Inertial Blend");
}

FText UAnimGraphNode_InertialBlend::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNodeInertialBlend_Tooltip", "Inertial Blend");
}

FString UAnimGraphNode_InertialBlend::GetNodeCategory() const
{
	return TEXT("Blends");
}

#undef LOCTEXT_NAMESPACE
