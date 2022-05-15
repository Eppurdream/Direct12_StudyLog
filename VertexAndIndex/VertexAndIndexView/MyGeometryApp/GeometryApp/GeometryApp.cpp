//***************************************************************************************
// GeometryApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//
// Shows how to draw a box in Direct3D 12.
//
// Controls:
//   Hold the left mouse button down and move the mouse to rotate.
//   Hold the right mouse button down and move the mouse to zoom in and out.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// ���� ����ü
struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};

// ��� ���۷� ���� ������ ��� ����ü
struct ObjectConstants
{
    // �������� ���� ��ȯ�� ���� ���
    XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
};

class GeometryApp : public D3DApp
{
public:
    GeometryApp(HINSTANCE hInstance);
    GeometryApp(const GeometryApp& rhs) = delete;
    GeometryApp& operator=(const GeometryApp& rhs) = delete;
    ~GeometryApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildBoxGeometry();
    void BuildPSO();

private:
    // ��Ʈ �ñ״�ó
    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    // ��� ���� ������ (���������ο� Binding�Ǳ� ���� Desc)
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;
    // ��� ���� ( ���ε� ������ ������ cpu ���� ���� )
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;

    // �������̴��� �ȼ����̴� ����
    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;

    // �Է� ��ġ
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // ���������� ���� ��ü
    ComPtr<ID3D12PipelineState> mPSO = nullptr;

    // ���� ���� ��
    D3D12_VERTEX_BUFFER_VIEW                VertexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource>  VertexBufferGPU = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>  VertexBufferUploader = nullptr;

    // �ε��� ���� ��
    D3D12_INDEX_BUFFER_VIEW                 IndexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource>  IndexBufferGPU = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>  IndexBufferUploader = nullptr;

    int VertexCount = 0;
    int IndexCount = 0;

    // ���� / �þ� / ���� ���
    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    // ���� ��ǥ ���� ��
    float mTheta = 1.5f * XM_PI;
    float mPhi = XM_PIDIV4;
    float mRadius = 5.0f;

    // ���콺 ��ǥ
    POINT mLastMousePos = { 0, 0 };

};

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        GeometryApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

GeometryApp::GeometryApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    memset(&VertexBufferView, 0, sizeof(VertexBufferView));
    memset(&IndexBufferView, 0, sizeof(IndexBufferView));
}

GeometryApp::~GeometryApp()
{
}

bool GeometryApp::Initialize()
{
    // Direct3D �ʱ�ȭ
    if (!D3DApp::Initialize())
        return false;

    // �ʱ�ȭ ��ɵ��� �غ��ϱ� ���� ��� ����� �缳�� �Ѵ�.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // �ʱ�ȭ ��ɵ�
    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
    BuildPSO();

    // �ʱ�ȭ ��ɵ��� �����Ѵ�.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // �ʱ�ȭ�� �Ϸ� �� ������ ��ٸ���.
    FlushCommandQueue();

    return true;
}

