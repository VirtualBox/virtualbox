#include "stddef.h"
#include "airconv_public.h"

#ifndef __WINEMETAL_UNIX_THUNKS_H
#define __WINEMETAL_UNIX_THUNKS_H

enum airconv_unixcalls {
  unix_sm50_initialize = 74,
  unix_sm50_destroy,
  unix_sm50_compile,
  unix_sm50_get_compiled_bitcode,
  unix_sm50_destroy_bitcode,
  unix_sm50_get_error_message,
  unix_sm50_free_error,
  unix_sm50_compile_geometry_pipeline_vertex,
  unix_sm50_compile_geometry_pipeline_geometry,
  unix_sm50_compile_tessellation_vertex,
  unix_sm50_compile_tessellation_hull,
  unix_sm50_compile_tessellation_domain,
  unix_sm50_get_arguments_info = 88,
};

struct sm50_initialize_params {
  const void *bytecode;
  size_t bytecode_size;
  sm50_shader_t *shader;
  void *reflection;
  sm50_error_t *error;
  int ret;
};

struct sm50_destroy_params {
  sm50_shader_t shader;
};

struct sm50_compile_params {
  sm50_shader_t shader;
  void *args;
  const char *func_name;
  sm50_bitcode_t *bitcode;
  sm50_error_t *error;
  int ret;
};

struct sm50_get_compiled_bitcode_params {
  sm50_bitcode_t bitcode;
  struct MTL_SHADER_BITCODE *data_out;
};

struct sm50_destroy_bitcode_params {
  sm50_bitcode_t bitcode;
};

struct sm50_get_error_message_params {
  sm50_error_t error;
  char *buffer;
  size_t buffer_size;
  size_t ret_size;
};

struct sm50_free_error_params {
  sm50_error_t error;
};

struct sm50_compile_geometry_pipeline_vertex_params {
  sm50_shader_t vertex;
  sm50_shader_t geometry;
  void *vertex_args;
  const char *func_name;
  sm50_bitcode_t *bitcode;
  sm50_error_t *error;
  int ret;
};

struct sm50_compile_geometry_pipeline_geometry_params {
  sm50_shader_t vertex;
  sm50_shader_t geometry;
  void *geometry_args;
  const char *func_name;
  sm50_bitcode_t *bitcode;
  sm50_error_t *error;
  int ret;
};

struct sm50_compile_tessellation_pipeline_vertex_params {
  sm50_shader_t vertex;
  sm50_shader_t hull;
  void *vertex_args;
  const char *func_name;
  sm50_bitcode_t *bitcode;
  sm50_error_t *error;
  int ret;
};

struct sm50_compile_tessellation_pipeline_hull_params {
  sm50_shader_t vertex;
  sm50_shader_t hull;
  void *hull_args;
  const char *func_name;
  sm50_bitcode_t *bitcode;
  sm50_error_t *error;
  int ret;
};

struct sm50_compile_tessellation_pipeline_domain_params {
  sm50_shader_t hull;
  sm50_shader_t domain;
  void *domain_args;
  const char *func_name;
  sm50_bitcode_t *bitcode;
  sm50_error_t *error;
  int ret;
};

struct sm50_get_arguments_info_params {
  sm50_shader_t shader;
  struct MTL_SM50_SHADER_ARGUMENT *constant_buffers;
  struct MTL_SM50_SHADER_ARGUMENT *arguments;
};

#if defined(__LP64__) || defined(_WIN64)
#define COMPATIBLE_STRUCT32(stru, size) _Static_assert(sizeof(struct stru##32) == size, "incompatible struct size");
#else
#define COMPATIBLE_STRUCT32(stru, size) _Static_assert(sizeof(struct stru) == size, "incompatible struct size");
#endif

struct sm50_initialize_params32 {
  uint32_t bytecode;
  unsigned int bytecode_size;
  uint32_t shader;
  uint32_t reflection;
  uint32_t error;
  int ret;
};

COMPATIBLE_STRUCT32(sm50_initialize_params, 24)

struct sm50_compile_params32 {
  sm50_shader_t shader;
  uint32_t args;
  uint32_t func_name;
  uint32_t bitcode;
  uint32_t error;
  int ret;
};

COMPATIBLE_STRUCT32(sm50_compile_params, 32)

struct sm50_get_compiled_bitcode_params32 {
  sm50_bitcode_t bitcode;
  uint32_t data_out;
};

COMPATIBLE_STRUCT32(sm50_get_compiled_bitcode_params, 16)

struct sm50_get_error_message_params32 {
  sm50_error_t error;
  uint32_t buffer;
  uint32_t buffer_size;
  uint32_t ret_size;
};

COMPATIBLE_STRUCT32(sm50_get_error_message_params, 24)

struct sm50_compile_geometry_pipeline_vertex_params32 {
  sm50_shader_t vertex;
  sm50_shader_t geometry;
  uint32_t vertex_args;
  uint32_t func_name;
  uint32_t bitcode;
  uint32_t error;
  int ret;
};

COMPATIBLE_STRUCT32(sm50_compile_geometry_pipeline_vertex_params, 40)

struct sm50_compile_geometry_pipeline_geometry_params32 {
  sm50_shader_t vertex;
  sm50_shader_t geometry;
  uint32_t geometry_args;
  uint32_t func_name;
  uint32_t bitcode;
  uint32_t error;
  int ret;
};

COMPATIBLE_STRUCT32(sm50_compile_geometry_pipeline_geometry_params, 40)

struct sm50_compile_tessellation_pipeline_vertex_params32 {
  sm50_shader_t vertex;
  sm50_shader_t hull;
  uint32_t vertex_args;
  uint32_t func_name;
  uint32_t bitcode;
  uint32_t error;
  int ret;
};

COMPATIBLE_STRUCT32(sm50_compile_tessellation_pipeline_vertex_params, 40)

struct sm50_compile_tessellation_pipeline_hull_params32 {
  sm50_shader_t vertex;
  sm50_shader_t hull;
  uint32_t hull_args;
  uint32_t func_name;
  uint32_t bitcode;
  uint32_t error;
  int ret;
};

COMPATIBLE_STRUCT32(sm50_compile_tessellation_pipeline_hull_params, 40)

struct sm50_compile_tessellation_pipeline_domain_params32 {
  sm50_shader_t hull;
  sm50_shader_t domain;
  uint32_t domain_args;
  uint32_t func_name;
  uint32_t bitcode;
  uint32_t error;
  int ret;
};

COMPATIBLE_STRUCT32(sm50_compile_tessellation_pipeline_domain_params, 40)

struct sm50_get_arguments_info_params32 {
  sm50_shader_t shader;
  uint32_t constant_buffers;
  uint32_t arguments;
};

COMPATIBLE_STRUCT32(sm50_get_arguments_info_params, 16)

#define UNIX_CALL(code, params) WINE_UNIX_CALL(unix_##code, params)

#endif