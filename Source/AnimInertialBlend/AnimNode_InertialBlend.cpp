// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimNode_InertialBlend.h"
#include "Animation/AnimInstanceProxy.h"
#include "Kismet/KismetMathLibrary.h"

FAnimNode_InertialBlend::FAnimNode_InertialBlend()
	: bAIsRelevant(true)
	, BlendTime(0.4f)
	, bResetChildOnActivation(false)
	, bAIsRelevantLast(true)
	, bRequestedTransition(false)
{
	PrimaryCache = &Caches[0];
	SecondaryCache = &Caches[1];

	PrimaryCache->bEnabled = false;
	SecondaryCache->bEnabled = false;
}

void FAnimNode_InertialBlend::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);

	A.Initialize(Context);
	B.Initialize(Context);

	bAIsRelevantLast = bAIsRelevant;
}

void FAnimNode_InertialBlend::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	A.CacheBones(Context);
	B.CacheBones(Context);
}

void FAnimNode_InertialBlend::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	EvaluateGraphExposedInputs.Execute(Context);

	if (bAIsRelevant != bAIsRelevantLast)
	{
		bAIsRelevantLast = bAIsRelevant;
		bRequestedTransition = true;

		if (bResetChildOnActivation)
		{
			FAnimationInitializeContext ReinitializeContext(Context.AnimInstanceProxy);

			if (bAIsRelevant)
			{
				A.Initialize(ReinitializeContext);
			}
			else
			{
				B.Initialize(ReinitializeContext);
			}
		}
	}

	if (bAIsRelevant)
	{
		A.Update(Context);
	}
	else
	{
		B.Update(Context);
	}
}

void FAnimNode_InertialBlend::Evaluate_AnyThread(FPoseContext& Output)
{
	if (bAIsRelevant)
	{
		A.Evaluate(Output);
	}
	else
	{
		B.Evaluate(Output);
	}

	const auto& CurrentTransforms = Output.Pose.GetBones();
	float DeltaSeconds = Output.AnimInstanceProxy->GetDeltaSeconds();

	if (bRequestedTransition && PrimaryCache->bEnabled && SecondaryCache->bEnabled)
	{
		bRequestedTransition = false;

		PoseTransition = MakeShareable(new FPoseTransition);
		PoseTransition->Transforms.SetNumUninitialized(CurrentTransforms.Num());
		PoseTransition->T = 0.f;

		int32 BoneIndex = 0;
		for (FTransformTransition& TransformTransision : PoseTransition->Transforms)
		{
			const FTransform& CurrentTransform = CurrentTransforms[BoneIndex];
			const FTransform& PrimaryCacheTransform = PrimaryCache->Transforms[BoneIndex];
			const FTransform& SecondaryCacheTransform = SecondaryCache->Transforms[BoneIndex];

			InitTransition(
				TransformTransision.Translation,
				CurrentTransform.GetTranslation(),
				PrimaryCacheTransform.GetTranslation(),
				SecondaryCacheTransform.GetTranslation(),
				PrimaryCache->DeltaSeconds
			);

			InitTransition(
				TransformTransision.Rotation,
				CurrentTransform.GetRotation(),
				PrimaryCacheTransform.GetRotation(),
				SecondaryCacheTransform.GetRotation(),
				PrimaryCache->DeltaSeconds
			);

			InitTransition(
				TransformTransision.Scale,
				CurrentTransform.GetScale3D(),
				PrimaryCacheTransform.GetScale3D(),
				SecondaryCacheTransform.GetScale3D(),
				PrimaryCache->DeltaSeconds
			);

			++BoneIndex;
		}
	}

	if (PoseTransition.IsValid())
	{
		PoseTransition->T += DeltaSeconds;

		if (PoseTransition->T < BlendTime)
		{
			int32 BoneIndex = 0;
			for (FTransformTransition& TransformTransition : PoseTransition->Transforms)
			{
				const FTransform& CurrentTransform = CurrentTransforms[BoneIndex];

				FVector Translation = CalcBlendedValue(CurrentTransform.GetTranslation(), TransformTransition.Translation, PoseTransition->T);
				FQuat Rotation = CalcBlendedValue(CurrentTransform.GetRotation(), TransformTransition.Rotation, PoseTransition->T);
				FVector Scale = CalcBlendedValue(CurrentTransform.GetScale3D(), TransformTransition.Scale, PoseTransition->T);

				FCompactPoseBoneIndex CPBoneIndex(BoneIndex);
				Output.Pose[CPBoneIndex] = FTransform(Rotation, Translation, Scale);

				++BoneIndex;
			}

			Output.Pose.NormalizeRotations();
		}
		else
		{
			PoseTransition.Reset();
		}
	}

	UpdatePoseCache(Output, DeltaSeconds);
}

