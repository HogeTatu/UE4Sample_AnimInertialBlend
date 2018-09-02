// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"
#include "BoneIndices.h"
#include "AnimNode_InertialBlend.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct FAnimNode_InertialBlend : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	/** Aポーズ */
	UPROPERTY(EditAnywhere, Category=Links)
	FPoseLink A;

	/** Bポーズ */
	UPROPERTY(EditAnywhere, Category=Links)
	FPoseLink B;

	/** Aポーズアクティブフラグ */
	UPROPERTY(EditAnywhere, Category=Runtime, meta=(AlwaysAsPin))
	bool bAIsRelevant;

	/** ブレンド時間 */
	UPROPERTY(EditAnywhere, Category=Runtime, meta=(AlwaysAsPin))
	float BlendTime;

	/** ブレンド開始時に遷移先ポーズを初期化するか */
	UPROPERTY(EditAnywhere, Category=Option)
	bool bResetChildOnActivation;

private:
	struct FTransitionBase
	{
		float X0;
		float V0;
		float T1;
		float A0;
		float A;
		float B;
		float C;
	};

	struct FVectorTransition : public FTransitionBase
	{
		FVector NormalizedVecX0;
	};

	struct FQuatTransition : public FTransitionBase
	{
		FVector AxisX0;
	};

	struct FTransformTransition
	{
		/** 位置遷移情報 */
		FVectorTransition Translation;

		/** 回転遷移情報 */
		FQuatTransition Rotation;

		/** スケール遷移情報 */
		FVectorTransition Scale;
	};

	struct FPoseTransition
	{
		/** 各ボーンの Transform 遷移情報 */
		TArray<FTransformTransition> Transforms;

		/** 現在時間 */
		float T;
	};

	/** ポーズ遷移情報 */
	TSharedPtr<FPoseTransition> PoseTransition;

private:
	struct FPoseCache
	{
		/** 有効フラグ */
		bool bEnabled;

		/** 各ボーンの Transform 情報 */
		TArray<FTransform> Transforms;

		/** 経過時間 */
		float DeltaSeconds;
	};

	FPoseCache Caches[2];

	/** 現在フレーム -1 ポーズキャッシュ */
	FPoseCache* PrimaryCache;

	/** 現在フレーム -2 ポーズキャッシュ */
	FPoseCache* SecondaryCache;

private:
	bool bAIsRelevantLast;
	bool bRequestedTransition;

public:
	FAnimNode_InertialBlend();

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

private:
	/**
	 * 遷移情報を初期化（ベクトル）
	 * @param Transition 遷移情報
	 * @param RequestedValue 遷移先ポーズ値
	 * @param PrimaryCacheValue 現在フレーム -1 ポーズ値
	 * @param SecondaryCacheValue 現在フレーム -2 ポーズ値
	 * @param PrimaryToSecondaryDeltaSeconds 現在フレーム -2 から 現在フレーム -1 の差分時間
	 */
	void InitTransition(
		FVectorTransition& Transition,
		const FVector& RequestedValue,
		const FVector& PrimaryCacheValue,
		const FVector& SecondaryCacheValue,
		float PrimaryToSecondaryDeltaSeconds
	);
	
	/**
	 * 遷移情報を初期化（クォータニオン）
	 * @param Transition 遷移情報
	 * @param RequestedValue 遷移先ポーズ値
	 * @param PrimaryCacheValue 現在フレーム -1 ポーズ値
	 * @param SecondaryCacheValue 現在フレーム -2 ポーズ値
	 * @param PrimaryToSecondaryDeltaSeconds 現在フレーム -2 から 現在フレーム -1 の差分時間
	 */
	void InitTransition(
		FQuatTransition& Transition,
		const FQuat& RequestedValue,
		const FQuat& PrimaryCacheValue,
		const FQuat& SecondaryCacheValue,
		float PrimaryToSecondaryDeltaSeconds
	);
	
	/**
	 * 遷移情報を初期化（共通）
	 * @param Transition 遷移情報
	 */
	void InitTransition(FTransitionBase& Transition);
	
	/**
	 * ブレンドされた値を取得（ベクトル）
	 * @param CurrentValue 現在値
	 * @param Transition 遷移情報
	 * @param T 現在時間
	 */
	FVector CalcBlendedValue(const FVector& CurrentValue, const FVectorTransition& Transition, float T) const;
	
	/**
	 * ブレンドされた値を取得（クォータニオン）
	 * @param CurrentValue 現在値
	 * @param Transition 遷移情報
	 * @param T 現在時間
	 */
	FQuat CalcBlendedValue(const FQuat& CurrentValue, const FQuatTransition& Transition, float T) const;
	
	/**
	 * ブレンド値を取得（スカラー）
	 * @param Transition 遷移情報
	 * @param T 現在時間
	 */
	float CalcBlendValue(const FTransitionBase& Transition, float T) const;

	/**
	 * ポーズキャッシュ更新
	 * @param PoseContext 現在ポーズ情報
	 * @param DeltaSeconds 経過時間
	 */
	void UpdatePoseCache(FPoseContext& PoseContext, float DeltaSeconds);

private:
	static FORCEINLINE void NormalizeAngle(float& Rad)
	{
		while (Rad <= -PI) Rad += PI * 2.f;
		while (Rad >=  PI) Rad -= PI * 2.f;
	}

	static FORCEINLINE void NormalizeQuat(FQuat& Quat)
	{
		if (Quat.W < 0.f)
		{
			Quat.X = -Quat.X;
			Quat.Y = -Quat.Y;
			Quat.Z = -Quat.Z;
			Quat.W = -Quat.W;
		}
	}
};
