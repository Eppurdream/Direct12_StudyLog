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

// 정점 구조체
struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};

// 상수 버퍼로 쓰일 정보를 담는 구조체
struct ObjectConstants
{
    // 동차절단 공간 변환에 쓰일 행렬
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
    // 루트 시그니처
    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    // 상수 버퍼 서술자 (파이프라인에 Binding되기 위한 Desc)
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;
    // 상수 버퍼 ( 업로드 힙으로 만들어야 cpu 갱신 가능 )
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;

    // 정점쉐이더와 픽셀쉐이더 변수
    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;

    // 입력 배치
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // 파이프라인 상태 객체
    ComPtr<ID3D12PipelineState> mPSO = nullptr;

    // 정점 버퍼 뷰
    D3D12_VERTEX_BUFFER_VIEW                VertexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource>  VertexBufferGPU = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>  VertexBufferUploader = nullptr;

    // 인덱스 버퍼 뷰
    D3D12_INDEX_BUFFER_VIEW                 IndexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource>  IndexBufferGPU = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>  IndexBufferUploader = nullptr;

    int VertexCount = 0;
    int IndexCount = 0;

    // 월드 / 시야 / 투영 행렬
    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    // 구면 좌표 제어 값
    float mTheta = 1.5f * XM_PI;
    float mPhi = XM_PIDIV4;
    float mRadius = 5.0f;

    // 마우스 좌표
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
    // Direct3D 초기화
    if (!D3DApp::Initialize())
        return false;

    // 초기화 명령들을 준비하기 위해 명령 목록을 재설정 한다.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // 초기화 명령들
    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
    BuildPSO();

    // 초기화 명령들을 실행한다.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 초기화가 완료 될 때까지 기다린다.
    FlushCommandQueue();

    return true;
}

