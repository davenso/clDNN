﻿/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <cmath>
#include "convolution_kernel_bfyx_gemm_like.h"
#include "kernel_selector_utils.h"
#include "common_tools.h"

namespace KernelSelector 
{
    
    ParamsKey ConvolutionKernel_bfyx_GEMMLike::GetSupportedKey() const
    {
        ParamsKey k;
        k.EnableInputDataType(Datatype::F16);
        k.EnableInputDataType(Datatype::F32);
        k.EnableInputWeightsType(WeightsType::F16);
        k.EnableInputWeightsType(WeightsType::F32);
        k.EnableOutputDataType(Datatype::F16);
        k.EnableOutputDataType(Datatype::F32);
        k.EnableInputLayout(DataLayout::bfyx);
        k.EnableOutputLayout(DataLayout::bfyx);
        k.EnableTensorOffset();
        k.EnableTensorPitches();
        k.EnableSubGroup();
        //k.EnableSubGroupShort(); // we need it for FP16 only. we check it on the Validate phase
        k.EnableBiasPerFeature();
        k.EnableNonBiasTerm();
        k.EnableBatching();
        k.EnableSplitSupport();
        return k;
    }

    std::string ConvolutionKernel_bfyx_GEMMLike::GetKernelName(const ConvolutionParams& params) const
    {
        if (params.inputs[0].GetDType() == Datatype::F32)
        {
            return kernelName + "_fp32";
        }
        else
        {
            return kernelName + "_fp16";
        }
    }

    JitConstants ConvolutionKernel_bfyx_GEMMLike::GetJitConstants(const ConvolutionParams& params, Parent::DispatchData runInfo) const
    {
        JitConstants jit = Parent::GetJitConstants(params, runInfo);
        
        jit.AddConstants({
            MakeJitConstant("ALIGNED_OFM",                  RoundUp(params.output.Feature().v, runInfo.gemmStyle.subBlockDimN)),
            MakeJitConstant("DX",                           runInfo.gemmStyle.globalWorkSizeDX),
            MakeJitConstant("DY",                           runInfo.gemmStyle.globalWorkSizeDY),
            MakeJitConstant("FILTER_SIZE_X_DIV2",           params.convParams.filterSize.x / 2),
            MakeJitConstant("INPUT_BUFFER_WIDTH_PADDED",    ""),    // TODO: enable non padding path again
            MakeJitConstant("INPUT_BUFFER_HEIGHT_PADDED",   ""),
        });

        return jit;
    }

    ConvolutionKernel_bfyx_GEMMLike::Parent::DispatchData ConvolutionKernel_bfyx_GEMMLike::SetDefault(const ConvolutionParams& arg, int autoTuneIndex) const
    {
        DispatchData runInfo = Parent::SetDefault(arg, autoTuneIndex);

        const auto& cp = arg.convParams;

        runInfo.lws0 = 1;
        runInfo.lws2 = 1;

        if (arg.inputs[0].GetDType() == Datatype::F16)
        {
            runInfo.gemmStyle = { 1, cp.filterSize.x, 32, 32, 1, 1 };
            runInfo.lws1 = 16;
            runInfo.effiency = FORCE_PRIORITY_6;
        }
        else
        {
            runInfo.gemmStyle = { 2, cp.filterSize.x, 32, 32, 2, 1 };
            runInfo.lws1 = 8;
            runInfo.effiency = FORCE_PRIORITY_8;
        }

        size_t sgemm_m = RoundUp(arg.output.X().v * arg.output.Y().v, runInfo.gemmStyle.subBlockDimM);
        size_t sgemm_n = RoundUp(arg.output.Feature().v, runInfo.gemmStyle.subBlockDimN);

        runInfo.gws0 = RoundUp(CeilDiv(sgemm_n, runInfo.gemmStyle.globalWorkSizeDX), runInfo.lws0);
        runInfo.gws1 = RoundUp(CeilDiv(sgemm_m, runInfo.gemmStyle.globalWorkSizeDY), runInfo.lws1);
        runInfo.gws2 = arg.output.Batch().v;

        return runInfo;
    }

    bool ConvolutionKernel_bfyx_GEMMLike::Validate(const Params& p, const OptionalParams& o) const
    {
        if (!Parent::Validate(p, o) ||
            !CovolutionCheckInput(p, o))
        {
            return false;
        }

        const auto& params = static_cast<const ConvolutionParams&>(p);

        if (!params.engineInfo.bSubGroupShortSupport && params.inputs[0].GetDType() == Datatype::F16)
        {
            return false;
        }

        return true;
    }

    std::vector<WeightsLayout> ConvolutionKernel_bfyx_GEMMLike::GetSupportedWeightLayouts(const ConvolutionParams& params) const
    {
        if (params.inputs[0].GetDType() == Datatype::F16)
        {
            return{ WeightsLayout::iy_xs_os_xsv2_osv16__ao32 };
        }
        else
        {
            return{ WeightsLayout::iy_xs_os_xsv2_osv8__ao32 };
        }
    }

    KernelsData ConvolutionKernel_bfyx_GEMMLike::GetKernelsData(const Params& params, const OptionalParams& options) const
    {
        return GetCommonKernelsData(params, options, AGE_BASED);
    }
}