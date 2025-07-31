native_llvm_path = get_option('native_llvm_path')

if not native_llvm_path.startswith('/')
  # Meson only allows relative include paths in case the toolchains are located inside
  # of the project source root, so handle that here.
  llvm_include_path_darwin = include_directories(join_paths('../../..', native_llvm_path, 'include'))
  native_llvm_path = join_paths(meson.project_source_root(), native_llvm_path)
else
  llvm_include_path_darwin = include_directories(join_paths(native_llvm_path, 'include'))
endif

llvm_ld_flags_darwin = [
  '-L'+join_paths(native_llvm_path, 'lib'),
  '-lm', '-lz', '-lcurses', '-lxml2'
]

if native_llvm_path.startswith('/usr/local/opt')
  llvm_ld_flags_darwin = [
    llvm_ld_flags_darwin,
    '/usr/local/opt/zstd/lib/libzstd.a', '/usr/local/opt/llvm@15/lib/libunwind.a'
  ]
endif

airconv_lib_darwin = static_library('airconv', airconv_src,
  include_directories : [ dxmt_include_path, llvm_include_path_darwin ],
  cpp_args            : [ '-ObjC++', llvm_cxx_flags  ],
  dependencies        : [ DXBCParser_native_dep ],
  link_args           : llvm_ld_flags_darwin,
  native              : not dxmt_native
)

airconv_dep_darwin = declare_dependency(
  link_with           : [ airconv_lib_darwin ],
  include_directories : [ dxmt_include_path, include_directories('..') ],
  link_args           : [ llvm_ld_flags_darwin, llvm_deps ] # meh
)

executable('airconv', airconv_src + airconv_cli_src,
  include_directories : [ dxmt_include_path, llvm_include_path_darwin ],
  cpp_args            : [ llvm_cxx_flags  ],
  dependencies        : [ DXBCParser_native_dep ],
  link_args           : [ llvm_ld_flags_darwin, llvm_deps ],
  native              : not dxmt_native
)