void GeometryApp::OnResize()
{
    D3DApp::OnResize();

    // â�� ũ�Ⱑ �ٲ�����Ƿ� ��Ⱦ�� �����ϰ� ���� ����� �ٽ� ����Ѵ�.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void GeometryApp::Update(const GameTimer& gt)
{
    // ���� ��ǥ�� ���� ��ǥ�� ��ȯ�Ѵ�.
    float x = mRadius * sinf(mPhi) * cosf(mTheta);
    float z = mRadius * sinf(mPhi) * sinf(mTheta);
    float y = mRadius * cosf(mPhi);

    // �þ� ����� �����Ѵ�.
    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX worldViewProj = world * view * proj;

    // ��� ���۸� �����Ѵ�.
    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
    mObjectCB->CopyData(0, objConstants);
}

void GeometryApp::Draw(const GameTimer& gt)
{
    // ��� ��Ͽ� ���õ� �޸��� ��Ȱ���� ���� ��� �Ҵ��ڸ� �缳���Ѵ�.
    // �缳���� GPU�� ���� ��� ��ϵ��� ��� ó���� �Ŀ� �Ͼ��.
    ThrowIfFailed(mDirectCmdListAlloc->Reset());

    // ��� ����� ExecuteCommandList�� ���ؼ� ��� ��⿭�� �߰��ߴٸ� ��� ��α� �缳�� �� �� �ִ�.
    // ��� ����� �缳���ϸ� �޸𸮰� ��Ȱ��ȴ�.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // �ڿ� �뵵�� ���õ� ���� ���̸� Direct3D�� �����Ѵ�.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // ���� �׸��� ���� ����ۿ� ���� ���۸� �����.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    /// ������ ����� ��ϵ� ���� ��� ���۵��� �����Ѵ�.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // ��Ʈ �ñ״�ó�� ������ ���� �����Ѵ�.
    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // ������ۿ� ���� ���������ο� ���� �ڿ����� �����ϴ� ������ ���̺��� �����Ѵ�.
    mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    // �ڽ��� ���� ������ ���� �Է� �����⿡ �����Ѵ�. 
    mCommandList->IASetVertexBuffers(0, 1, &VertexBufferView);
    mCommandList->IASetIndexBuffer(&IndexBufferView);
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    mCommandList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);

    // ������ۿ� ���� ���������ο� ���� �ڿ����� �����ϴ� ������ ���̺��� �����Ѵ�.
    mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    // �ڿ� �뵵�� ���õ� ���� ���̸� Direct3D�� �뺸�Ѵ�.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // ��ɵ��� ����� ��ģ��.
    ThrowIfFailed(mCommandList->Close());

    // ��� ������ ���� ��� ����� ��� ��⿭�� �߰��Ѵ�.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // �ĸ� ���ۿ� ���� ���۸� ��ȯ�Ѵ�.
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // �� �������� ��ɵ��� ��� ó���Ǳ� ��ٸ���.
    FlushCommandQueue();
}

void GeometryApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void GeometryApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void GeometryApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        // ���콺 �� �ȼ� �̵��� 4���� 1���� ������Ų��.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // ���콺 �Է¿� ������ ������ �����Ѵ�. ī�޶� ���ڸ� �߽����� �����Ѵ�.
        mTheta += dx;
        mPhi += dy;

        // ���� ������ �����Ѵ�.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        // ���콺 �� �ȼ� �̵��� ����� 0.005������ ������Ų��.
        float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

        // ���콺 �Է¿� �����ؼ� ī�޶� �������� �����Ѵ�.
        mRadius += dx - dy;

        // �������� �����Ѵ�.
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void GeometryApp::BuildDescriptorHeaps()
{
    // ��� ���� ������
    // ���� �Ű������� ���� ���� ������ ���� �����.
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 1;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
        IID_PPV_ARGS(&mCbvHeap)));
}

void GeometryApp::BuildConstantBuffers()
{
    // ��ü n���� ��� �ڷḦ ���� ��� ����
    // UpLoadBuff�� ���ε� ���۸� �ڵ����� �Ҵ����ִ� ����� ���� Ŭ�����̴�.
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    // ���� ��ü�� ���� �ּ�
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    
    // �������� i��° ��ü�� ��� ������ �������� ��´�. ������ 0
    int boxCBufIndex = 0;
    cbAddress += boxCBufIndex * objCBByteSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    md3dDevice->CreateConstantBufferView(
        &cbvDesc,
        mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void GeometryApp::BuildRootSignature()
{
    // �Ϲ������� ���̴� ���α׷��� Ư�� �ڿ����� �Էµȴٰ� ����Ѵ�.
    // ��Ʈ �ñ״�ó�� ���̴� ���α׷��� ����ϴ� �ڿ����� ���� �Ѵ�.
    // ���̴� ���α׷��� ���������� �ϳ��� �Լ��̰� ���̴��� �ԷµǴ� �ڿ����� �Լ��� �Ű������鿡 �ش��Ѵ�.
    // ��Ʈ �ñ״�ó�� �� �Լ� �ñ״�ó�� �����ϴ� �����̶�� �� �� �ִ�.

    // ��Ʈ �Ű������� ������ ���̺��̰ų� ��Ʈ ������ �Ǵ� ��Ʈ ���
    CD3DX12_ROOT_PARAMETER slotRootParameter[1];

    // CBV �ϳ��� ��� ������ ���̺� ����
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 
        1,              // ���̺� ������ ����
        0);             // �� ��Ʈ �Ű������� ���� ���̴� �μ����� ���� �������� ��ȣ
    slotRootParameter[0].InitAsDescriptorTable(
        1,              // ���� ����  
        &cbvTable);     // �������� �迭�� ����Ű�� ������

    // ��Ʈ �ñ״�ó�� ��Ʈ �Ű��������� �迭
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // ��� ���� �ϳ��� ������ ������ ������ ����Ű�� ���� �ϳ��� �̷���� ��Ʈ �ñ״�ó�� �����Ѵ�.
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));
}

