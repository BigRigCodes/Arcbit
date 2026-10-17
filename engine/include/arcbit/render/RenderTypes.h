#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/core/Math.h>
#include <arcbit/render/RenderHandle.h>

#include <span>

// ---------------------------------------------------------------------------
// DLL export / import macro
// The backend DLL defines ARCBIT_VULKAN_EXPORTS before including these headers.
// Everything else (engine, game) gets the import side automatically.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
    #if defined(ARCBIT_VULKAN_EXPORTS)
        #define ARCBIT_RENDER_API __declspec(dllexport)
    #else
        #define ARCBIT_RENDER_API __declspec(dllimport)
    #endif
#else
    #if defined(ARCBIT_VULKAN_EXPORTS)
        #define ARCBIT_RENDER_API __attribute__((visibility("default")))
    #else
        #define ARCBIT_RENDER_API
    #endif
#endif

namespace Arcbit {

// ---------------------------------------------------------------------------
// Flags helper
//
// Mark an enum as a flags type to enable bitwise operators.
// Usage:  ARCBIT_ENABLE_FLAGS(BufferUsage)
// ---------------------------------------------------------------------------
template<typename T>
struct IsFlags : std::false_type {};

#define ARCBIT_ENABLE_FLAGS(EnumType)                                              \
    template<> struct IsFlags<EnumType> : std::true_type {};

template<typename T> requires IsFlags<T>::value
constexpr T operator|(T a, T b) noexcept
{ return static_cast<T>(std::to_underlying(a) | std::to_underlying(b)); }

template<typename T> requires IsFlags<T>::value
constexpr T operator&(T a, T b) noexcept
{ return static_cast<T>(std::to_underlying(a) & std::to_underlying(b)); }

template<typename T> requires IsFlags<T>::value
constexpr T operator^(T a, T b) noexcept
{ return static_cast<T>(std::to_underlying(a) ^ std::to_underlying(b)); }

template<typename T> requires IsFlags<T>::value
constexpr T operator~(T a) noexcept
{ return static_cast<T>(~std::to_underlying(a)); }

template<typename T> requires IsFlags<T>::value
constexpr bool HasFlag(T flags, T flag) noexcept
{ return (std::to_underlying(flags) & std::to_underlying(flag)) != 0; }

// ---------------------------------------------------------------------------
// Pixel format
// ---------------------------------------------------------------------------
enum class Format : u32
{
    Undefined = 0,

    // 8-bit per channel
    RGBA8_UNorm,
    RGBA8_SRGB,
    BGRA8_UNorm,
    BGRA8_SRGB,   // typical swapchain format on Windows

    // 16-bit float per channel
    RGBA16_Float,

    // 32-bit float per channel
    RGBA32_Float,
    R32_Float,