void FAnimNode_InertialBlend::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugData.AddDebugItem(DebugLine);

	A.GatherDebugData(DebugData);
	B.GatherDebugData(DebugData);
}

/**
 * 遷移情報を初期化（ベクトル）
 * @param Transition 遷移情報
 * @param RequestedValue 遷移先ポーズ値
 * @param PrimaryCacheValue 現在フレーム -1 ポーズ値
 * @param SecondaryCacheValue 現在フレーム -2 ポーズ値
 * @param PrimaryToSecondaryDeltaSeconds 現在フレーム -2 から 現在フレーム -1 の差分時間
 */
void FAnimNode_InertialBlend::InitTransition(
	FVectorTransition& Transition,
	const FVector& RequestedValue,
	const FVector& PrimaryCacheValue,
	const FVector& SecondaryCacheValue,
	float PrimaryToSecondaryDeltaSeconds)
{
	FVector VecX0 = PrimaryCacheValue - RequestedValue;
	Transition.X0 = VecX0.Size();


	if (Transition.X0 >= SMALL_NUMBER)
	{
		Transition.NormalizedVecX0 = VecX0 / Transition.X0;

		FVector VecXn1 = SecondaryCacheValue - RequestedValue;
		float Xn1 = FVector::DotProduct(VecXn1, Transition.NormalizedVecX0);

		Transition.V0 = (Transition.X0 - Xn1) / PrimaryToSecondaryDeltaSeconds;
	}
	else
	{
		Transition.NormalizedVecX0 = FVector::ZeroVector;
		Transition.V0 = 0.f;
	}

	InitTransition(Transition);
}

/**
 * 遷移情報を初期化（クォータニオン）
 * @param Transition 遷移情報
 * @param RequestedValue 遷移先ポーズ値
 * @param PrimaryCacheValue 現在フレーム -1 ポーズ値
 * @param SecondaryCacheValue 現在フレーム -2 ポーズ値
 * @param PrimaryToSecondaryDeltaSeconds 現在フレーム -2 から 現在フレーム -1 の差分時間
 */
void FAnimNode_InertialBlend::InitTransition(
	FQuatTransition& Transition,
	const FQuat& RequestedValue,
	const FQuat& PrimaryCacheValue,
	const FQuat& SecondaryCacheValue,
	float PrimaryToSecondaryDeltaSeconds)
{
	FQuat InvRequestedValue = RequestedValue.Inverse();

	FQuat Q0 = PrimaryCacheValue * InvRequestedValue;
	Q0.ToAxisAndAngle(Transition.AxisX0, Transition.X0);
	NormalizeAngle(Transition.X0);

	FQuat Qn1 = SecondaryCacheValue * InvRequestedValue;
	float Xn1 = PI;

	if (!FMath::IsNearlyZero(Qn1.W))
	{
		FVector Qxyz = FVector(Qn1.X, Qn1.Y, Qn1.Z);
		Xn1 = 2.f * FMath::Atan(FVector::DotProduct(Qxyz, Transition.AxisX0) / Qn1.W);
		NormalizeAngle(Xn1);
	}

	float DeltaAngle = Transition.X0 - Xn1;
	NormalizeAngle(DeltaAngle);

	Transition.V0 = DeltaAngle / PrimaryToSecondaryDeltaSeconds;

	InitTransition(Transition);
}