void GeometryApp::OnResize()
{
    D3DApp::OnResize();

    // 창의 크기가 바뀌었으므로 종횡비를 갱신하고 투영 행렬을 다시 계산한다.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void GeometryApp::Update(const GameTimer& gt)
{
    // 구면 좌표를 직교 좌표로 변환한다.
    float x = mRadius * sinf(mPhi) * cosf(mTheta);
    float z = mRadius * sinf(mPhi) * sinf(mTheta);
    float y = mRadius * cosf(mPhi);

    // 시야 행렬을 구축한다.
    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX worldViewProj = world * view * proj;

    // 상수 버퍼를 갱신한다.
    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
    mObjectCB->CopyData(0, objConstants);
}

void GeometryApp::Draw(const GameTimer& gt)
{
    // 명령 기록에 관련된 메모리의 재활용을 위해 명령 할당자를 재설정한다.
    // 재설정은 GPU가 관련 명령 목록들을 모두 처리한 후에 일어난다.
    ThrowIfFailed(mDirectCmdListAlloc->Reset());

    // 명령 목록을 ExecuteCommandList를 통해서 명령 대기열에 추가했다면 명령 목로글 재설정 할 수 있다.
    // 명령 목록을 재설정하면 메모리가 재활용된다.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // 자원 용도에 관련된 상태 전이를 Direct3D에 통지한다.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // 새로 그리기 위해 백버퍼와 깊이 버퍼를 지운다.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    /// 렌더링 결과가 기록될 렌더 대상 버퍼들을 지정한다.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // 루트 시그니처와 서술자 힙을 설정한다.
    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // 상수버퍼와 같은 파이프라인에 묶을 자원들을 지정하는 서술자 테이블을 세팅한다.
    mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    // 박스의 정점 정보를 묶어 입력 조립기에 셋팅한다. 
    mCommandList->IASetVertexBuffers(0, 1, &VertexBufferView);
    mCommandList->IASetIndexBuffer(&IndexBufferView);
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    mCommandList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);

    // 상수버퍼와 같은 파이프라인에 묶을 자원들을 지정하는 서술자 테이블을 세팅한다.
    mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    // 자원 용도에 관련된 상태 전이를 Direct3D에 통보한다.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // 명령들의 기록을 마친다.
    ThrowIfFailed(mCommandList->Close());

    // 명령 실행을 위해 명령 목록을 명령 대기열에 추가한다.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 후면 버퍼와 전면 버퍼를 교환한다.
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // 이 프레임의 명령들이 모두 처리되길 기다린다.
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
        // 마우스 한 픽셀 이동을 4분의 1도에 대응시킨다.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // 마우스 입력에 기초해 각도를 갱신한다. 카메라가 상자를 중심으로 공전한다.
        mTheta += dx;
        mPhi += dy;

        // 파이 각도를 제한한다.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        // 마우스 한 픽셀 이동을 장면의 0.005단위에 대응시킨다.
        float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

        // 마우스 입력에 기초해서 카메라 반지름을 갱신한다.
        mRadius += dx - dy;

        // 반지름을 제한한다.
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void GeometryApp::BuildDescriptorHeaps()
{
    // 상수 버퍼 서슬자
    // 루프 매개변수에 쓰기 위한 서술자 힙을 만들다.
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
    // 물체 n개의 상수 자료를 담을 상수 버퍼
    // UpLoadBuff는 업로드 버퍼를 자동으로 할당해주는 사용자 정의 클래스이다.
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    // 버퍼 자체의 시작 주소
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    
    // 버프에서 i번째 물체의 상수 버퍼의 오프셋을 얻는다. 지금은 0
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
    // 일반적으로 쉐이더 프로그램은 특정 자원들이 입력된다고 기대한다.
    // 루트 시그니처는 쉐이더 프로그램이 기대하는 자원들을 정의 한다.
    // 쉐이더 프로그램은 본질적으로 하나의 함수이고 쉐이더에 입력되는 자원들은 함수의 매개변수들에 해당한다.
    // 루트 시그니처는 곧 함수 시그니처를 정의하는 수단이라고 할 수 있다.

    // 루트 매개변수는 서술자 테이블이거나 루트 서술자 또는 루트 상수
    CD3DX12_ROOT_PARAMETER slotRootParameter[1];

    // CBV 하나를 담는 서술자 테이블 생성
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 
        1,              // 테이블 서술자 개수
        0);             // 이 루트 매개변수에 묶일 쉐이더 인수들의 기준 레지스터 번호
    slotRootParameter[0].InitAsDescriptorTable(
        1,              // 구간 개수  
        &cbvTable);     // 구간들의 배열을 가리키는 포인터

    // 루트 시그니처는 루트 매개변수들의 배열
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // 상수 버퍼 하나로 구성된 서술자 구간을 가리키는 슬롯 하나로 이루어진 루트 시그니처를 생성한다.
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
    // 쉐이더 프로그램을 빌드하고, 입력 레이아웃에 대한 세부 요소들이 무엇인지 설정한다.
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
        // 팔각형 정점 값
        Vertex({ XMFLOAT3(+0.0f, +0.0f, +0.0f), XMFLOAT4(Colors::Red) }),
        Vertex({ XMFLOAT3(+0.0f, +1.0f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(+0.7f, +0.7f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(+1.0f, +0.0f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(+0.7f, -0.7f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(+0.0f, -1.0f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(-0.7f, -0.7f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(-1.0f, +0.0f, +0.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(-0.7f, +0.7f, +0.0f), XMFLOAT4(Colors::Blue) }),

        // 사각뿔 정점 값
        Vertex({ XMFLOAT3(+1.0f, +0.0f, +2.0f), XMFLOAT4(Colors::Red) }), // 9
        Vertex({ XMFLOAT3(+1.0f, +0.0f, -0.0f), XMFLOAT4(Colors::Blue) }), // 10
        Vertex({ XMFLOAT3(-1.0f, +0.0f, -0.0f), XMFLOAT4(Colors::Red) }), // 11
        Vertex({ XMFLOAT3(-1.0f, +0.0f, +2.0f), XMFLOAT4(Colors::Blue) }), // 12
        Vertex({ XMFLOAT3(+0.0f, +2.0f, +1.0f), XMFLOAT4(Colors::Green) }), // 13
    };

    std::array<std::uint16_t, 42> Indices =
    {
        // 팔각형 인덱스 값
        0, 1, 2,
        0, 2, 3,
        0, 3, 4,
        0, 4, 5,
        0, 5, 6,
        0, 6, 7,
        0, 7, 8,
        0, 8, 1,

        // 사각뿔 인덱스 값
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
    // 파이프라인 상태를 생성한다.
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