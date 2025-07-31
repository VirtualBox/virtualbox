Reference: [D3D11.3 Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)

## General Glossary

\* (star) means there are similarities but also significant differences in detail.

| D3D11 | Metal | Note 
| ----------- | ---------- | - 
| Device | Device | 
| DeviceContext | N/A | to issue commands
| N/A | CommandEncoder | to issue commands
| N/A | CommandQueue | 
| DeferredContext | N/A | `ParallelRenderCommandEncoder`?
| Shader | Function |
| VertexShader | VertexFunction |
| PixelShader | FragmentFunction |
| ComputeShader |  KernelFunction |
| HullShader | N/A |
| DomainShader | PostVertexFunction |
| GeometryShader | N/A |
| InputAssembler | ? | I'm sure there is such a thing but I don't find the name of it. Also in many online tutorials this stage is omitted because we can just access unbounded array of structure in shader...
| Rasterizer* | Rasterizer* |
| StreamOutput | N/A | Although you can write to buffer under certain conditions
| Tessellator* | Tessellator* | different supported topology
| Resource* | Resource* | 
| Buffer* | Buffer* |
| Texture* | Texture* | 
| (Resource)View | N/A | metal merges the concepts of resource and resource view
| ConstantBuffer(/View) | Buffer* |
| UAV | Buffer* or Texture* |
| RTV | Texture* | Can we really have buffer render target?
| DSV | Texture* |
| Multisampling | Multisampling |
| Fence | ? |
| ? | Fence | I'm sure D3D11Fence is not the same animal as MTLFence
| ? | Event | maybe this is more close to D3D11Fence?
| Query | N/A | but some type of query can be emulated (TODO: add more on this)
| Predication | N/A | hard to emulate
| (DXGI) Format | PixelFormat / VertexFormat / AttributeFormat | 


### On Resources

| D3D11 | Metal | Note 
| ----------- | ---------- | - 
| `USAGE_DEFAULT` | `StorageMode.private` |
| `USAGE_IMMUTABLE` | `StorageMode.private` | you can control shader's access to resources (e.g. to set it immutable) [ref.](https://developer.apple.com/documentation/metal/mtlpipelinebufferdescriptor/2879274-mutability). seems it can't be done per resource.
| `USAGE_DYNAMIC` | `StorageMode.shared` with `CPUCacheMode.writeCombined`? | [shared](https://developer.apple.com/documentation/metal/mtlstoragemode/shared) is not available for texture on intel-based mac
| `USAGE_STAGING` | `StoragåeMode.shared`  | 
| Texture1D | Texture with .type1D |
| Texture1D with ArraySize > 1 | Texture with .type1DArray |
| Texture2D | Texture with .type2D |
| Texture2D with ArraySize > 1 | Texture with .type2DArray |
| Texture2D with SampleDesc.Count > 1 | Texture with .type2DMultisample |
| Texture2D with ArraySize > 1 and SampleDesc.Count > 1 | Texture with .type2DMultisampleArray |
| Texture3D | Texture with .type3D |
| TextureCube(2D with MISC_TEXTURECUBE) | Texture with .typeCube |
| TextureCube(2D with MISC_TEXTURECUBE) with ArraySize > 1 | Texture with .typeCubeArray |
| Buffer | Buffer |
| ? | Texture with .typeTextureBuffer |
| Width | width |
| Height | height |
| Depth | depth |
| MipLevels | mipmapLevelCount |
| ArraySize | arrayLength |
| SampleDesc.Count | sampleCount | 
| SampleDesc.Quality | N/A | 
| MipSlice | levels.lowerBound (range length = 1), or simply mipmapLevel
| FirstArraySlice | slices.lowerBound
| ArraySize | slices.upperBound - slices.lowerBound
| MostDetailedMip | levels.lowerBound
| MipLevels | levels.upperBound - levels.lowerBound
| FirstWSlice | ? | should be slices
| WSize | ? |
| First2DArrayFace | ? | ?
| NumCubes | ? |
| SysMemPitch/RowPitch | bytesPerRow | D3D provides this value when mapping a resource. however, in metal it's user's responsibility to define it
| SysMemSlicePitch/DepthPitch | bytesPerImage | the same above

[Constraints on texture view casting](https://developer.apple.com/documentation/metal/mtltexture/2928180-maketextureview#discussion)

[Constraints on creating texture from buffer](https://developer.apple.com/documentation/metal/mtlbuffer/1613852-maketexture)

## Interfaces & Constants

| D3D11 | Metal | Note 
| ----------- | ---------- | - 
| 