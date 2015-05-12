//
//   Copyright 2013 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#include "../osd/d3d11DrawRegistry.h"
#include "../far/error.h"

#include <D3D11.h>
#include <D3Dcompiler.h>

#include <sstream>

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

namespace Osd {

D3D11DrawConfig::~D3D11DrawConfig()
{
    if (vertexShader) vertexShader->Release();
    if (hullShader) hullShader->Release();
    if (domainShader) domainShader->Release();
    if (geometryShader) geometryShader->Release();
    if (pixelShader) pixelShader->Release();
}

static const char *commonShaderSource =
#include "hlslPatchCommon.gen.h"
;
static const char *bsplineShaderSource =
#include "hlslPatchBSpline.gen.h"
;
static const char *gregoryShaderSource =
#include "hlslPatchGregory.gen.h"
;

D3D11DrawRegistryBase::~D3D11DrawRegistryBase() {}

D3D11DrawSourceConfig *
D3D11DrawRegistryBase::_CreateDrawSourceConfig(
    Far::PatchDescriptor const & desc, ID3D11Device * pd3dDevice)
{
    D3D11DrawSourceConfig * sconfig = _NewDrawSourceConfig();

    sconfig->commonShader.source = commonShaderSource;

    switch (desc.GetType()) {
    case Far::PatchDescriptor::REGULAR:
    case Far::PatchDescriptor::BOUNDARY:
    case Far::PatchDescriptor::CORNER:
        sconfig->commonShader.AddDefine("OSD_PATCH_BSPLINE");
        sconfig->commonShader.AddDefine("OSD_PATCH_ENABLE_SINGLE_CREASE");
        sconfig->vertexShader.source = bsplineShaderSource;
        sconfig->vertexShader.target = "vs_5_0";
        sconfig->vertexShader.entry = "vs_main_patches";
        sconfig->hullShader.source = bsplineShaderSource;
        sconfig->hullShader.target = "hs_5_0";
        sconfig->hullShader.entry = "hs_main_patches";
        sconfig->domainShader.source = bsplineShaderSource;
        sconfig->domainShader.target = "ds_5_0";
        sconfig->domainShader.entry = "ds_main_patches";
        break;
    case Far::PatchDescriptor::GREGORY:
        sconfig->commonShader.AddDefine("OSD_PATCH_GREGORY");
        sconfig->vertexShader.source = gregoryShaderSource;
        sconfig->vertexShader.target = "vs_5_0";
        sconfig->vertexShader.entry = "vs_main_patches";
        sconfig->hullShader.source = gregoryShaderSource;
        sconfig->hullShader.target = "hs_5_0";
        sconfig->hullShader.entry = "hs_main_patches";
        sconfig->domainShader.source = gregoryShaderSource;
        sconfig->domainShader.target = "ds_5_0";
        sconfig->domainShader.entry = "ds_main_patches";
        break;
    case Far::PatchDescriptor::GREGORY_BOUNDARY:
        sconfig->commonShader.AddDefine("OSD_PATCH_GREGORY_BOUNDARY");
        sconfig->vertexShader.source = gregoryShaderSource;
        sconfig->vertexShader.target = "vs_5_0";
        sconfig->vertexShader.entry = "vs_main_patches";
        sconfig->vertexShader.AddDefine("OSD_PATCH_GREGORY_BOUNDARY");
        sconfig->hullShader.source = gregoryShaderSource;
        sconfig->hullShader.target = "hs_5_0";
        sconfig->hullShader.entry = "hs_main_patches";
        sconfig->hullShader.AddDefine("OSD_PATCH_GREGORY_BOUNDARY");
        sconfig->domainShader.source = gregoryShaderSource;
        sconfig->domainShader.target = "ds_5_0";
        sconfig->domainShader.entry = "ds_main_patches";
        sconfig->domainShader.AddDefine("OSD_PATCH_GREGORY_BOUNDARY");
        break;
    case Far::PatchDescriptor::GREGORY_BASIS:
        // XXXdyu-patch-drawing gregory basis for d3d11
        break;
    default: // POINTS, LINES, QUADS, TRIANGLES
        // do nothing
        break;
    }

    return sconfig;
}

static ID3DBlob *
_CompileShader(
        DrawShaderSource const & common,
        DrawShaderSource const & source)
{
    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

    ID3DBlob* pBlob = NULL;
    ID3DBlob* pBlobError = NULL;

    std::vector<D3D_SHADER_MACRO> shaderDefines;
    for (int i=0; i<(int)common.defines.size(); ++i) {
        const D3D_SHADER_MACRO def = {
            common.defines[i].first.c_str(),
            common.defines[i].second.c_str(),
        };
        shaderDefines.push_back(def);
    }
    for (int i=0; i<(int)source.defines.size(); ++i) {
        const D3D_SHADER_MACRO def = {
            source.defines[i].first.c_str(),
            source.defines[i].second.c_str(),
        };
        shaderDefines.push_back(def);
    }
    const D3D_SHADER_MACRO def = { 0, 0 };
    shaderDefines.push_back(def);

    std::string shaderSource = common.source + source.source;

    HRESULT hr = D3DCompile(shaderSource.c_str(), shaderSource.size(),
                            NULL, &shaderDefines[0], NULL,
                            source.entry.c_str(), source.target.c_str(),
                            dwShaderFlags, 0, &pBlob, &pBlobError);
    if (FAILED(hr)) {
        if ( pBlobError != NULL ) {
            Far::Error(Far::FAR_RUNTIME_ERROR,
                     "Error compiling HLSL shader: %s\n",
                     (CHAR*)pBlobError->GetBufferPointer());
            pBlobError->Release();
            return NULL;
        }
    }

    return pBlob;
}

#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=NULL; } }

D3D11DrawConfig*
D3D11DrawRegistryBase::_CreateDrawConfig(
        DescType const & desc,
        SourceConfigType const * sconfig,
        ID3D11Device * pd3dDevice,
        ID3D11InputLayout ** ppInputLayout,
        D3D11_INPUT_ELEMENT_DESC const * pInputElementDescs,
        int numInputElements)
{
    assert(sconfig);

    ID3DBlob * pBlob;

    ID3D11VertexShader *vertexShader = NULL;
    if (! sconfig->vertexShader.source.empty()) {
        pBlob = _CompileShader(sconfig->commonShader, sconfig->vertexShader);
        pd3dDevice->CreateVertexShader(pBlob->GetBufferPointer(),
                                         pBlob->GetBufferSize(),
                                         NULL,
                                         &vertexShader);
        assert(vertexShader);

        if (ppInputLayout and !*ppInputLayout) {
            pd3dDevice->CreateInputLayout(pInputElementDescs, numInputElements,
                                          pBlob->GetBufferPointer(),
                                          pBlob->GetBufferSize(), ppInputLayout);
            assert(ppInputLayout);
        }

        SAFE_RELEASE(pBlob);
    }


    ID3D11HullShader *hullShader = NULL;
    if (! sconfig->hullShader.source.empty()) {
        pBlob = _CompileShader(sconfig->commonShader, sconfig->hullShader);
        pd3dDevice->CreateHullShader(pBlob->GetBufferPointer(),
                                       pBlob->GetBufferSize(),
                                       NULL,
                                       &hullShader);
        assert(hullShader);
        SAFE_RELEASE(pBlob);
    }

    ID3D11DomainShader *domainShader = NULL;
    if (! sconfig->domainShader.source.empty()) {
        pBlob = _CompileShader(sconfig->commonShader, sconfig->domainShader);
        pd3dDevice->CreateDomainShader(pBlob->GetBufferPointer(),
                                         pBlob->GetBufferSize(),
                                         NULL,
                                         &domainShader);
        assert(domainShader);
        SAFE_RELEASE(pBlob);
    }

    ID3D11GeometryShader *geometryShader = NULL;
    if (! sconfig->geometryShader.source.empty()) {
        pBlob = _CompileShader(sconfig->commonShader, sconfig->geometryShader);
        pd3dDevice->CreateGeometryShader(pBlob->GetBufferPointer(),
                                           pBlob->GetBufferSize(),
                                           NULL,
                                           &geometryShader);
        assert(geometryShader);
        SAFE_RELEASE(pBlob);
    }

    ID3D11PixelShader *pixelShader = NULL;
    if (! sconfig->pixelShader.source.empty()) {
        pBlob = _CompileShader(sconfig->commonShader, sconfig->pixelShader);
        pd3dDevice->CreatePixelShader(pBlob->GetBufferPointer(),
                                        pBlob->GetBufferSize(),
                                        NULL,
                                        &pixelShader);
        assert(pixelShader);
        SAFE_RELEASE(pBlob);
    }

    D3D11DrawConfig * config = _NewDrawConfig();

    config->vertexShader = vertexShader;
    config->hullShader = hullShader;
    config->domainShader = domainShader;
    config->geometryShader = geometryShader;
    config->pixelShader = pixelShader;

    return config;
}

}  // end namespace Osd

}  // end namespace OPENSUBDIV_VERSION
} // end namespace OpenSubdiv
