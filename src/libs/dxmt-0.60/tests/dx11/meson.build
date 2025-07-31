fs = import('fs')

lib_d3d11 = cpp.find_library('d3d11')
lib_dxgi = cpp.find_library('dxgi')
lib_d3dcompiler = cpp.find_library('d3dcompiler_47')

fs.copyfile('shader_bp.hlsl')
fs.copyfile('shader_lighting.hlsl')
fs.copyfile('shader_cube.hlsl')
fs.copyfile('shaders.hlsl')
fs.copyfile('cube.obj')
fs.copyfile('test.png')
fs.copyfile('shader_hdr.hlsl')

executable('dx11_cube', ['dx11_cube.cpp'],
  dependencies: [ lib_d3d11, lib_dxgi, lib_d3dcompiler]
)

executable('dx11_bpl', ['dx11_bpl.cpp'],
  dependencies: [ lib_d3d11, lib_dxgi, lib_d3dcompiler]
)

executable('dx11_tri', ['dx11_triangle.cpp'],
  dependencies: [ lib_d3d11, lib_dxgi, lib_d3dcompiler]
)

executable('dx11_hdr_linear', ['dx11_hdr_linear.cpp'],
  dependencies: [ lib_d3d11, lib_dxgi, lib_d3dcompiler]
)

executable('dx11_hdr_pq', ['dx11_hdr_pq.cpp'],
  dependencies: [ lib_d3d11, lib_dxgi, lib_d3dcompiler]
)