/**
 * 遷移情報を初期化（共通）
 * @param Transition 遷移情報
 */
void FAnimNode_InertialBlend::InitTransition(FTransitionBase& Transition)
{
	if (FMath::IsNearlyZero(Transition.X0))
	{
		Transition.T1 = 0.f;
		Transition.A0 = 0.f;
		Transition.A = 0.f;
		Transition.B = 0.f;
		Transition.C = 0.f;
	}
	else
	{
		if (Transition.V0 != 0.f && (Transition.X0 / Transition.V0) < 0.f)
		{
			Transition.T1 = FMath::Min(BlendTime, -5.f * Transition.X0 / Transition.V0);
		}
		else
		{
			Transition.T1 = BlendTime;
		}

		Transition.A0 = ((-8.f * Transition.V0 * Transition.T1) + (-20.f * Transition.X0)) / FMath::Pow(Transition.T1, 2.f);

		Transition.A  = -((1.f * Transition.A0 * Transition.T1 * Transition.T1) + ( 6.f * Transition.V0 * Transition.T1) + (12.f * Transition.X0)) / (2.f * FMath::Pow(Transition.T1, 5.f));
		Transition.B  =  ((3.f * Transition.A0 * Transition.T1 * Transition.T1) + (16.f * Transition.V0 * Transition.T1) + (30.f * Transition.X0)) / (2.f * FMath::Pow(Transition.T1, 4.f));
		Transition.C  = -((3.f * Transition.A0 * Transition.T1 * Transition.T1) + (12.f * Transition.V0 * Transition.T1) + (20.f * Transition.X0)) / (2.f * FMath::Pow(Transition.T1, 3.f));
	}
}

/**
 * ブレンドされた値を取得（ベクトル）
 * @param CurrentValue 現在値
 * @param Transition 遷移情報
 * @param T 現在時間
 */
FVector FAnimNode_InertialBlend::CalcBlendedValue(const FVector& CurrentValue, const FVectorTransition& Transition, float T) const
{
	float Xt = CalcBlendValue(Transition, T);
	return Xt * Transition.NormalizedVecX0 + CurrentValue;
}

/**
 * ブレンドされた値を取得（クォータニオン）
 * @param CurrentValue 現在値
 * @param Transition 遷移情報
 * @param T 現在時間
 */
FQuat FAnimNode_InertialBlend::CalcBlendedValue(const FQuat& CurrentValue, const FQuatTransition& Transition, float T) const
{
	float Xt = CalcBlendValue(Transition, T);
	return FQuat(Transition.AxisX0, Xt) * CurrentValue;
}

/**
 * ブレンド値を取得（スカラー）
 * @param Transition 遷移情報
 * @param T 現在時間
 */
float FAnimNode_InertialBlend::CalcBlendValue(const FTransitionBase& Transition, float T) const
{
	float T_1 = FMath::Min(T, Transition.T1);
	float T_2 = T_1 * T_1;
	float T_3 = T_1 * T_2;
	float T_4 = T_1 * T_3;
	float T_5 = T_1 * T_4;

	return
		(Transition.A * T_5) +
		(Transition.B * T_4) +
		(Transition.C * T_3) +
		(0.5f * Transition.A0 * T_2) +
		(Transition.V0 * T_1) +
		(Transition.X0);
}

/**
 * ポーズキャッシュ更新
 * @param PoseContext 現在ポーズ情報
 * @param DeltaSeconds 経過時間
 */
void FAnimNode_InertialBlend::UpdatePoseCache(FPoseContext& PoseContext, float DeltaSeconds)
{
	FPoseCache* Temp = PrimaryCache;
	PrimaryCache = SecondaryCache;
	SecondaryCache = Temp;

	PrimaryCache->bEnabled = true;
	PoseContext.Pose.CopyBonesTo(PrimaryCache->Transforms);
	PrimaryCache->DeltaSeconds = DeltaSeconds;
}