    // Depth / stencil
    D32_Float,
    D24_UNorm_S8_UInt,
};

// ---------------------------------------------------------------------------
// Buffer usage flags
// ---------------------------------------------------------------------------
enum class BufferUsage : u32
{
    None     = 0,
    Vertex   = 1 << 0,
    Index    = 1 << 1,
    Uniform  = 1 << 2,   // small per-draw constants, UBO
    Storage  = 1 << 3,   // large read/write data, SSBO (e.g. light list)
    Transfer = 1 << 4,   // staging buffer for uploads
    Readback = 1 << 5,   // GPU writes, CPU reads (screenshots); pair with HostVisible
};
ARCBIT_ENABLE_FLAGS(BufferUsage)

// ---------------------------------------------------------------------------
// Texture usage flags
// ---------------------------------------------------------------------------
enum class TextureUsage : u32
{
    None         = 0,
    Sampled      = 1 << 0,   // read in shaders
    RenderTarget = 1 << 1,   // write as color attachment
    DepthStencil = 1 << 2,   // write as depth attachment
    Storage      = 1 << 3,   // read/write in compute
    Transfer     = 1 << 4,   // copy src/dst
};
ARCBIT_ENABLE_FLAGS(TextureUsage)

// ---------------------------------------------------------------------------
// Shader stage flags
// ---------------------------------------------------------------------------
enum class ShaderStage : u32
{
    None     = 0,
    Vertex   = 1 << 0,
    Fragment = 1 << 1,
    Compute  = 1 << 2,
};
ARCBIT_ENABLE_FLAGS(ShaderStage)

// ---------------------------------------------------------------------------
// Sampler state
// ---------------------------------------------------------------------------
enum class Filter : u32
{
    Nearest,
    Linear,
};

enum class AddressMode : u32
{
    Repeat,
    MirrorRepeat,
    ClampToEdge,
    ClampToBorder,
};

// ---------------------------------------------------------------------------
// Pipeline state
// ---------------------------------------------------------------------------
enum class CullMode : u32
{
    None,
    Front,
    Back,
};

enum class CompareOp : u32
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class BlendFactor : u32
{
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};

enum class BlendOp : u32
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class IndexType : u32
{
    U16,
    U32,
};

// ---------------------------------------------------------------------------
// Load / store ops (used for render pass attachments)
// ---------------------------------------------------------------------------
enum class LoadOp : u32
{
    Load,      // preserve existing contents
    Clear,     // fill with ClearValue
    DontCare,  // undefined — fastest, use when you'll overwrite every pixel
};

enum class StoreOp : u32
{
    Store,     // write results back
    DontCare,  // discard — use for depth when you won't sample it later
};

// ---------------------------------------------------------------------------
// Resource descriptors
// ---------------------------------------------------------------------------

struct BufferDesc
{
    u64         Size        = 0;
    BufferUsage Usage       = BufferUsage::None;
    bool        HostVisible = false;   // true = CPU can map & write directly
    const char* DebugName   = nullptr;
};

struct TextureDesc
{
    u32          Width     = 0;
    u32          Height    = 0;
    u32          MipLevels = 1;
    Format       Format    = Format::RGBA8_UNorm;
    TextureUsage Usage     = TextureUsage::Sampled;
    const char*  DebugName = nullptr;
};

struct SamplerDesc
{
    Filter      MinFilter  = Filter::Linear;
    Filter      MagFilter  = Filter::Linear;
    AddressMode AddressU   = AddressMode::Repeat;
    AddressMode AddressV   = AddressMode::Repeat;
    bool        Anisotropy = false;
    f32         MaxAniso   = 1.0f;
    const char* DebugName  = nullptr;
};

struct SwapchainDesc
{
    void* NativeWindowHandle = nullptr;   // SDL_Window* — void* keeps SDL out of this header
    u32   Width              = 0;
    u32   Height             = 0;
    bool  VSync              = true;
};

struct ShaderDesc
{
    ShaderStage  Stage      = ShaderStage::Vertex;
    const u8*    Code       = nullptr;   // SPIR-V bytecode
    u32          CodeSize   = 0;
    const char*  EntryPoint = "main";
    const char*  DebugName  = nullptr;
};

// Describes a single vertex input attribute.
struct VertexAttribute
{
    u32    Location;    // layout(location = N) in the shader
    u32    Binding;     // which vertex buffer binding slot
    Format Format;      // data type/size
    u32    Offset;      // byte offset within the vertex stride
};

// Describes a vertex buffer binding (one entry per bound VBO).
struct VertexBinding
{
    u32  Binding;
    u32  Stride;
    bool PerInstance = false;
};

struct BlendState
{
    bool        Enable         = false;
    BlendFactor SrcColor       = BlendFactor::One;
    BlendFactor DstColor       = BlendFactor::Zero;
    BlendOp     ColorOp        = BlendOp::Add;
    BlendFactor SrcAlpha       = BlendFactor::One;
    BlendFactor DstAlpha       = BlendFactor::Zero;
    BlendOp     AlphaOp        = BlendOp::Add;
};

struct DepthState
{
    bool      TestEnable  = false;
    bool      WriteEnable = false;
    CompareOp Compare     = CompareOp::Less;
};

struct PipelineDesc
{
    ShaderHandle  VertexShader;
    ShaderHandle  FragmentShader;

    // Vertex input — empty = no vertex buffers (e.g. fullscreen triangle)
    std::span<const VertexAttribute> Attributes;
    std::span<const VertexBinding>   Bindings;

    CullMode   CullMode  = CullMode::Back;
    bool       Wireframe = false;
    DepthState Depth;
    BlendState Blend;

    // Format of attachments this pipeline will write to.
    // Must match what's passed to BeginRendering.
    // Set DepthFormat to Format::Undefined when no depth attachment is used.
    Format ColorFormat = Format::BGRA8_SRGB;
    Format DepthFormat = Format::Undefined;

    // Set to true to include the albedo texture descriptor set (set 0, binding 0).
    bool UseTextures = false;

    // Set to true to include a second texture descriptor set (set 1, binding 0)
    // for normal maps. Requires UseTextures to also be true.
    bool UseNormalTexture = false;

    // Set to true to include a storage buffer descriptor set after any texture
    // sets (set 2 when both UseTextures and UseNormalTexture are set).
    // Used for the per-frame dynamic light list.
    bool UseStorageBuffer = false;

    // Set to true to add a second storage buffer descriptor set (set 3 when all
    // three flags above are set). Used for the per-frame shadow map SSBO.
    bool UseShadowBuffer = false;

    const char* DebugName = nullptr;
};

// A single color or depth attachment for a render pass.
struct Attachment
{
    TextureHandle Texture;
    LoadOp        Load  = LoadOp::Clear;
    StoreOp       Store = StoreOp::Store;

    // Used when Load == Clear
    Color ClearColor  = Color::Black();
    f32   ClearDepth  = 1.0f;
    u8    ClearStencil = 0;
};

// Passed to BeginRendering to describe the output targets for this draw.
struct RenderingDesc
{
    std::span<const Attachment> ColorAttachments;
    const Attachment*           DepthAttachment  = nullptr;   // optional
};

// Top-level device creation descriptor.
struct DeviceDesc
{
    void*       NativeWindowHandle = nullptr;
    const char* AppName            = "Arcbit";
    u32         AppVersion         = 0;
    bool        EnableValidation   = false;
};

} // namespace Arcbit
