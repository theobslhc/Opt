#pragma once

#ifndef _PATCH_SOLVER_Stereo_EQUATIONS_
#define _PATCH_SOLVER_Stereo_EQUATIONS_

#include <cutil_inline.h>
#include <cutil_math.h>

#include "PatchSolverWarpingUtil.h"
#include "PatchSolverWarpingState.h"
#include "PatchSolverWarpingParameters.h"

////////////////////////////////////////
// evalF
////////////////////////////////////////

__inline__ __device__ float4 evalFDevice(unsigned int variableIdx, PatchSolverInput& input, PatchSolverState& state, PatchSolverParameters& parameters)
{
	float4 e = make_float4(0.0f, 0.0f, 0.0F, 0.0f);

	// E_fit
	float4 targetDepth = state.d_target[variableIdx];
	float4 e_fit = (state.d_x[variableIdx] - targetDepth);
	e += parameters.weightFitting * e_fit * e_fit;

	// E_reg
	int i; int j; get2DIdx(variableIdx, input.width, input.height, i, j);
	const int n0_i = i;		const int n0_j = j - 1; const bool validN0 = isInsideImage(n0_i, n0_j, input.width, input.height);
	const int n1_i = i;		const int n1_j = j + 1; const bool validN1 = isInsideImage(n1_i, n1_j, input.width, input.height);
	const int n2_i = i - 1; const int n2_j = j;		const bool validN2 = isInsideImage(n2_i, n2_j, input.width, input.height);
	const int n3_i = i + 1; const int n3_j = j;		const bool validN3 = isInsideImage(n3_i, n3_j, input.width, input.height);

	float4 p = state.d_x[get1DIdx(i, j, input.width, input.height)];
	float4 e_reg = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	if (validN0){ float4 q = state.d_x[get1DIdx(n0_i, n0_j, input.width, input.height)]; e_reg += (p - q)*(p - q); }
	if (validN1){ float4 q = state.d_x[get1DIdx(n1_i, n1_j, input.width, input.height)]; e_reg += (p - q)*(p - q); }
	if (validN2){ float4 q = state.d_x[get1DIdx(n2_i, n2_j, input.width, input.height)]; e_reg += (p - q)*(p - q); }
	if (validN3){ float4 q = state.d_x[get1DIdx(n3_i, n3_j, input.width, input.height)]; e_reg += (p - q)*(p - q); }
	e += parameters.weightRegularizer*e_reg;

	return e;
}

////////////////////////////////////////
// evalMinusJTF
////////////////////////////////////////

__inline__ __device__ float4 evalMinusJTFDevice(int tId_i, int tId_j, int gId_i, int gId_j, unsigned int W, unsigned int H, volatile float4* inTarget, volatile float4* inX, PatchSolverParameters& parameters, float4& outPre)
{
	float4 b   = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 pre = make_float4(0.0f, 0.0f, 0.0f, 0.0f);

	float4 X_CC = readValueFromCache2D(inX, tId_i, tId_j);	     
	float4 X_CM = readValueFromCache2D(inX, tId_i, tId_j - 1);
	float4 X_CP = readValueFromCache2D(inX, tId_i, tId_j + 1);
	float4 X_MC = readValueFromCache2D(inX, tId_i - 1, tId_j);
	float4 X_PC = readValueFromCache2D(inX, tId_i + 1, tId_j);

	const bool validN0 = isValid(X_CM);
	const bool validN1 = isValid(X_CP);
	const bool validN2 = isValid(X_MC);
	const bool validN3 = isValid(X_PC);

	// fit/pos
	float4 t = readValueFromCache2D(inTarget, tId_i, tId_j);
	b += -parameters.weightFitting*(X_CC - t);
	pre += parameters.weightFitting;

	// reg/pos
	float4 p = X_CC;
	float4 e_reg = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	if (validN0){ float4 q = X_CM; e_reg += (p - q); pre += parameters.weightRegularizer*make_float4(1.0f, 1.0f, 1.0f, 1.0f); }
	if (validN1){ float4 q = X_CP; e_reg += (p - q); pre += parameters.weightRegularizer*make_float4(1.0f, 1.0f, 1.0f, 1.0f); }
	if (validN2){ float4 q = X_MC; e_reg += (p - q); pre += parameters.weightRegularizer*make_float4(1.0f, 1.0f, 1.0f, 1.0f); }
	if (validN3){ float4 q = X_PC; e_reg += (p - q); pre += parameters.weightRegularizer*make_float4(1.0f, 1.0f, 1.0f, 1.0f); }
	b += -parameters.weightRegularizer*e_reg;

	// Preconditioner
	if (pre.x > FLOAT_EPSILON) pre = 1.0f / pre;
	else				       pre = make_float4(1.0f, 1.0f, 1.0f, 1.0f);
	outPre = pre;

	return b;
}

////////////////////////////////////////
// applyJTJ
////////////////////////////////////////

__inline__ __device__ float4 applyJTJDevice(int tId_i, int tId_j, int gId_i, int gId_j, unsigned int W, unsigned int H, volatile float4* inTarget, volatile float4* inP, volatile float4* inX, PatchSolverParameters& parameters)
{
	float4 b = make_float4(0.0f, 0.0f, 0.0f, 0.0f);

	float4 X_CC = readValueFromCache2D(inX, tId_i, tId_j);	   float4 P_CC = readValueFromCache2D(inP, tId_i, tId_j);    
	float4 X_CM = readValueFromCache2D(inX, tId_i, tId_j - 1); float4 P_CM = readValueFromCache2D(inP, tId_i, tId_j - 1);
	float4 X_CP = readValueFromCache2D(inX, tId_i, tId_j + 1); float4 P_CP = readValueFromCache2D(inP, tId_i, tId_j + 1);
	float4 X_MC = readValueFromCache2D(inX, tId_i - 1, tId_j); float4 P_MC = readValueFromCache2D(inP, tId_i - 1, tId_j);
	float4 X_PC = readValueFromCache2D(inX, tId_i + 1, tId_j); float4 P_PC = readValueFromCache2D(inP, tId_i + 1, tId_j);

	const bool validN0 = isValid(X_CM);
	const bool validN1 = isValid(X_CP);
	const bool validN2 = isValid(X_MC);
	const bool validN3 = isValid(X_PC);

	// fit/pos
	b += parameters.weightFitting*P_CC;

	// pos/reg
	float4 e_reg = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	if (validN0) e_reg += (P_CC - P_CM);
	if (validN1) e_reg += (P_CC - P_CP);
	if (validN2) e_reg += (P_CC - P_MC);
	if (validN3) e_reg += (P_CC - P_PC);
	b += parameters.weightRegularizer*e_reg;

	return b;
}

#endif
