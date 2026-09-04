project('miniocpp', 'cpp', version : '0.6.0', default_options : ['cpp_std=c++17'])

cxx = meson.get_compiler('cpp')

# method: 'cmake' скрізь нижче (крім openssl - у Meson є вбудований
# спеціальний резолвер під нього) - на Windows-раннерах pkg-config може
# бути затінений нефункціональною заглушкою (напр. Strawberry Perl's
# pkg-config.bat), що ламає дефолтний auto-метод. Назви пакетів/таргетів
# узяті з того, що реально шукає CMake-версія цього ж репозиторію
# (cmake/miniocpp-deps.cmake).
openssl_dep = dependency('openssl')
httplib_dep = dependency('httplib', method : 'cmake', modules : ['httplib::httplib'])
inih_dep = dependency('unofficial-inih', method : 'cmake', modules : ['unofficial::inih::inireader'])
nlohmann_json_dep = dependency('nlohmann_json', method : 'cmake', modules : ['nlohmann_json::nlohmann_json'])
pugixml_dep = dependency('pugixml', method : 'cmake', modules : ['pugixml'])
zlib_dep = dependency('zlib', method : 'cmake', modules : ['ZLIB::ZLIB'])

extra_libs = []
if host_machine.system() == 'windows'
  extra_libs = [cxx.find_library('ws2_32'), cxx.find_library('wsock32')]
endif

all_deps = [openssl_dep, httplib_dep, inih_dep, nlohmann_json_dep, pugixml_dep, zlib_dep] + extra_libs
miniocpp_include = include_directories('include')

miniocpp_sources = files(
  'src/args.cc',
  'src/baseclient.cc',
  'src/client.cc',
  'src/credentials.cc',
  'src/error.cc',
  'src/http.cc',
  'src/providers.cc',
  'src/request.cc',
  'src/response.cc',
  'src/select.cc',
  'src/signer.cc',
  'src/sse.cc',
  'src/types.cc',
  'src/utils.cc',
)

# The real library - all .cc files from this repo, not the official minio-cpp
# vcpkg package.
miniocpp_lib = static_library('miniocpp', miniocpp_sources,
  include_directories : miniocpp_include,
  dependencies : all_deps,
)

build_module = (
  (cxx.get_id() == 'msvc' and cxx.version().version_compare('>=19.34'))
  or (cxx.get_id() == 'clang' and host_machine.system() != 'darwin'
      and cxx.version().version_compare('>=16.0'))
)

if build_module
  if cxx.get_id() == 'msvc'
    module_cpp_args = ['/interface']
  elif cxx.get_id() == 'clang'
    module_cpp_args = ['-x', 'c++-module']
  else
    module_cpp_args = []
  endif
  miniocpp_module_lib = static_library('miniocpp_module', 'modules/minio.cc',
    include_directories : miniocpp_include,
    dependencies : all_deps,
    cpp_args : module_cpp_args,
    override_options : ['cpp_std=c++20'],
  )
  miniocpp_dep = declare_dependency(
    link_with : [miniocpp_lib, miniocpp_module_lib],
    include_directories : miniocpp_include,
    dependencies : all_deps,
  )
else
  miniocpp_dep = declare_dependency(
    link_with : [miniocpp_lib],
    include_directories : miniocpp_include,
    dependencies : all_deps,
  )
endif