void GeometryApp::BuildShadersAndInputLayout()
{
    // ���̴� ���α׷��� �����ϰ�, �Է� ���̾ƿ��� ���� ���� ��ҵ��� �������� �����Ѵ�.
    HRESULT hr = S_OK;

    mvsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mpsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void GeometryApp::BuildBoxGeometry()
{

    std::array<Vertex, 14> vertices =
    {
        // �Ȱ��� ���� ��
        Vertex({ XMFLOAT3(+0.0f, +0.0f, +0.0f), XMFLOAT4(Colors::Red) }),
        Vertex({ XMFLOAT3(+0.0f, +1.0f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(+0.7f, +0.7f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(+1.0f, +0.0f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(+0.7f, -0.7f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(+0.0f, -1.0f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(-0.7f, -0.7f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(-1.0f, +0.0f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(-0.7f, +0.7f, +0.0f), XMFLOAT4(Colors::Blue) }),

        // �簢�� ���� ��
        Vertex({ XMFLOAT3(+1.0f, +0.0f, +2.0f), XMFLOAT4(Colors::Red) }), // 9
        Vertex({ XMFLOAT3(+1.0f, +0.0f, -0.0f), XMFLOAT4(Colors::Blue) }), // 10
        Vertex({ XMFLOAT3(-1.0f, +0.0f, -0.0f), XMFLOAT4(Colors::Red) }), // 11
        Vertex({ XMFLOAT3(-1.0f, +0.0f, +2.0f), XMFLOAT4(Colors::Blue) }), // 12
        Vertex({ XMFLOAT3(+0.0f, +2.0f, +1.0f), XMFLOAT4(Colors::Green) }), // 13
    };

    std::array<std::uint16_t, 42> Indices =
    {
        // �Ȱ��� �ε��� ��
        0, 1, 2,
        0, 2, 3,
        0, 3, 4,
        0, 4, 5,
        0, 5, 6,
        0, 6, 7,
        0, 7, 8,
        0, 8, 1,

        // �簢�� �ε��� ��
        11, 10, 9,
        11, 9, 12,
        13, 10, 11,
        13, 9, 10,
        13, 12, 9,
        13, 11, 12
    };

    VertexCount = (UINT)vertices.size();
    const UINT vbByteSize = VertexCount * sizeof(Vertex);

    IndexCount = (UINT)Indices.size();
    const UINT ibByteSize = IndexCount * sizeof(std::uint16_t);

    
    VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, VertexBufferUploader);

    VertexBufferView.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
    VertexBufferView.StrideInBytes = sizeof(Vertex);
    VertexBufferView.SizeInBytes = vbByteSize;

    IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(),
        Indices.data(), ibByteSize, IndexBufferUploader);

    IndexBufferView.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
    IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
    IndexBufferView.SizeInBytes = ibByteSize;
}

void GeometryApp::BuildPSO()
{
    // ���������� ���¸� �����Ѵ�.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()
    };
    psoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize()
    };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}