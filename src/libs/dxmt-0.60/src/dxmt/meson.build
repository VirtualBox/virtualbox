dxmt_src = [
  'dxmt_format.cpp',
  'dxmt_names.cpp',
  'dxmt_command_queue.cpp',
  'dxmt_command.cpp',
  'dxmt_capture.cpp',
  'dxmt_info.cpp',
  'dxmt_device.cpp',
  'dxmt_buffer.cpp',
  'dxmt_texture.cpp',
  'dxmt_context.cpp',
  'dxmt_dynamic.cpp',
  'dxmt_staging.cpp',
  'dxmt_hud_state.cpp',
  'dxmt_allocation.cpp',
  'dxmt_presenter.cpp',
]

air = custom_target('metal_air',
  input: 'dxmt_command.metal',
  output: 'metal_air',
  command: [
    find_program('xcrun'), '-sdk', 'macosx', 'metal',
    '-o', '@OUTPUT@', '-c', '@INPUT@'
  ]
)

metal_lib = custom_target('metal_lib',
  output: 'metal_lib',
  input: air,
  command: [
    find_program('xcrun'), '-sdk', 'macosx', 'metallib',
    '-o', '@OUTPUT@', '@INPUT@'
  ]
)

metal_lib_bin = custom_target('objectify-data',
  output : 'command_bin.c',
  input : metal_lib,
  command : [ 
    find_program('xxd'),
    '-n', 'dxmt_command_metallib',
    '-i', '@INPUT@',
    '@OUTPUT@', 
  ]
)

dxmt_lib = static_library('dxmt', dxmt_src, metal_lib_bin, dxmt_version,
  dependencies        : [ util_dep, airconv_forward_dep, winemetal_dep ],
  include_directories : [ dxmt_include_path ],
)

dxmt_dep = declare_dependency(
  link_with           : [ dxmt_lib ],
  include_directories : [ dxmt_include_path, include_directories('.') ],
)
